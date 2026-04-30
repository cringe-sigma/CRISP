/*
 * PR_CACHE bench (ported from PolyRhythm/src/Cache_Attacks.c)
 *
 * Per-thread private buffer; in each iteration sweep the buffer doing
 * cache-line strided writes.  The PolyRhythm authors found this is most
 * effective for write-bandwidth eviction.
 *
 * Tunables:
 *   PR_CACHE_KB     working set per thread, KiB    (default 1024)
 *   PR_CACHE_STRIDE stride in cache-line multiples (default 1)
 *   PR_CACHE_LINE   cache-line bytes               (default 64)
 *   PR_CACHE_ITER   inner iterations per _main()   (default 6)
 */

#include <stdint.h>
#include <stdlib.h>

#ifndef PR_CACHE_KB
#define PR_CACHE_KB      1024
#endif
#ifndef PR_CACHE_STRIDE
#define PR_CACHE_STRIDE  1
#endif
#ifndef PR_CACHE_LINE
#define PR_CACHE_LINE    64
#endif
#ifndef PR_CACHE_ITER
#define PR_CACHE_ITER    6
#endif

void PR_CACHE_init(void);
void PR_CACHE_main(void);
int  PR_CACHE_return(void);
int  main(void);

static volatile int *PR_CACHE_arr = NULL;
static size_t        PR_CACHE_n   = 0;
static volatile int  PR_CACHE_sink = 0;

void PR_CACHE_init(void)
{
    size_t bytes = (size_t)PR_CACHE_KB * 1024u;
    PR_CACHE_n = bytes / sizeof(int);
    if (!PR_CACHE_arr) PR_CACHE_arr = (int *)malloc(bytes);
    if (PR_CACHE_arr) {
        for (size_t i = 0; i < PR_CACHE_n; i++) PR_CACHE_arr[i] = (int)i;
    }
    PR_CACHE_sink = 0;
}

void PR_CACHE_main(void)
{
    if (!PR_CACHE_arr) return;
    register volatile int *a = PR_CACHE_arr;
    size_t step = ((size_t)PR_CACHE_STRIDE * (size_t)PR_CACHE_LINE) / sizeof(int);
    if (step == 0) step = 1;
    for (int it = 0; it < PR_CACHE_ITER; it++) {
        for (size_t i = 0; i < PR_CACHE_n; i += step) {
            a[i] = 0xff;
        }
    }
    PR_CACHE_sink ^= a[0];
}

int PR_CACHE_return(void) { return PR_CACHE_sink; }

int main(void) { PR_CACHE_init(); PR_CACHE_main(); return PR_CACHE_return(); }
