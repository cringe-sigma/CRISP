#define _GNU_SOURCE

#include <asm/unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define NPROC 4          // Number of processes: fixed at 4 here
#define NEVENTS 3        // Number of PMU events tracked: cycles / instructions / cache-misses

/*
 * Return-value layout for a group read.
 * Because we use PERF_FORMAT_GROUP | PERF_FORMAT_ID,
 * a single read(group_leader_fd, ...) returns the values of all events
 * in the group at once.
 */
struct read_format {
    uint64_t nr;   // Number of events in the current group
    struct {
        uint64_t value;  // Counter value for the event
        uint64_t id;     // Unique id of the event, used to tell which event this value belongs to
    } values[NEVENTS];
};

/* ------------------------------------------------------------------ */
/* Bench dispatch table                                               */
/* ------------------------------------------------------------------ */

/*
 * Forward declarations of the TACLeBench entry points we link against.
 * Each bench's own `int main()` is neutralized at compile time via
 * `-Dmain=<bench>_main_disabled` in the Makefile, so there are no
 * duplicate-symbol conflicts at link time.
 */
#define BENCH_DECL(name)                  \
    void name##_init  ( void );           \
    void name##_main  ( void );           \
    int  name##_return( void )

BENCH_DECL(binarysearch);
BENCH_DECL(bitcount);
BENCH_DECL(bitonic);
BENCH_DECL(bsort);
BENCH_DECL(complex_updates);
BENCH_DECL(cosf);
BENCH_DECL(countnegative);
BENCH_DECL(cubic);
BENCH_DECL(deg2rad);
BENCH_DECL(fac);
BENCH_DECL(fft);
BENCH_DECL(filterbank);
BENCH_DECL(fir2dim);
BENCH_DECL(iir);
BENCH_DECL(insertsort);
BENCH_DECL(isqrt);
BENCH_DECL(jfdctint);
BENCH_DECL(lms);
BENCH_DECL(ludcmp);
BENCH_DECL(matrix1);
BENCH_DECL(md5);
BENCH_DECL(minver);
BENCH_DECL(pm);
BENCH_DECL(prime);
BENCH_DECL(quicksort);
BENCH_DECL(rad2deg);
BENCH_DECL(recursion);
BENCH_DECL(sha);
BENCH_DECL(st);

#undef BENCH_DECL

typedef struct {
    const char *name;
    void      (*init)(void);    // Setup (excluded from PMU measurement)
    void      (*run)(void);     // The actual measured region
    int       (*verify)(void);  // Optional: returns a checksum / result code
} bench_t;

#define BENCH_ENTRY(name) { #name, name##_init, name##_main, name##_return }

static const bench_t BENCHES[] = {
    BENCH_ENTRY(binarysearch),
    BENCH_ENTRY(bitcount),
    BENCH_ENTRY(bitonic),
    BENCH_ENTRY(bsort),
    BENCH_ENTRY(complex_updates),
    BENCH_ENTRY(cosf),
    BENCH_ENTRY(countnegative),
    BENCH_ENTRY(cubic),
    BENCH_ENTRY(deg2rad),
    BENCH_ENTRY(fac),
    BENCH_ENTRY(fft),
    BENCH_ENTRY(filterbank),
    BENCH_ENTRY(fir2dim),
    BENCH_ENTRY(iir),
    BENCH_ENTRY(insertsort),
    BENCH_ENTRY(isqrt),
    BENCH_ENTRY(jfdctint),
    BENCH_ENTRY(lms),
    BENCH_ENTRY(ludcmp),
    BENCH_ENTRY(matrix1),
    BENCH_ENTRY(md5),
    BENCH_ENTRY(minver),
    BENCH_ENTRY(pm),
    BENCH_ENTRY(prime),
    BENCH_ENTRY(quicksort),
    BENCH_ENTRY(rad2deg),
    BENCH_ENTRY(recursion),
    BENCH_ENTRY(sha),
    BENCH_ENTRY(st),
};

#undef BENCH_ENTRY
#define NBENCHES (sizeof(BENCHES) / sizeof(BENCHES[0]))

