#define _GNU_SOURCE

#include <asm/unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define NPROC 4          // Number of processes: fixed at 4 here
#define NEVENTS 3        // Number of PMU events tracked: cycles / instructions / cache-misses

/*
 * Size of the buffer used to evict L1/L2/LLC caches before each measurement.
 * Should be larger than the last-level cache of the target SoC.
 * 32 MiB is conservative for most embedded ARM boards.
 */
/*
 * Compile-time default for the locked CPU frequency, in kHz.
 * Override via -f on the command line, or by passing
 * -DDEFAULT_LOCK_FREQ_KHZ=<value> at build time.
 * 0 means "auto": use min(scaling_max_freq) across active cores.
 */
#ifndef DEFAULT_LOCK_FREQ_KHZ
#define DEFAULT_LOCK_FREQ_KHZ 0ULL
#endif

#define CACHE_FLUSH_BYTES (32u * 1024u * 1024u)

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
/* CPU frequency / idle-state pinning                                  */
/* ------------------------------------------------------------------ */

/*
 * Per-CPU saved cpufreq state for the NPROC active cores so that the
 * original governor / min / max frequency can be restored on exit.
 */
static char saved_governor[NPROC][64];
static char saved_min_freq[NPROC][32];
static char saved_max_freq[NPROC][32];
static int  governor_saved[NPROC] = {0};
static int  min_freq_saved[NPROC] = {0};
static int  max_freq_saved[NPROC] = {0};

/*
 * Saved /sys/devices/system/cpu/cpuN/online state for cores that we
 * forced offline (i.e. cpu indices >= NPROC). Sized to a generous upper
 * bound; entries beyond the actual CPU count remain 0.
 */
#define MAX_TRACKED_CPUS 256
static int  cpu_was_online[MAX_TRACKED_CPUS] = {0};
static int  cpu_offline_done[MAX_TRACKED_CPUS] = {0};
static int  total_cpus_detected = 0;

/*
 * File descriptor kept open against /dev/cpu_dma_latency.
 * Writing a 32-bit 0 and keeping the fd open requests a PM-QoS DMA
 * latency of 0 us, which prevents the kernel from entering deep C-states
 * (idle states) on any CPU for the lifetime of this process.
 */
static int cpu_dma_latency_fd = -1;

static int read_file(const char *path, char *buf, size_t buflen)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, buflen - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    /* Strip trailing newline / whitespace */
    while (n > 0 && (buf[n - 1] == '\n' || isspace((unsigned char)buf[n - 1]))) {
        buf[--n] = '\0';
    }
    return 0;
}

static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return (n >= 0) ? 0 : -1;
}

/*
 * Read /sys/devices/system/cpu/cpuN/cpufreq/scaling_max_freq for the given
 * CPU. Returns the value in kHz on success, or 0 on failure.
 */
static unsigned long long read_max_freq(int cpu)
{
    char path[256], buf[64];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
    if (read_file(path, buf, sizeof(buf)) != 0) return 0;
    return strtoull(buf, NULL, 10);
}

/*
 * Read /sys/devices/system/cpu/cpuN/cpufreq/scaling_available_frequencies for
 * the given CPU and check whether `khz` appears in it (exact match).
 * Returns 1 on match, 0 on no match, -1 on read failure (e.g. attribute
 * missing on this driver — common on intel_pstate / schedutil setups).
 */
static int freq_is_available(int cpu, unsigned long long khz)
{
    char path[256], buf[1024];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies",
             cpu);
    if (read_file(path, buf, sizeof(buf)) != 0) return -1;

    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t\n", &save); tok;
         tok = strtok_r(NULL, " \t\n", &save)) {
        if (strtoull(tok, NULL, 10) == khz) return 1;
    }
    return 0;
}

