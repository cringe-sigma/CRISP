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
#include <signal.h>
#include <unistd.h>

#include "bench_registry.h"

/*
 * Maximum number of active cores supported by the harness (cpu0 measurement
 * + up to MAX_NPROC-1 background co-runners). The actual number of active
 * cores for a given run is g_nproc, derived from the CLI: it equals 1 +
 * (number of background bench arguments). All cpus with index >= g_nproc
 * are hot-unplugged so they cannot interfere with the measured cores.
 */
#define MAX_NPROC 8
static int g_nproc = 1;
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
 * Per-CPU saved cpufreq state for the active cores so that the
 * original governor / min / max frequency can be restored on exit.
 */
static char saved_governor[MAX_NPROC][64];
static char saved_min_freq[MAX_NPROC][32];
static char saved_max_freq[MAX_NPROC][32];
static int  governor_saved[MAX_NPROC] = {0};
static int  min_freq_saved[MAX_NPROC] = {0};
static int  max_freq_saved[MAX_NPROC] = {0};

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
 * missing on this driver -- common on intel_pstate / schedutil setups).
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
 * drivers is absent altogether -- that's fine, the kernel will accept any
 * value inside the [min, max] range).
 *
 * If `requested_khz` is 0, the function falls back to min(scaling_max_freq)
 * across all active cores, which always yields a value reachable on every
 * one of them (handy for quick runs and for asymmetric big.LITTLE SoCs).
 */
