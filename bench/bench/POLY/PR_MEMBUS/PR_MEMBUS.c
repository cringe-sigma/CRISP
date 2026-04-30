/*
 * PR_MEMBUS bench (ported from PolyRhythm/src/Attacks.c -- stress_memory_bus_contention)
 *
 * Two virtual mappings of the SAME backing file are created with mmap;
 * the inner loop alternates writes (or reads) to vdata0 and vdata1.
 * Because both mappings refer to the same physical page, every store
 * forces a coherency / writeback transaction on the memory bus -- the
 * key novelty over PR_CACHE/MTH_BUS.
 *
 * Tunables:
 *   PR_MEMBUS_KB     mapped region size in KiB           (default 64)
 *   PR_MEMBUS_INNER  inner loop unrolled stores          (default 1024)
 *   PR_MEMBUS_ITER   outer iterations per _main()        (default 8)
 *   PR_MEMBUS_WRITE  1=write pattern, 0=read pattern     (default 1)
 *   PR_MEMBUS_DIR    backing directory                   (default "/tmp")
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PR_MEMBUS_KB
#define PR_MEMBUS_KB     64
#endif
#ifndef PR_MEMBUS_INNER
#define PR_MEMBUS_INNER  1024
#endif
#ifndef PR_MEMBUS_ITER
#define PR_MEMBUS_ITER   8
#endif
#ifndef PR_MEMBUS_WRITE
#define PR_MEMBUS_WRITE  1
#endif
#ifndef PR_MEMBUS_DIR
#define PR_MEMBUS_DIR    "/tmp"
#endif

void PR_MEMBUS_init(void);
void PR_MEMBUS_main(void);
int  PR_MEMBUS_return(void);
int  main(void);

static int       PR_MB_fd = -1;
static char      PR_MB_path[256];
static uint64_t *PR_MB_v0 = NULL;
static uint64_t *PR_MB_v1 = NULL;
static size_t    PR_MB_bytes = 0;
static volatile uint64_t PR_MB_sink = 0;
static uint32_t  PR_MB_rng = 0xB0BAFE77u;

static inline uint32_t PR_MB_xorshift(void)
{
    uint32_t x = PR_MB_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    PR_MB_rng = x;
    return x;
}

void PR_MEMBUS_init(void)
{
    PR_MB_bytes = (size_t)PR_MEMBUS_KB * 1024u;
    PR_MB_rng   = 0xB0BAFE77u;
    PR_MB_sink  = 0;
    if (PR_MB_fd < 0) {
        snprintf(PR_MB_path, sizeof(PR_MB_path),
                 "%s/pr_membus_%d_%u.tmp",
                 PR_MEMBUS_DIR, (int)getpid(),
                 (unsigned)PR_MB_xorshift());
        PR_MB_fd = open(PR_MB_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (PR_MB_fd < 0) return;
        if (ftruncate(PR_MB_fd, (off_t)PR_MB_bytes) != 0) {
            close(PR_MB_fd); PR_MB_fd = -1; return;
        }
        PR_MB_v0 = (uint64_t *)mmap(NULL, PR_MB_bytes,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED, PR_MB_fd, 0);
        PR_MB_v1 = (uint64_t *)mmap(NULL, PR_MB_bytes,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED, PR_MB_fd, 0);
        if (PR_MB_v0 == MAP_FAILED) PR_MB_v0 = NULL;
        if (PR_MB_v1 == MAP_FAILED) PR_MB_v1 = NULL;
    }
}

void PR_MEMBUS_main(void)
{
    if (!PR_MB_v0 || !PR_MB_v1) return;
    volatile uint64_t *v0 = PR_MB_v0;
    volatile uint64_t *v1 = PR_MB_v1;
    for (int it = 0; it < PR_MEMBUS_ITER; it++) {
        for (int i = 0; i < PR_MEMBUS_INNER; i++) {
#if PR_MEMBUS_WRITE
            v0[0] = (uint64_t)i; v1[0] = (uint64_t)i;
            v0[1] = (uint64_t)i; v1[1] = (uint64_t)i;
            v0[2] = (uint64_t)i; v1[2] = (uint64_t)i;
            v0[3] = (uint64_t)i; v1[3] = (uint64_t)i;
            v0[4] = (uint64_t)i; v1[4] = (uint64_t)i;
            v0[5] = (uint64_t)i; v1[5] = (uint64_t)i;
            v0[6] = (uint64_t)i; v1[6] = (uint64_t)i;
            v0[7] = (uint64_t)i; v1[7] = (uint64_t)i;
#else
            PR_MB_sink ^= v0[0] ^ v1[0]
                       ^ v0[1] ^ v1[1]
                       ^ v0[2] ^ v1[2]
                       ^ v0[3] ^ v1[3]
                       ^ v0[4] ^ v1[4]
                       ^ v0[5] ^ v1[5]
                       ^ v0[6] ^ v1[6]
                       ^ v0[7] ^ v1[7];
#endif
        }
    }
    PR_MB_sink ^= v0[0];
}

int PR_MEMBUS_return(void)
{
    if (PR_MB_v0) { munmap(PR_MB_v0, PR_MB_bytes); PR_MB_v0 = NULL; }
    if (PR_MB_v1) { munmap(PR_MB_v1, PR_MB_bytes); PR_MB_v1 = NULL; }
    if (PR_MB_fd >= 0) { close(PR_MB_fd); PR_MB_fd = -1; }
    if (PR_MB_path[0]) { remove(PR_MB_path); PR_MB_path[0] = 0; }
    return (int)(PR_MB_sink & 0x7fffffff);
}

int main(void) { PR_MEMBUS_init(); PR_MEMBUS_main(); return PR_MEMBUS_return(); }
