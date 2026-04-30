/*
 * PR_ROWBUF bench (ported from PolyRhythm/src/Row_Buffer_Attack.c)
 *
 * DRAM row-buffer stress: two large arrays a[] and b[] plus an index
 * array.  Two access phases per iteration: a strided copy and a full
 * walk through the indirection table.  Either random or sequential
 * indices are precomputed.
 *
 * Tunables:
 *   PR_ROWBUF_KB        size of each array in KiB         (default 1024)
 *   PR_ROWBUF_STRIDE    stride for sequential pattern     (default 16)
 *   PR_ROWBUF_RANDOM    1=random index, 0=sequential      (default 1)
 *   PR_ROWBUF_ITER      iterations per _main()            (default 1)
 */

#include <stdint.h>
#include <stdlib.h>

#ifndef PR_ROWBUF_KB
#define PR_ROWBUF_KB      1024
#endif
#ifndef PR_ROWBUF_STRIDE
#define PR_ROWBUF_STRIDE  16
#endif
#ifndef PR_ROWBUF_RANDOM
#define PR_ROWBUF_RANDOM  1
#endif
#ifndef PR_ROWBUF_ITER
#define PR_ROWBUF_ITER    1
#endif

void PR_ROWBUF_init(void);
void PR_ROWBUF_main(void);
int  PR_ROWBUF_return(void);
int  main(void);

static volatile int *PR_RB_a = NULL;
static volatile int *PR_RB_b = NULL;
static volatile int *PR_RB_idx = NULL;
static size_t        PR_RB_n = 0;
static uint32_t      PR_RB_rng = 0xBADC0FFEu;
static volatile int  PR_RB_sink = 0;

static inline uint32_t PR_RB_xorshift(void)
{
    uint32_t x = PR_RB_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    PR_RB_rng = x;
    return x;
}

void PR_ROWBUF_init(void)
{
    size_t bytes = (size_t)PR_ROWBUF_KB * 1024u;
    PR_RB_n = bytes / sizeof(int);
    if (!PR_RB_a)   PR_RB_a   = (int *)malloc(bytes);
    if (!PR_RB_b)   PR_RB_b   = (int *)malloc(bytes);
    if (!PR_RB_idx) PR_RB_idx = (int *)malloc(bytes);
    PR_RB_rng = 0xBADC0FFEu;
    if (PR_RB_idx) {
#if PR_ROWBUF_RANDOM
        for (size_t j = 0; j < PR_RB_n; j++)
            PR_RB_idx[j] = (int)(PR_RB_xorshift() % (uint32_t)PR_RB_n);
#else
        size_t t = 0;
        for (int k = 0; k < PR_ROWBUF_STRIDE; k++) {
            for (size_t j = (size_t)k; j + (size_t)PR_ROWBUF_STRIDE < PR_RB_n;
                 j += (size_t)PR_ROWBUF_STRIDE) {
                if (t < PR_RB_n) PR_RB_idx[t++] = (int)j;
            }
        }
        for (; t < PR_RB_n; t++) PR_RB_idx[t] = 0;
#endif
    }
    if (PR_RB_a && PR_RB_b) {
        for (size_t j = 0; j < PR_RB_n; j++) {
            PR_RB_a[j] = (int)PR_RB_xorshift();
            PR_RB_b[j] = (int)PR_RB_xorshift();
        }
    }
    PR_RB_sink = 0;
}

void PR_ROWBUF_main(void)
{
    if (!PR_RB_a || !PR_RB_b || !PR_RB_idx) return;
    register volatile int *a = PR_RB_a;
    register volatile int *b = PR_RB_b;
    register volatile int *idx = PR_RB_idx;
    size_t n = PR_RB_n;
    for (int it = 0; it < PR_ROWBUF_ITER; it++) {
        unsigned jump = 80u + (PR_RB_xorshift() % 80u);   /* 80..159 */
        for (size_t i = 0; i + jump < n; i += jump) {
            int t = idx[i];
            a[t] = b[t];
        }
        for (size_t j = 0; j < n; j++) {
            int t = idx[j];
            b[t] = a[t];
        }
    }
    PR_RB_sink ^= b[0];
}

int PR_ROWBUF_return(void) { return PR_RB_sink; }

int main(void) { PR_ROWBUF_init(); PR_ROWBUF_main(); return PR_ROWBUF_return(); }