/*
 * Lock CPUs 0..NPROC-1 to a single, identical frequency.
 *
 * If `requested_khz` is non-zero, that value (in kHz) is used as the
 * target frequency. The function verifies it is within
 * [cpuinfo_min_freq, cpuinfo_max_freq] for every active core, and warns
 * if it is not listed in scaling_available_frequencies (which on some
 * drivers is absent altogether — that's fine, the kernel will accept any
 * value inside the [min, max] range).
 *
 * If `requested_khz` is 0, the function falls back to min(scaling_max_freq)
 * across all active cores, which always yields a value reachable on every
 * one of them (handy for quick runs and for asymmetric big.LITTLE SoCs).
 */
static void pin_cpu_frequency(unsigned long long requested_khz)
{
    char path[256];

    for (int cpu = 0; cpu < NPROC; cpu++) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
        if (read_file(path, saved_governor[cpu], sizeof(saved_governor[cpu])) == 0) {
            governor_saved[cpu] = 1;
            if (write_file(path, "performance") != 0) {
                fprintf(stderr,
                        "[warn] cannot set %s to 'performance' (errno=%d: %s)\n",
                        path, errno, strerror(errno));
                governor_saved[cpu] = 0;
            }
        } else {
            fprintf(stderr,
                    "[warn] cannot read %s (errno=%d: %s); cpufreq pinning skipped for cpu%d\n",
                    path, errno, strerror(errno), cpu);
        }

        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
        if (read_file(path, saved_min_freq[cpu], sizeof(saved_min_freq[cpu])) == 0) {
            min_freq_saved[cpu] = 1;
        }

        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        if (read_file(path, saved_max_freq[cpu], sizeof(saved_max_freq[cpu])) == 0) {
            max_freq_saved[cpu] = 1;
        }
    }

    unsigned long long target_khz = requested_khz;

    if (target_khz != 0) {
        /* Validate the user-supplied frequency against every active core. */
        for (int cpu = 0; cpu < NPROC; cpu++) {
            char buf[32];
            unsigned long long lo = 0, hi = 0;

            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", cpu);
            if (read_file(path, buf, sizeof(buf)) == 0) lo = strtoull(buf, NULL, 10);

            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
            if (read_file(path, buf, sizeof(buf)) == 0) hi = strtoull(buf, NULL, 10);

            if (lo != 0 && hi != 0 && (target_khz < lo || target_khz > hi)) {
                fprintf(stderr,
                        "[error] requested frequency %llu kHz is outside cpu%d's "
                        "supported range [%llu, %llu] kHz\n",
                        target_khz, cpu, lo, hi);
                target_khz = 0;
                break;
            }

            int avail = freq_is_available(cpu, target_khz);
            if (avail == 0) {
                fprintf(stderr,
                        "[warn] cpu%d does not list %llu kHz in "
                        "scaling_available_frequencies; the cpufreq driver "
                        "may snap to the nearest supported step\n",
                        cpu, target_khz);
            }
        }
    }

    if (target_khz == 0) {
        /* Fallback: lowest scaling_max_freq across the active cores. */
        for (int cpu = 0; cpu < NPROC; cpu++) {
            unsigned long long f = read_max_freq(cpu);
            if (f == 0) continue;
            if (target_khz == 0 || f < target_khz) target_khz = f;
        }
    }

    if (target_khz == 0) {
        fprintf(stderr,
                "[warn] could not determine a target frequency; cores will run "
                "under the 'performance' governor without an explicit min=max pin.\n");
        return;
    }

    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "%llu", target_khz);

    for (int cpu = 0; cpu < NPROC; cpu++) {
        char min_path[256], max_path[256];
        snprintf(min_path, sizeof(min_path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
        snprintf(max_path, sizeof(max_path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);

        /* First lower scaling_max_freq, then raise scaling_min_freq up to
         * it. The kernel rejects writes that would leave min > max
         * momentarily; doing max first avoids that. */
        if (write_file(max_path, freq_str) != 0) {
            fprintf(stderr,
                    "[warn] cannot pin %s = %s (errno=%d: %s)\n",
                    max_path, freq_str, errno, strerror(errno));
        }
        if (write_file(min_path, freq_str) != 0) {
            fprintf(stderr,
                    "[warn] cannot pin %s = %s (errno=%d: %s)\n",
                    min_path, freq_str, errno, strerror(errno));
        }
    }

    fprintf(stderr,
            "[info] cpu0..cpu%d locked to %llu kHz via 'performance' governor%s\n",
            NPROC - 1, target_khz,
            (requested_khz != 0 && requested_khz == target_khz) ? " (user-specified)" : "");
}

static void restore_cpu_frequency(void)
{
    char path[256];

    /* Restore in the safe order: widen scaling_max_freq first (back to
     * its original, possibly higher value), then restore scaling_min_freq,
     * then the governor. */
    for (int cpu = 0; cpu < NPROC; cpu++) {
        if (max_freq_saved[cpu]) {
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
            write_file(path, saved_max_freq[cpu]);
        }
        if (min_freq_saved[cpu]) {
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
            write_file(path, saved_min_freq[cpu]);
        }
        if (governor_saved[cpu]) {
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
            write_file(path, saved_governor[cpu]);
        }
    }
}

/*
 * Hot-unplug (offline) every CPU outside the active set [0..NPROC-1] so
 * that they do not run any other workloads, do not steal LLC/memory
 * bandwidth, and stay in their lowest power state for the duration of
 * the measurement. The original online state is remembered so that the
 * cores can be brought back online in restore_offline_cpus() on exit.
 *
 * Note: cpu0 is typically not hot-unpluggable on Linux, but cpu0 is in
 * the active set anyway, so this never tries to offline it.
 */
static void offline_other_cpus(void)
{
    long n = sysconf(_SC_NPROCESSORS_CONF);
    if (n <= 0) {
        fprintf(stderr,
                "[warn] sysconf(_SC_NPROCESSORS_CONF) failed; cannot offline extra cores\n");
        return;
    }
    if (n > MAX_TRACKED_CPUS) n = MAX_TRACKED_CPUS;
    total_cpus_detected = (int)n;

    char path[256], buf[16];
    for (int cpu = NPROC; cpu < total_cpus_detected; cpu++) {
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/online", cpu);

        if (read_file(path, buf, sizeof(buf)) != 0) {
            /* Some CPUs (often cpu0) lack the 'online' attribute. Skip. */
            continue;
        }
        cpu_was_online[cpu] = (buf[0] == '1') ? 1 : 0;

        if (cpu_was_online[cpu]) {
            if (write_file(path, "0") == 0) {
                cpu_offline_done[cpu] = 1;
            } else {
                fprintf(stderr,
                        "[warn] cannot offline cpu%d via %s (errno=%d: %s)\n",
                        cpu, path, errno, strerror(errno));
            }
        }
    }

    if (total_cpus_detected > NPROC) {
        fprintf(stderr,
                "[info] offlined cpu%d..cpu%d to isolate the active cores\n",
                NPROC, total_cpus_detected - 1);
    }
}

static void restore_offline_cpus(void)
{
    char path[256];
    for (int cpu = 0; cpu < total_cpus_detected; cpu++) {
        if (!cpu_offline_done[cpu]) continue;
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/online", cpu);
        write_file(path, "1");
    }
}

/*
 * Block deep CPU idle states (C-states) by registering a 0us DMA latency
 * requirement via /dev/cpu_dma_latency. The fd must remain open for the
 * QoS request to stay in effect; closing it (or process exit) revokes it.
 */
static void block_cpu_idle_states(void)
{
    cpu_dma_latency_fd = open("/dev/cpu_dma_latency", O_WRONLY);
    if (cpu_dma_latency_fd < 0) {
        fprintf(stderr,
                "[warn] cannot open /dev/cpu_dma_latency (errno=%d: %s); "
                "deep C-states will not be blocked\n",
                errno, strerror(errno));
        return;
    }
    int32_t latency = 0;
    if (write(cpu_dma_latency_fd, &latency, sizeof(latency)) != sizeof(latency)) {
        fprintf(stderr,
                "[warn] write(/dev/cpu_dma_latency) failed (errno=%d: %s)\n",
                errno, strerror(errno));
        close(cpu_dma_latency_fd);
        cpu_dma_latency_fd = -1;
    }
}

static void release_cpu_idle_block(void)
{
    if (cpu_dma_latency_fd >= 0) {
        close(cpu_dma_latency_fd);
        cpu_dma_latency_fd = -1;
    }
}

static void on_exit_cleanup(void)
{
    /* Restore order matters: bring cores back online BEFORE touching their
     * cpufreq sysfs nodes, otherwise writes to offline cores' attributes
     * would silently fail. */
    restore_offline_cpus();
    restore_cpu_frequency();
    release_cpu_idle_block();
}

/* ------------------------------------------------------------------ */
/* perf_event_open helpers                                             */
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
/* Cache flushing                                                      */
/* ------------------------------------------------------------------ */

/*
 * Walk a buffer larger than the last-level cache to evict any cache lines
 * left over from previous activity, ensuring a "cold cache" baseline for
 * the next measurement. The volatile read prevents the compiler from
 * removing the loop.
 */
static void flush_caches(volatile uint8_t *buf, size_t len)
{
    /* Stride of 64 bytes matches a typical cache line size. */
    const size_t stride = 64;
    volatile uint8_t sink = 0;
    for (size_t i = 0; i < len; i += stride) {
        sink ^= buf[i];
    }
    (void)sink;
}

/* ------------------------------------------------------------------ */
/* Workload                                                            */
/* ------------------------------------------------------------------ */

/*
 * This is the "code under measurement". Replace with whatever function or
 * logic you actually want to profile.
 *
 * For demonstration, each process runs a simple computation loop.
 * cpu_id participates in the computation only to make the workload
 * slightly different across processes.
 */
static void target_work(int cpu_id)
{
    volatile uint64_t sum = 0;

    for (uint64_t i = 0; i < 300000000ULL; i++) {
        sum += (i ^ (uint64_t)cpu_id);
    }

    /* Prevent the compiler from optimizing the loop away. */
    (void)sum;
}

/* ------------------------------------------------------------------ */
/* Child entry: per-iteration measurement                              */
/* ------------------------------------------------------------------ */

/*
 * Child process entry function:
 * 1. Bind to a CPU core
 * 2. Allocate the cache-flush buffer (so each freshly-forked child starts cold)
 * 3. Open the PMU events
 * 4. Wait for the start signal from the parent
 * 5. Flush caches, then reset + enable
 * 6. Execute the target code region
 * 7. disable
 * 8. Read the results and print them
 */
static void child_main(int cpu_id, int iteration, int start_fd)
{
    // Step 1: pin this child process to the specified CPU
    bind_to_cpu(cpu_id);

    // Step 2: allocate flush buffer. Allocated *after* fork so this child
    // owns fresh, never-touched-by-the-parent pages.
    uint8_t *flush_buf = (uint8_t *)malloc(CACHE_FLUSH_BYTES);
    if (!flush_buf) {
        perror("malloc flush_buf");
        exit(EXIT_FAILURE);
    }
    /* Touch the buffer once to make sure pages are populated; otherwise
     * the first access in flush_caches() would page-fault inside the
     * measured region of subsequent iterations (here only one, but keep
     * the contract clean). */
    memset(flush_buf, 0, CACHE_FLUSH_BYTES);

    // Step 3: create a group of PMU events.
    int fd_cycles = open_leader_event(PERF_COUNT_HW_CPU_CYCLES);
    int fd_instr  = open_member_event(fd_cycles, PERF_COUNT_HW_INSTRUCTIONS);
    int fd_cachem = open_member_event(fd_cycles, PERF_COUNT_HW_CACHE_MISSES);

    uint64_t id_cycles = 0, id_instr = 0, id_cachem = 0;
    if (ioctl(fd_cycles, PERF_EVENT_IOC_ID, &id_cycles) == -1 ||
        ioctl(fd_instr,  PERF_EVENT_IOC_ID, &id_instr)  == -1 ||
        ioctl(fd_cachem, PERF_EVENT_IOC_ID, &id_cachem) == -1) {
        perror("ioctl PERF_EVENT_IOC_ID");
        exit(EXIT_FAILURE);
    }

    // Step 4: wait for the start signal from the parent process.
    char ch;
    if (read(start_fd, &ch, 1) != 1) {
        perror("read start signal");
        exit(EXIT_FAILURE);
    }

    // Step 5a: evict caches just before the measured region so every
    // iteration starts from a cold-cache baseline.
    flush_caches(flush_buf, CACHE_FLUSH_BYTES);

    // Step 5b: reset + enable counters
    if (ioctl(fd_cycles, PERF_EVENT_IOC_RESET,  PERF_IOC_FLAG_GROUP) == -1 ||
        ioctl(fd_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
        perror("ioctl RESET/ENABLE");
        exit(EXIT_FAILURE);
    }

    // Step 6: run the actual code region under measurement
    target_work(cpu_id);

    // Step 7: stop counting on the whole group
    if (ioctl(fd_cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
        perror("ioctl DISABLE");
        exit(EXIT_FAILURE);
    }

    // Step 8: read the PMU results from the group leader.
    struct read_format rf;
    memset(&rf, 0, sizeof(rf));
    ssize_t n = read(fd_cycles, &rf, sizeof(rf));
    if (n < 0) {
        perror("read perf group");
        exit(EXIT_FAILURE);
    }

    uint64_t cycles = 0, instructions = 0, cache_misses = 0;
    for (uint64_t i = 0; i < rf.nr; i++) {
        if      (rf.values[i].id == id_cycles) cycles       = rf.values[i].value;
        else if (rf.values[i].id == id_instr)  instructions = rf.values[i].value;
        else if (rf.values[i].id == id_cachem) cache_misses = rf.values[i].value;
    }

    printf("[iter=%d] pid=%d cpu=%d cycles=%" PRIu64
           " instructions=%" PRIu64
           " cache-misses=%" PRIu64 "\n",
           iteration, getpid(), cpu_id, cycles, instructions, cache_misses);
    fflush(stdout);

    close(fd_cachem);
    close(fd_instr);
    close(fd_cycles);
    close(start_fd);
    free(flush_buf);

    exit(EXIT_SUCCESS);
}

/* ------------------------------------------------------------------ */
/* Parent: one full iteration (fork �?? start �?? wait)                    */
/* ------------------------------------------------------------------ */

/*
 * Run one fully cold-started iteration: every iteration spawns a brand-new
 * set of child processes, so each measurement starts with empty TLBs,
 * fresh page tables, untrained branch predictors, and (after flush_caches)
 * cold data/instruction caches.
 */
static int run_one_iteration(int iteration)
{
    pid_t pids[NPROC];
    int start_pipe[NPROC][2];

    for (int i = 0; i < NPROC; i++) {
        if (pipe(start_pipe[i]) < 0) {
            perror("pipe");
            return -1;
        }
    }

    for (int i = 0; i < NPROC; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            // Child: keep only its own read end open
            close(start_pipe[i][1]);
            for (int j = 0; j < NPROC; j++) {
                if (j != i) {
                    close(start_pipe[j][0]);
                    close(start_pipe[j][1]);
                }
            }
            child_main(i, iteration, start_pipe[i][0]);
            /* not reached */
        } else {
            pids[i] = pid;
            close(start_pipe[i][0]);  // parent only writes
        }
    }

    /* Give children a moment to bind, allocate and arm PMUs. */
    usleep(200 * 1000);

    /* Broadcast start signal. */
    for (int i = 0; i < NPROC; i++) {
        if (write(start_pipe[i][1], "S", 1) != 1) {
            perror("write start signal");
        }
        close(start_pipe[i][1]);
    }

    for (int i = 0; i < NPROC; i++) {
        waitpid(pids[i], NULL, 0);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-n N] [-f KHZ]\n"
            "  -n N      Number of cold-start iterations to run (default: 1)\n"
            "  -f KHZ    Lock active cores to this exact frequency, in kHz.\n"
            "            Must lie within [cpuinfo_min_freq, cpuinfo_max_freq];\n"
            "            a warning is printed if the value is not listed in\n"
            "            scaling_available_frequencies. Default: %llu\n"
            "            (0 = auto, picks min(scaling_max_freq) across active cores).\n"
            "\n"
            "Each iteration spawns NPROC=%d child processes from scratch and\n"
            "flushes the data caches before the measured region, so every\n"
            "iteration observes a true cold-start baseline.\n"
            "\n"
            "On startup the program also:\n"
            "  * offlines every CPU outside cpu0..cpu%d (writes 0 to the\n"
            "    corresponding /sys/devices/system/cpu/cpuN/online);\n"
            "  * locks the active cores to the selected frequency by setting\n"
            "    the 'performance' governor and pinning\n"
            "    scaling_min_freq == scaling_max_freq == target;\n"
            "  * blocks deep CPU idle states via /dev/cpu_dma_latency.\n"
            "All three require write access to the relevant sysfs/dev nodes\n"
            "(typically root). Original state is restored on exit.\n"
            "\n"
            "Build with the provided Makefile (CFLAGS defaults to -O0 -g);\n"
            "the compile-time default frequency can be overridden with\n"
            "  make CFLAGS='-O0 -g -DDEFAULT_LOCK_FREQ_KHZ=1200000'\n",
            argv0, (unsigned long long)DEFAULT_LOCK_FREQ_KHZ, NPROC, NPROC - 1);
}

int main(int argc, char **argv)
{
    int iterations = 1;
    unsigned long long lock_freq_khz = DEFAULT_LOCK_FREQ_KHZ;

    int opt;
    while ((opt = getopt(argc, argv, "n:f:h")) != -1) {
        switch (opt) {
            case 'n': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (!end || *end != '\0' || v <= 0 || v > 100000) {
                    fprintf(stderr, "Invalid -n value: %s\n", optarg);
                    return EXIT_FAILURE;
                }
                iterations = (int)v;
                break;
            }
            case 'f': {
                char *end = NULL;
                unsigned long long v = strtoull(optarg, &end, 10);
                if (!end || *end != '\0' || v == 0) {
                    fprintf(stderr,
                            "Invalid -f value (expected positive integer in kHz): %s\n",
                            optarg);
                    return EXIT_FAILURE;
                }
                lock_freq_khz = v;
                break;
            }
            case 'h':
            default:
                usage(argv[0]);
                return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    /* Set up frequency / idle pinning, and ensure they are released even
     * if we exit early. Order: register cleanup first, then offline the
     * inactive cores, then pin frequency on the active ones, then block
     * deep idle states. */
    atexit(on_exit_cleanup);
    offline_other_cpus();
    pin_cpu_frequency(lock_freq_khz);
    block_cpu_idle_states();

    printf("# multi_proc_pmu: iterations=%d, NPROC=%d, lock_freq_khz=%llu%s\n",
           iterations, NPROC, lock_freq_khz,
           (lock_freq_khz == 0) ? " (auto)" : "");
    fflush(stdout);

    for (int it = 0; it < iterations; it++) {
        if (run_one_iteration(it) != 0) {
            return EXIT_FAILURE;
        }
    }

    return 0;
}
