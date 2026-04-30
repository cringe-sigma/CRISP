/*
 * MTH_POINTER bench (ported from multicore-test-harness/src/pointer_chasing/pointer_chasing.c)
 *
 * Builds a circular linked list of cache-line-sized nodes connected with
 * STRIDE elements, then chases pointers ITER times.  Portable C version
 * (the original used inline arm/x86 asm).
 *
 * Tunables:
 *   MTH_POINTER_ELEMENTS   number of nodes               (default 1<<18 = 262144)
 *   MTH_POINTER_STRIDE     stride between nodes          (default 1000)
 *   MTH_POINTER_ITER       chase steps per _main()       (default 1<<20)
 *   MTH_POINTER_LINE       cache-line size in bytes      (default 64)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MTH_POINTER_ELEMENTS
#define MTH_POINTER_ELEMENTS  (1u << 18)
#endif
#ifndef MTH_POINTER_STRIDE
#define MTH_POINTER_STRIDE    1000u
#endif
#ifndef MTH_POINTER_ITER
#define MTH_POINTER_ITER      (1u << 20)
#endif
#ifndef MTH_POINTER_LINE
#define MTH_POINTER_LINE      64
#endif

void MTH_POINTER_init(void);
void MTH_POINTER_main(void);
int  MTH_POINTER_return(void);
int  main(void);

#define MTH_PTR_PAD ( MTH_POINTER_LINE - (int)sizeof(void *) )

struct MTH_PTR_line {
    struct MTH_PTR_line *next;
    uint8_t pad[MTH_PTR_PAD > 0 ? MTH_PTR_PAD : 1];
};

static struct MTH_PTR_line *MTH_PTR_chunk = NULL;
static volatile uintptr_t   MTH_PTR_sink  = 0;

void MTH_POINTER_init(void)
{
    if (MTH_PTR_chunk == NULL) {
        MTH_PTR_chunk = (struct MTH_PTR_line *)
            calloc((size_t)MTH_POINTER_ELEMENTS, sizeof(struct MTH_PTR_line));
    }
    if (MTH_PTR_chunk) {
        for (size_t j = 0; j < (size_t)MTH_POINTER_ELEMENTS; j++) {
            size_t k = (j + (size_t)MTH_POINTER_STRIDE) % (size_t)MTH_POINTER_ELEMENTS;
            MTH_PTR_chunk[j].next = &MTH_PTR_chunk[k];
        }
    }
    MTH_PTR_sink = 0;
}

void MTH_POINTER_main(void)
{
    if (!MTH_PTR_chunk) return;
    register struct MTH_PTR_line *p = MTH_PTR_chunk;
    for (unsigned i = 0; i < (unsigned)MTH_POINTER_ITER; i++) {
        p = p->next;
    }
    MTH_PTR_sink ^= (uintptr_t)p;
}

int MTH_POINTER_return(void) { return (int)(MTH_PTR_sink & 0x7fffffff); }

int main(void) { MTH_POINTER_init(); MTH_POINTER_main(); return MTH_POINTER_return(); }