static const bench_t *lookup_bench(const char *name)
{
    for (size_t i = 0; i < NBENCHES; i++) {
        if (strcmp(BENCHES[i].name, name) == 0) {
            return &BENCHES[i];
        }
    }
    return NULL;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-n N] <bench_for_cpu0> [bench_for_cpu1] [bench_for_cpu2] [bench_for_cpu3]\n"
        "  -n N            Repeat the measured PMU collection N times (1..1000,\n"
        "                  default 1). Background cores keep running across all\n"
        "                  iterations. With N>1, prints min/max/avg/median.\n"
        "  Each positional argument selects the bench for the matching CPU\n"
        "  core (1..%d arguments accepted). Cores without an argument stay idle.\n"
        "  CPU 0 is always the measured core (PMU collected); CPU 1..%d run\n"
        "  their bench in a background loop without PMU until the measured\n"
        "  run finishes.\n"
        "  Available benches (%zu):\n",
        prog, NPROC, NPROC - 1, NBENCHES);
    for (size_t i = 0; i < NBENCHES; i++) {
        fprintf(stderr, "    %s%s", BENCHES[i].name,
                ((i + 1) % 4 == 0 || i + 1 == NBENCHES) ? "\n" : "");
    }
}

/* ------------------------------------------------------------------ */
/* perf_event_open plumbing                                           */
/* ------------------------------------------------------------------ */

/*
 * Wrapper for the perf_event_open system call.
 * libc does not provide a wrapper for it, so we have to invoke it via syscall().
 */
static long
sys_perf_event_open(struct perf_event_attr *attr, pid_t pid,
                    int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/*
 * Bind the current process to the specified CPU core.
 * For example, cpu=0 means the current process is only allowed to run on CPU0.
 */
static void bind_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
}

/*
 * Open a group-leader event.
 * The leader is typically cycles; other events are attached to it as group members.
 *
 * Here we use:
 *   pid = 0   -> count the current process
 *   cpu = -1  -> follow the current process, rather than counting
 *                everything happening on a particular CPU
 *
 * Since we have already pinned the process to a core, the combination
 * "current process + fixed CPU" gives us per-process data on that core.
 */
static int open_leader_event(uint64_t config)
{
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));

    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = config;

    pe.disabled = 1;         // Created in disabled state; enabled later when entering the measured region
    pe.exclude_kernel = 1;   // Count user-space only, exclude the kernel
    pe.exclude_hv = 1;       // Exclude hypervisor
    pe.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

    int fd = sys_perf_event_open(&pe, 0, -1, -1, 0);
    if (fd == -1) {
        perror("perf_event_open leader");
        exit(EXIT_FAILURE);
    }
    return fd;
}

/*
 * Open a group-member event, attaching it to the group identified by leader_fd.
 */
static int open_member_event(int leader_fd, uint64_t config)
{
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));

    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = config;

    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

    int fd = sys_perf_event_open(&pe, 0, -1, leader_fd, 0);
    if (fd == -1) {
        perror("perf_event_open member");
        exit(EXIT_FAILURE);
    }
    return fd;
}

/* ------------------------------------------------------------------ */
/* Child process                                                      */
/* ------------------------------------------------------------------ */

/*
 * Background children (cpu 1..NPROC-1) loop until they receive SIGUSR1
 * from the parent. The handler just flips this flag.
 */
static volatile sig_atomic_t bg_stop_flag = 0;
static void bg_stop_handler(int sig) { (void)sig; bg_stop_flag = 1; }

/*
 * Background child:
 *   - bind to CPU
 *   - bench init()
 *   - wait for start signal from parent
 *   - loop bench->run() until SIGUSR1 arrives
 * No PMU is opened here.
 */
static void child_background(int cpu_id, const bench_t *b, int start_fd)
{
    bind_to_cpu(cpu_id);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = bg_stop_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* allow read() to return EINTR if needed */
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    b->init();

    char ch;
    if (read(start_fd, &ch, 1) != 1) {
        perror("read start signal");
        exit(EXIT_FAILURE);
    }
    close(start_fd);

    uint64_t iters = 0;
    while (!bg_stop_flag) {
        b->run();
        iters++;
    }

    printf("[bg]    pid=%d cpu=%d bench=%-13s iterations=%" PRIu64 "\n",
           getpid(), cpu_id, b->name, iters);

    exit(EXIT_SUCCESS);
}

/*
 * Measured child (cpu 0):
 *   - bind to CPU
 *   - open PMU group
 *   - bench init()
 *   - wait for start signal
 *   - repeat n times: reset + enable PMU, run(), disable, read sample
 *   - print per-iteration line and (if n > 1) min/max/avg/median stats
 */
