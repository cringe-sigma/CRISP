/*
 * MTH_MEM bench (ported from multicore-test-harness/src/mem_thrashing_set/mem_thrashing.c)
 *
 * Allocate a large region; in each iteration pick a random page-aligned
 * chunk and memset it with a random fill byte.
 *
 * Tunables:
 *   MTH_MEM_SIZE_MB   buffer size in MiB         (default 32)
 *   MTH_MEM_PAGE      chunk size in bytes        (default 4096)
 *   MTH_MEM_ITER      ops per _main()            (default 32)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MTH_MEM_SIZE_MB
#define MTH_MEM_SIZE_MB  32
#endif
#ifndef MTH_MEM_PAGE
#define MTH_MEM_PAGE     4096
#endif
#ifndef MTH_MEM_ITER
#define MTH_MEM_ITER     32
#endif

void MTH_MEM_init(void);
void MTH_MEM_main(void);
int  MTH_MEM_return(void);
int  main(void);

static volatile uint8_t *MTH_MEM_buf = NULL;
static size_t            MTH_MEM_chunks = 0;
static uint32_t          MTH_MEM_rng = 0xDEADD00Du;
static volatile int      MTH_MEM_sink = 0;

static inline uint32_t MTH_MEM_xorshift(void)
{
    uint32_t x = MTH_MEM_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    MTH_MEM_rng = x;
    return x;
}

void MTH_MEM_init(void)
{
    size_t bytes = (size_t)MTH_MEM_SIZE_MB * 1024u * 1024u;
    MTH_MEM_chunks = bytes / (size_t)MTH_MEM_PAGE;
    if (!MTH_MEM_buf) MTH_MEM_buf = (uint8_t *)malloc(bytes);
    if (MTH_MEM_buf) memset((void *)MTH_MEM_buf, 0, bytes);
    MTH_MEM_rng = 0xDEADD00Du;
    MTH_MEM_sink = 0;
}

void MTH_MEM_main(void)
{
    if (!MTH_MEM_buf || MTH_MEM_chunks == 0) return;
    for (int it = 0; it < MTH_MEM_ITER; it++) {
        size_t chunk = MTH_MEM_xorshift() % MTH_MEM_chunks;
        size_t offset = chunk * (size_t)MTH_MEM_PAGE;
        int    fill   = (int)(MTH_MEM_xorshift() & 0xff);
        memset((void *)(MTH_MEM_buf + offset), fill, (size_t)MTH_MEM_PAGE);
    }
    MTH_MEM_sink ^= MTH_MEM_buf[0];
}

int MTH_MEM_return(void) { return MTH_MEM_sink; }

int main(void) { MTH_MEM_init(); MTH_MEM_main(); return MTH_MEM_return(); }