static void pin_cpu_frequency(unsigned long long requested_khz)
{
    char path[256];

    for (int cpu = 0; cpu < g_nproc; cpu++) {
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
        for (int cpu = 0; cpu < g_nproc; cpu++) {
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
        for (int cpu = 0; cpu < g_nproc; cpu++) {
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

    for (int cpu = 0; cpu < g_nproc; cpu++) {
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
            g_nproc - 1, target_khz,
            (requested_khz != 0 && requested_khz == target_khz) ? " (user-specified)" : "");
}

static void restore_cpu_frequency(void)
{
    char path[256];

    /* Restore in the safe order: widen scaling_max_freq first (back to
     * its original, possibly higher value), then restore scaling_min_freq,
     * then the governor. */
    for (int cpu = 0; cpu < g_nproc; cpu++) {
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
    for (int cpu = g_nproc; cpu < total_cpus_detected; cpu++) {
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

    if (total_cpus_detected > g_nproc) {
        fprintf(stderr,
                "[info] offlined cpu%d..cpu%d to isolate the active cores\n",
                g_nproc, total_cpus_detected - 1);
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
/* Workload registry                                                   */
/* ------------------------------------------------------------------ */

/*
 * Each TACLeBench kernel exposes the same C API:
 *   void <name>_init(void);
 *   void <name>_main(void);
 *   int  <name>_return(void);
 * The Makefile auto-discovers every kernel directory under
 * bench/bench/kernel/, links them into isolated object files (with
 * --keep-global-symbol), and emits build/bench_registry.h with a
 * BENCH_LIST(X) macro listing every available bench.
 */
#define DECLARE_BENCH(n)                  \
    extern void n##_init(void);           \
    extern void n##_main(void);           \
    extern int  n##_return(void);
BENCH_LIST(DECLARE_BENCH)
#undef DECLARE_BENCH

struct bench {
    const char *name;
    void (*init)(void);
    void (*main_fn)(void);
    int  (*ret)(void);
};

static const struct bench BENCH_TABLE[] = {
#define BENCH_ENTRY(n) { #n, n##_init, n##_main, n##_return },
    BENCH_LIST(BENCH_ENTRY)
#undef BENCH_ENTRY
};

static const int NBENCHES = (int)(sizeof(BENCH_TABLE) / sizeof(BENCH_TABLE[0]));

/*
 * Look up a bench by exact name, or by unique prefix match if no exact
 * match exists. Returns NULL on no match. If the prefix is ambiguous,
 * lists all candidates on stderr and returns NULL.
 */
static const struct bench *find_bench(const char *name)
{
    /* Pass 1: exact match wins (e.g. "fft" should not be ambiguous even if
     * other bench names happened to start with "fft"). */
    for (int i = 0; i < NBENCHES; i++) {
        if (strcmp(BENCH_TABLE[i].name, name) == 0) return &BENCH_TABLE[i];
    }

    /* Pass 2: unique prefix match. */
    size_t nlen = strlen(name);
    int match_count = 0;
    int match_idx   = -1;
    for (int i = 0; i < NBENCHES; i++) {
        if (strncmp(BENCH_TABLE[i].name, name, nlen) == 0) {
            match_idx = i;
            match_count++;
        }
    }
    if (match_count == 1) return &BENCH_TABLE[match_idx];
    if (match_count > 1) {
        fprintf(stderr, "error: prefix '%s' is ambiguous; matches:\n  ", name);
        for (int i = 0; i < NBENCHES; i++) {
            if (strncmp(BENCH_TABLE[i].name, name, nlen) == 0) {
                fprintf(stderr, "%s ", BENCH_TABLE[i].name);
            }
        }
        fprintf(stderr, "\n");
    }
    return NULL;
}

static void list_all_benches(FILE *f)
{
    fprintf(f, "available benches (%d):\n  ", NBENCHES);
    for (int i = 0; i < NBENCHES; i++) {
        fprintf(f, "%s%s", BENCH_TABLE[i].name,
                (i + 1 == NBENCHES) ? "\n" : " ");
    }
}

/*
 * Run one full bench iteration: init -> main -> return. The return value
 * is consumed via a `volatile` sink so the compiler cannot drop the call.
 */
static inline void run_bench_once(const struct bench *b)
{
    b->init();
    b->main_fn();
    volatile int sink = b->ret();
    (void)sink;
}

/* ------------------------------------------------------------------ */
/* Statistics helpers                                                  */
/* ------------------------------------------------------------------ */

static int u64_cmp(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    if (x < y) return -1;
    if (x > y) return  1;
    return 0;
}

struct stats {
    uint64_t min;
    uint64_t max;
    double   avg;
    double   median;
};

static void compute_stats(uint64_t *vals, int n, struct stats *out)
{
    qsort(vals, (size_t)n, sizeof(vals[0]), u64_cmp);
    out->min = vals[0];
    out->max = vals[n - 1];

    long double sum = 0.0L;
    for (int i = 0; i < n; i++) sum += (long double)vals[i];
    out->avg = (double)(sum / (long double)n);

    if (n % 2 == 1) {
        out->median = (double)vals[n / 2];
    } else {
        out->median = ((double)vals[n / 2 - 1] + (double)vals[n / 2]) / 2.0;
    }
}

/* ------------------------------------------------------------------ */
/* Background worker (cpu 1..NPROC-1)                                  */
/* ------------------------------------------------------------------ */

/*
 * Continuously execute the assigned bench to provide steady-state
 * interference for the measured core. The process exits when the parent
 * sends SIGTERM after core 0 finishes its sample collection.
 */
static void background_worker_main(int cpu_id, const struct bench *b)
{
    bind_to_cpu(cpu_id);

    for (;;) {
        run_bench_once(b);
    }
}

/* ------------------------------------------------------------------ */
/* Measurement process (cpu 0): N samples + statistics                 */
/* ------------------------------------------------------------------ */

/*
 * Core-0 measurement entry function:
 * 1. Bind to cpu0
 * 2. Allocate the cache-flush buffer
 * 3. Open the PMU event group (cycles / instructions / cache-misses)
 * 4. Loop `samples` times: flush caches, reset+enable, run bench,
 *    disable, read counters, record values
 * 5. Print every sample, then min/max/avg/median across samples
 */
static void measurement_main(int samples, const struct bench *b)
{
    bind_to_cpu(0);

    uint8_t *flush_buf = (uint8_t *)malloc(CACHE_FLUSH_BYTES);
    if (!flush_buf) {
        perror("malloc flush_buf");
        exit(EXIT_FAILURE);
    }
    memset(flush_buf, 0, CACHE_FLUSH_BYTES);

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

    uint64_t *s_cycles = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)samples);
    uint64_t *s_instr  = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)samples);
    uint64_t *s_cachem = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)samples);
    if (!s_cycles || !s_instr || !s_cachem) {
        perror("malloc samples");
        exit(EXIT_FAILURE);
    }

    /* Flush caches ONCE before the sampling loop, not before every sample.
     * Per-sample flushing destroys the steady-state cache contents that the
     * background co-runners on cpu1..cpu(N-1) are trying to pollute, which
     * masks LLC interference. With a single up-front flush, sample 0 starts
     * cold but samples 1..N-1 observe the natural cache evolution under
     * sustained co-runner pressure -- the regime we actually want to
     * measure. */
    flush_caches(flush_buf, CACHE_FLUSH_BYTES);

    for (int it = 0; it < samples; it++) {
        if (ioctl(fd_cycles, PERF_EVENT_IOC_RESET,  PERF_IOC_FLAG_GROUP) == -1 ||
            ioctl(fd_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
            perror("ioctl RESET/ENABLE");
            exit(EXIT_FAILURE);
        }

        run_bench_once(b);

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
            if      (rf.values[i].id == id_cycles) cycles       = rf.values[i].value;
            else if (rf.values[i].id == id_instr)  instructions = rf.values[i].value;
            else if (rf.values[i].id == id_cachem) cache_misses = rf.values[i].value;
        }

        s_cycles[it] = cycles;
        s_instr[it]  = instructions;
        s_cachem[it] = cache_misses;

        printf("[sample=%d] cpu=0 bench=%s cycles=%" PRIu64
               " instructions=%" PRIu64
               " cache-misses=%" PRIu64 "\n",
               it, b->name, cycles, instructions, cache_misses);
        fflush(stdout);
    }

    struct stats st_c, st_i, st_m;
    compute_stats(s_cycles, samples, &st_c);
    compute_stats(s_instr,  samples, &st_i);
    compute_stats(s_cachem, samples, &st_m);

    printf("\n# Statistics over %d samples on cpu0 bench=%s (background load on cpu1..cpu%d)\n",
           samples, b->name, g_nproc - 1);
    printf("# %-13s %20s %20s %20s %20s\n",
           "event", "min", "max", "avg", "median");
    printf("  %-13s %20" PRIu64 " %20" PRIu64 " %20.2f %20.2f\n",
           "cycles",       st_c.min, st_c.max, st_c.avg, st_c.median);
    printf("  %-13s %20" PRIu64 " %20" PRIu64 " %20.2f %20.2f\n",
           "instructions", st_i.min, st_i.max, st_i.avg, st_i.median);
    printf("  %-13s %20" PRIu64 " %20" PRIu64 " %20.2f %20.2f\n",
           "cache-misses", st_m.min, st_m.max, st_m.avg, st_m.median);
    fflush(stdout);

    free(s_cycles);
    free(s_instr);
    free(s_cachem);
    close(fd_cachem);
    close(fd_instr);
    close(fd_cycles);
    free(flush_buf);

    exit(EXIT_SUCCESS);
}

/* ------------------------------------------------------------------ */
/* Parent: launch background workers + measurement, then join          */
/* ------------------------------------------------------------------ */

/*
 * Spawn (g_nproc - 1) background workers on cpu1..cpu(g_nproc-1) that
 * loop their assigned bench forever, plus one measurement process on
 * cpu0 that collects `samples` PMU readings of `meas_bench`.
 * `bg_benches[i]` (i = 1..g_nproc-1) is the bench assigned to cpu i.
 * If g_nproc == 1, no background workers are spawned: cpu0 runs alone
 * while every other CPU is hot-unplugged, so cpu0 sees zero on-chip
 * interference.
 */
static int run_measurements(int samples,
                            const struct bench *meas_bench,
                            const struct bench *bg_benches[MAX_NPROC])
{
    pid_t bg_pids[MAX_NPROC];      // index 0 unused; entries [1..g_nproc-1] valid
    pid_t meas_pid;
    int n_bg = g_nproc - 1;

    for (int i = 1; i <= n_bg; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork background");
            for (int j = 1; j < i; j++) {
                kill(bg_pids[j], SIGTERM);
                waitpid(bg_pids[j], NULL, 0);
            }
            return -1;
        }
        if (pid == 0) {
            background_worker_main(i, bg_benches[i]);
            /* not reached */
        }
        bg_pids[i] = pid;
    }

    /* Let the background workers ramp up so the measured samples see a
     * steady-state interference pattern from sample #0. */
    if (n_bg > 0) usleep(200 * 1000);

    meas_pid = fork();
    if (meas_pid < 0) {
        perror("fork measurement");
        for (int j = 1; j <= n_bg; j++) {
            kill(bg_pids[j], SIGTERM);
            waitpid(bg_pids[j], NULL, 0);
        }
        return -1;
    }
    if (meas_pid == 0) {
        measurement_main(samples, meas_bench);
        /* not reached */
    }

    int meas_status = 0;
    waitpid(meas_pid, &meas_status, 0);

    /* Tear down background workers. */
    for (int j = 1; j <= n_bg; j++) {
        kill(bg_pids[j], SIGTERM);
    }
    for (int j = 1; j <= n_bg; j++) {
        waitpid(bg_pids[j], NULL, 0);
    }

    if (WIFEXITED(meas_status) && WEXITSTATUS(meas_status) == 0) return 0;
    return -1;
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-n N] [-f KHZ] BENCH [BG_BENCH ...]\n"
            "  -n N      Number of PMU samples to collect on cpu0 (default: 1)\n"
            "  -f KHZ    Lock active cores to this exact frequency, in kHz.\n"
            "            Must lie within [cpuinfo_min_freq, cpuinfo_max_freq];\n"
            "            a warning is printed if the value is not listed in\n"
            "            scaling_available_frequencies. Default: %llu\n"
            "            (0 = auto, picks min(scaling_max_freq) across active cores).\n"
            "  -l        List all available benches and exit.\n"
            "\n"
            "Positional arguments:\n"
            "  BENCH         Bench name to run on cpu0 (the *measured* workload).\n"
            "  BG_BENCH ...  Up to %d additional bench names, assigned in order to\n"
            "                cpu1, cpu2, ... The number of *active* cores equals\n"
            "                1 + (number of BG_BENCH arguments). Extra names\n"
            "                beyond cpu%d are rejected.\n"
            "\n"
            "cpu0 measures (cycles / instructions / cache-misses) over BENCH for\n"
            "N samples; the active background cores run their assigned bench in\n"
            "an infinite loop to provide steady-state interference. After all N\n"
            "samples are collected, min/max/avg/median are printed.\n"
            "Caches are flushed before every measured sample.\n"
            "\n"
            "On startup the program also:\n"
            "  * offlines every CPU outside the active set (writes 0 to the\n"
            "    corresponding /sys/devices/system/cpu/cpuN/online), so cores\n"
            "    that are *not* running a bench cannot interfere with the ones\n"
            "    that are;\n"
            "  * locks the active cores to the selected frequency by setting\n"
            "    the 'performance' governor and pinning\n"
            "    scaling_min_freq == scaling_max_freq == target;\n"
            "  * blocks deep CPU idle states via /dev/cpu_dma_latency.\n"
            "All three require write access to the relevant sysfs/dev nodes\n"
            "(typically root). Original state is restored on exit.\n",
            argv0, (unsigned long long)DEFAULT_LOCK_FREQ_KHZ,
            MAX_NPROC - 1, MAX_NPROC - 1);
}

