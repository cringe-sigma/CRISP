#define _GNU_SOURCE

#include <asm/unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <sched.h>
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
 * Parameters:
 *   config = PERF_COUNT_HW_CPU_CYCLES / PERF_COUNT_HW_INSTRUCTIONS / etc.
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

/*
 * This is the "code under measurement".
 * You can replace it with whatever function or logic you actually want to profile.
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

/*
 * Child process entry function:
 * 1. Bind to a CPU core
 * 2. Open the PMU events
 * 3. Wait for the start signal from the parent
 * 4. reset + enable
 * 5. Execute the target code region
 * 6. disable
 * 7. Read the results and print them
 */
static void child_main(int cpu_id, int start_fd)
{
    // Step 1: pin this child process to the specified CPU
    bind_to_cpu(cpu_id);

    // Step 2: create a group of PMU events.
    // Leader: cycles
    int fd_cycles = open_leader_event(PERF_COUNT_HW_CPU_CYCLES);

    // Member: instructions
    int fd_instr = open_member_event(fd_cycles, PERF_COUNT_HW_INSTRUCTIONS);

    // Member: cache-misses
    int fd_cachem = open_member_event(fd_cycles, PERF_COUNT_HW_CACHE_MISSES);

    // To know which value in a group read corresponds to which event,
    // fetch the unique id of each fd here.
    uint64_t id_cycles = 0;
    uint64_t id_instr = 0;
    uint64_t id_cachem = 0;

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

    /*
     * Step 3: wait for the start signal from the parent process.
     * The parent writes 1 byte through the pipe; only after reading it
     * does the child actually begin the measured work.
     */
    char ch;
    if (read(start_fd, &ch, 1) != 1) {
        perror("read start signal");
        exit(EXIT_FAILURE);
    }

    // Step 4: reset the entire group's counters before starting
    if (ioctl(fd_cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
        perror("ioctl RESET");
        exit(EXIT_FAILURE);
    }

    // Enable counting on the whole group at once
    if (ioctl(fd_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
        perror("ioctl ENABLE");
        exit(EXIT_FAILURE);
    }

    // Step 5: run the actual code region under measurement
    target_work(cpu_id);

    // Step 6: stop counting on the whole group
    if (ioctl(fd_cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
        perror("ioctl DISABLE");
        exit(EXIT_FAILURE);
    }

    /*
     * Step 7: read the PMU results.
     * Note: we read from the group leader (fd_cycles) once, which returns
     * the values of all events in the group.
     */
    struct read_format rf;
    memset(&rf, 0, sizeof(rf));

    ssize_t n = read(fd_cycles, &rf, sizeof(rf));
    if (n < 0) {
        perror("read perf group");
        exit(EXIT_FAILURE);
    }

    uint64_t cycles = 0;
    uint64_t instructions = 0;
    uint64_t cache_misses = 0;

    // Iterate over the events returned by read() and map each id back to its event
    for (uint64_t i = 0; i < rf.nr; i++) {
        if (rf.values[i].id == id_cycles) {
            cycles = rf.values[i].value;
        } else if (rf.values[i].id == id_instr) {
            instructions = rf.values[i].value;
        } else if (rf.values[i].id == id_cachem) {
            cache_misses = rf.values[i].value;
        }
    }

    // Print the per-process / per-core measurement results
    printf("[child] pid=%d cpu=%d cycles=%" PRIu64
           " instructions=%" PRIu64
           " cache-misses=%" PRIu64 "\n",
           getpid(), cpu_id, cycles, instructions, cache_misses);

    close(fd_cachem);
    close(fd_instr);
    close(fd_cycles);
    close(start_fd);

    exit(EXIT_SUCCESS);
}

int main(void)
{
    pid_t pids[NPROC];
    int start_pipe[NPROC][2];

    /*
     * Create a pipe for each child process:
     *   - the parent sends the start signal via write()
     *   - the child waits for the start signal via read()
     */
    for (int i = 0; i < NPROC; i++) {
        if (pipe(start_pipe[i]) < 0) {
            perror("pipe");
            return EXIT_FAILURE;
        }
    }

    // Spawn 4 child processes
    for (int i = 0; i < NPROC; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            // Child: close the pipe ends it does not use
            close(start_pipe[i][1]); // child does not write

            // The other pipes belong to other children; close them all
            for (int j = 0; j < NPROC; j++) {
                if (j != i) {
                    close(start_pipe[j][0]);
                    close(start_pipe[j][1]);
                }
            }

            // The i-th child binds to CPU i
            child_main(i, start_pipe[i][0]);
        } else {
            // Parent: record the child's pid
            pids[i] = pid;

            // Parent does not need the read end; keep only the write end
            // so it can later send the start signal to the child
            close(start_pipe[i][0]);
        }
    }

    /*
     * Wait a little to make sure all children have finished pinning to
     * their cores and initializing the PMU before the parent fires the
     * start signal.
     */
    sleep(1);

    /*
     * Broadcast the start signal to all children.
     * This does not guarantee a strictly identical CPU-cycle start,
     * but it is good enough for typical parallel measurement scenarios.
     */
    for (int i = 0; i < NPROC; i++) {
        if (write(start_pipe[i][1], "S", 1) != 1) {
            perror("write start signal");
        }
        close(start_pipe[i][1]);
    }

    // Wait for all child processes to finish
    for (int i = 0; i < NPROC; i++) {
        waitpid(pids[i], NULL, 0);
    }

    return 0;
}
