/*
 * MTH_CACHE bench (ported from multicore-test-harness/src/cache_set/cache_stress.c)
 *
 * Walk through a region of memory the size of the L3 cache, striding at
 * the size of a cache line, doing read accumulations.
 *
 * Tunables:
 *   MTH_CACHE_SIZE_KB   working set in KiB           (default 1024 = 1 MB)
 *   MTH_CACHE_LINE      stride in bytes              (default 64)
 *   MTH_CACHE_ITER      iterations per _main()       (default 4)
 */

#include <stdint.h>
#include <stdlib.h>

#ifndef MTH_CACHE_SIZE_KB
#define MTH_CACHE_SIZE_KB  1024
#endif
#ifndef MTH_CACHE_LINE
#define MTH_CACHE_LINE     64
#endif
#ifndef MTH_CACHE_ITER
#define MTH_CACHE_ITER     4
#endif

void MTH_CACHE_init(void);
void MTH_CACHE_main(void);
int  MTH_CACHE_return(void);
int  main(void);

static volatile int *MTH_CACHE_arr = NULL;
static size_t        MTH_CACHE_n   = 0;
static volatile unsigned MTH_CACHE_sum = 0;

void MTH_CACHE_init(void)
{
    size_t bytes = (size_t)MTH_CACHE_SIZE_KB * 1024u;
    MTH_CACHE_n = bytes / sizeof(int);
    if (MTH_CACHE_arr == NULL)
        MTH_CACHE_arr = (int *)malloc(bytes);
    if (MTH_CACHE_arr) {
        for (size_t i = 0; i < MTH_CACHE_n; i++) MTH_CACHE_arr[i] = (int)i;
    }
    MTH_CACHE_sum = 0;
}

void MTH_CACHE_main(void)
{
    if (!MTH_CACHE_arr) return;
    register volatile int *a = MTH_CACHE_arr;
    register unsigned sum = MTH_CACHE_sum;
    size_t step = (size_t)MTH_CACHE_LINE / sizeof(int);
    if (step == 0) step = 1;
    for (int it = 0; it < MTH_CACHE_ITER; it++) {
        for (size_t i = 0; i < MTH_CACHE_n; i += step) {
            sum += (unsigned)a[i];
        }
    }
    MTH_CACHE_sum = sum;
}

int MTH_CACHE_return(void) { return (int)MTH_CACHE_sum; }

int main(void) { MTH_CACHE_init(); MTH_CACHE_main(); return MTH_CACHE_return(); }