int main(int argc, char **argv)
{
    int samples = 1;
    unsigned long long lock_freq_khz = DEFAULT_LOCK_FREQ_KHZ;

    int opt;
    while ((opt = getopt(argc, argv, "n:f:lh")) != -1) {
        switch (opt) {
            case 'n': {
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (!end || *end != '\0' || v <= 0 || v > 100000) {
                    fprintf(stderr, "Invalid -n value: %s\n", optarg);
                    return EXIT_FAILURE;
                }
                samples = (int)v;
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
            case 'l':
                list_all_benches(stdout);
                return EXIT_SUCCESS;
            case 'h':
            default:
                usage(argv[0]);
                return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "error: missing BENCH (measured workload) argument.\n\n");
        usage(argv[0]);
        list_all_benches(stderr);
        return EXIT_FAILURE;
    }

    const struct bench *meas_bench = find_bench(argv[optind]);
    if (!meas_bench) {
        fprintf(stderr, "error: unknown bench '%s'.\n", argv[optind]);
        list_all_benches(stderr);
        return EXIT_FAILURE;
    }

    /* Active core count = 1 (cpu0) + number of background bench arguments.
     * Capped at MAX_NPROC. CPUs >= g_nproc are hot-unplugged so they cannot
     * interfere with the active set. */
    int n_bg_args = argc - optind - 1;
    if (n_bg_args < 0) n_bg_args = 0;
    if (n_bg_args > MAX_NPROC - 1) {
        fprintf(stderr,
                "error: too many background bench arguments (%d); max is %d.\n",
                n_bg_args, MAX_NPROC - 1);
        return EXIT_FAILURE;
    }
    g_nproc = 1 + n_bg_args;

    /* bg_benches[0] is unused; bg_benches[i] is the bench assigned to cpu i
     * for i = 1..g_nproc-1. */
    const struct bench *bg_benches[MAX_NPROC] = {0};
    for (int i = 1; i < g_nproc; i++) {
        const struct bench *bb = find_bench(argv[optind + i]);
        if (!bb) {
            fprintf(stderr,
                    "error: unknown background bench '%s' (slot cpu%d).\n",
                    argv[optind + i], i);
            list_all_benches(stderr);
            return EXIT_FAILURE;
        }
        bg_benches[i] = bb;
    }

    /* Set up frequency / idle pinning, and ensure they are released even
     * if we exit early. Order: register cleanup first, then offline the
     * inactive cores, then pin frequency on the active ones, then block
     * deep idle states. */
    atexit(on_exit_cleanup);
    offline_other_cpus();
    pin_cpu_frequency(lock_freq_khz);
    block_cpu_idle_states();

    printf("# multi_proc_pmu: samples=%d, active_cores=%d, lock_freq_khz=%llu%s\n",
           samples, g_nproc, lock_freq_khz,
           (lock_freq_khz == 0) ? " (auto)" : "");
    printf("# cpu0=%s (measured)", meas_bench->name);
    if (g_nproc == 1) {
        printf("  (solo: all other cpus offlined)");
    } else {
        for (int i = 1; i < g_nproc; i++) {
            printf("  cpu%d=%s", i, bg_benches[i]->name);
        }
    }
    printf("\n");
    fflush(stdout);

    if (run_measurements(samples, meas_bench, bg_benches) != 0) {
        return EXIT_FAILURE;
    }

    return 0;
}