static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void child_measured(int cpu_id, const bench_t *b, int start_fd, int repeat)
{
    bind_to_cpu(cpu_id);

    int fd_cycles = open_leader_event(PERF_COUNT_HW_CPU_CYCLES);
    int fd_instr  = open_member_event(fd_cycles, PERF_COUNT_HW_INSTRUCTIONS);
    int fd_cachem = open_member_event(fd_cycles, PERF_COUNT_HW_CACHE_MISSES);

    uint64_t id_cycles = 0, id_instr = 0, id_cachem = 0;

    if (ioctl(fd_cycles, PERF_EVENT_IOC_ID, &id_cycles) == -1) {
        perror("ioctl PERF_EVENT_IOC_ID cycles");
        exit(EXIT_FAILURE);
    }
    if (ioctl(fd_instr, PERF_EVENT_IOC_ID, &id_instr) == -1) {
        perror("ioctl PERF_EVENT_IOC_ID instr");
        exit(EXIT_FAILURE);
    }
    if (ioctl(fd_cachem, PERF_EVENT_IOC_ID, &id_cachem) == -1) {
        perror("ioctl PERF_EVENT_IOC_ID cachem");
        exit(EXIT_FAILURE);
    }

    b->init();

    char ch;
    if (read(start_fd, &ch, 1) != 1) {
        perror("read start signal");
        exit(EXIT_FAILURE);
    }

    uint64_t *cyc_arr = malloc(sizeof(uint64_t) * (size_t)repeat);
    uint64_t *ins_arr = malloc(sizeof(uint64_t) * (size_t)repeat);
    uint64_t *cm_arr  = malloc(sizeof(uint64_t) * (size_t)repeat);
    if (!cyc_arr || !ins_arr || !cm_arr) {
        perror("malloc samples");
        exit(EXIT_FAILURE);
    }

    int last_rc = 0;
    for (int it = 0; it < repeat; it++) {
        if (ioctl(fd_cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
            perror("ioctl RESET");
            exit(EXIT_FAILURE);
        }
        if (ioctl(fd_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
            perror("ioctl ENABLE");
            exit(EXIT_FAILURE);
        }

        b->run();

        if (ioctl(fd_cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
            perror("ioctl DISABLE");
            exit(EXIT_FAILURE);
        }

        struct read_format rf;
        memset(&rf, 0, sizeof(rf));
        ssize_t n = read(fd_cycles, &rf, sizeof(rf));
        if (n < 0) {
            perror("read perf group");
            exit(EXIT_FAILURE);
        }

        uint64_t cycles = 0, instructions = 0, cache_misses = 0;
        for (uint64_t i = 0; i < rf.nr; i++) {
            if (rf.values[i].id == id_cycles)      cycles       = rf.values[i].value;
            else if (rf.values[i].id == id_instr)  instructions = rf.values[i].value;
            else if (rf.values[i].id == id_cachem) cache_misses = rf.values[i].value;
        }

        cyc_arr[it] = cycles;
        ins_arr[it] = instructions;
        cm_arr[it]  = cache_misses;
        last_rc = b->verify();

        printf("[meas]  pid=%d cpu=%d bench=%-13s iter=%-4d "
               "cycles=%" PRIu64
               " instructions=%" PRIu64
               " cache-misses=%" PRIu64
               " return=%d\n",
               getpid(), cpu_id, b->name, it,
               cycles, instructions, cache_misses, last_rc);
    }

    if (repeat > 1) {
        uint64_t *arrs[3]   = { cyc_arr, ins_arr, cm_arr };
        const char *labels[3] = { "cycles      ",
                                  "instructions",
                                  "cache-misses" };
        printf("[stats] pid=%d cpu=%d bench=%-13s n=%d\n",
               getpid(), cpu_id, b->name, repeat);
        for (int k = 0; k < 3; k++) {
            qsort(arrs[k], (size_t)repeat, sizeof(uint64_t), cmp_u64);
            uint64_t mn = arrs[k][0];
            uint64_t mx = arrs[k][repeat - 1];
            /* sum may overflow 64-bit only for absurd inputs; acceptable. */
            uint64_t sum = 0;
            for (int j = 0; j < repeat; j++) sum += arrs[k][j];
            double avg = (double)sum / (double)repeat;
            double med;
            if (repeat % 2 == 1) {
                med = (double)arrs[k][repeat / 2];
            } else {
                med = ((double)arrs[k][repeat / 2 - 1]
                     + (double)arrs[k][repeat / 2]) / 2.0;
            }
            printf("        %s  min=%-12" PRIu64
                   " max=%-12" PRIu64
                   " avg=%-14.2f median=%.2f\n",
                   labels[k], mn, mx, avg, med);
        }
    }

    free(cyc_arr);
    free(ins_arr);
    free(cm_arr);

    close(fd_cachem);
    close(fd_instr);
    close(fd_cycles);
    close(start_fd);

    exit(EXIT_SUCCESS);
}

/* ------------------------------------------------------------------ */
/* Parent / orchestration                                             */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    /*
     * Optional `-n N` flag: how many times to repeat the measured PMU
     * collection on CPU 0. Default 1, allowed range 1..1000.
     * The remaining argv[] entries are bench names for cpu0..cpuN-1.
     */
    int repeat = 1;
    int argi = 1;
    if (argi < argc && strcmp(argv[argi], "-n") == 0) {
        if (argi + 1 >= argc) {
            fprintf(stderr, "error: -n requires an integer argument\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        char *endp = NULL;
        long val = strtol(argv[argi + 1], &endp, 10);
        if (!endp || *endp != '\0' || val < 1 || val > 1000) {
            fprintf(stderr,
                "error: -n value must be an integer in 1..1000 (got '%s')\n",
                argv[argi + 1]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        repeat = (int)val;
        argi += 2;
    }

    int positional = argc - argi;
    if (positional < 1 || positional > NPROC) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const bench_t *chosen[NPROC] = { 0 };
    int nactive = positional;
    for (int i = 0; i < nactive; i++) {
        chosen[i] = lookup_bench(argv[argi + i]);
        if (!chosen[i]) {
            fprintf(stderr, "error: unknown bench '%s'\n", argv[argi + i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    pid_t pids[NPROC] = { 0 };
    int start_pipe[NPROC][2];

    /*
     * Create a pipe per ACTIVE child. The parent uses write() to send
     * the start signal, the child uses read() to wait on it.
     */
    for (int i = 0; i < nactive; i++) {
        if (pipe(start_pipe[i]) < 0) {
            perror("pipe");
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < nactive; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            // Child: close the pipe ends it does not use
            close(start_pipe[i][1]); // child does not write

            // Close other active children's pipes
            for (int j = 0; j < nactive; j++) {
                if (j != i) {
                    close(start_pipe[j][0]);
                    close(start_pipe[j][1]);
                }
            }

            /*
             * CPU 0 is the measured core; CPUs 1..nactive-1 act as
             * background workload that runs continuously without PMU.
             */
            if (i == 0) {
                child_measured(i, chosen[i], start_pipe[i][0], repeat);
            } else {
                child_background(i, chosen[i], start_pipe[i][0]);
            }
        } else {
            pids[i] = pid;
            close(start_pipe[i][0]);
        }
    }

    /*
     * Give every child time to pin its core, init the bench (and PMU
     * for cpu 0) before sending start signals.
     */
    sleep(1);

    /*
     * Step A: start background workers on cpu 1..nactive-1 first so
     *         they are already busy when the measured core begins.
     */
    for (int i = 1; i < nactive; i++) {
        if (write(start_pipe[i][1], "S", 1) != 1) {
            perror("write start signal (bg)");
        }
        close(start_pipe[i][1]);
    }

    /* Let the background cores actually enter their run loops. */
    if (nactive > 1) {
        usleep(200 * 1000); /* 200 ms */
    }

    /*
     * Step B: release the measured core (cpu 0). PMU collection happens
     *         inside child_measured() while the active background cores
     *         keep hammering their benches.
     */
    if (write(start_pipe[0][1], "S", 1) != 1) {
        perror("write start signal (measured)");
    }
    close(start_pipe[0][1]);

    /* Wait for the measured child first. */
    waitpid(pids[0], NULL, 0);

    /* Step C: tell the background workers to stop and reap them. */
    for (int i = 1; i < nactive; i++) {
        kill(pids[i], SIGUSR1);
    }
    for (int i = 1; i < nactive; i++) {
        waitpid(pids[i], NULL, 0);
    }

    return 0;
}
