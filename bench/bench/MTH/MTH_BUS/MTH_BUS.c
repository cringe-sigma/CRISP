/*
 * MTH_BUS bench (ported from multicore-test-harness/src/bus_set/bus.c)
 *
 * Two large memory buffers: memset both, then copy mem1->mem2 with an
 * additive transform.  Designed to keep MEM<->CPU<->MEM traffic high.
 *
 * Tunables:
 *   MTH_BUS_SIZE_MB    total buffer size in MiB       (default 4)
 *   MTH_BUS_ITER       iterations per _main()         (default 1)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MTH_BUS_SIZE_MB
#define MTH_BUS_SIZE_MB  4
#endif
#ifndef MTH_BUS_ITER
#define MTH_BUS_ITER     1
#endif

void MTH_BUS_init(void);
void MTH_BUS_main(void);
int  MTH_BUS_return(void);
int  main(void);

static volatile int32_t *MTH_BUS_m1 = NULL;
static volatile int32_t *MTH_BUS_m2 = NULL;
static size_t            MTH_BUS_n  = 0;
static uint32_t          MTH_BUS_rng = 0xCAFEBABEu;
static volatile int32_t  MTH_BUS_sink = 0;

static inline uint32_t MTH_BUS_xorshift(void)
{
    uint32_t x = MTH_BUS_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    MTH_BUS_rng = x;
    return x;
}

void MTH_BUS_init(void)
{
    size_t bytes = (size_t)MTH_BUS_SIZE_MB * 1024u * 1024u;
    MTH_BUS_n = bytes / sizeof(int32_t);
    if (!MTH_BUS_m1) MTH_BUS_m1 = (int32_t *)malloc(bytes);
    if (!MTH_BUS_m2) MTH_BUS_m2 = (int32_t *)malloc(bytes);
    if (MTH_BUS_m1) memset((void *)MTH_BUS_m1, 0xA5, bytes);
    if (MTH_BUS_m2) memset((void *)MTH_BUS_m2, 0x5A, bytes);
    MTH_BUS_rng = 0xCAFEBABEu;
    MTH_BUS_sink = 0;
}

void MTH_BUS_main(void)
{
    if (!MTH_BUS_m1 || !MTH_BUS_m2) return;
    for (int it = 0; it < MTH_BUS_ITER; it++) {
        int v1 = (int)MTH_BUS_xorshift();
        int v2 = (int)MTH_BUS_xorshift();
        memset((void *)MTH_BUS_m1, v1 & 0xff, MTH_BUS_n * sizeof(int32_t));
        memset((void *)MTH_BUS_m2, v2 & 0xff, MTH_BUS_n * sizeof(int32_t));
        int32_t r = (int32_t)MTH_BUS_xorshift();
        for (size_t i = 0; i < MTH_BUS_n; i++) {
            MTH_BUS_m2[i] = MTH_BUS_m1[i] + r;
        }
    }
    MTH_BUS_sink ^= MTH_BUS_m2[0];
}

int MTH_BUS_return(void) { return MTH_BUS_sink; }

int main(void) { MTH_BUS_init(); MTH_BUS_main(); return MTH_BUS_return(); }
