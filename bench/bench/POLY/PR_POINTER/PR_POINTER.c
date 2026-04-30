/*
 * PR_POINTER bench (ported from PolyRhythm/src/Pointer_Chasing.c)
 *
 * Cache-line-padded linked list, traversed with portable C.  PolyRhythm's
 * version used arch-specific inline asm; we deliberately let the compiler
 * generate a load-use chain on the `next` pointer.
 *
 * Tunables:
 *   PR_POINTER_ELEMENTS   list length              (default 1<<18)
 *   PR_POINTER_STRIDE     stride                   (default 1000)
 *   PR_POINTER_ITER       chase steps per _main()  (default 1<<20)
 *   PR_POINTER_LINE       cache-line bytes         (default 64)
 */

#include <stdint.h>
#include <stdlib.h>

#ifndef PR_POINTER_ELEMENTS
#define PR_POINTER_ELEMENTS  (1u << 18)
#endif
#ifndef PR_POINTER_STRIDE
#define PR_POINTER_STRIDE    1000u
#endif
#ifndef PR_POINTER_ITER
#define PR_POINTER_ITER      (1u << 20)
#endif
#ifndef PR_POINTER_LINE
#define PR_POINTER_LINE      64
#endif

void PR_POINTER_init(void);
void PR_POINTER_main(void);
int  PR_POINTER_return(void);
int  main(void);

#define PR_PCH_PAD ( PR_POINTER_LINE - (int)sizeof(void *) )

struct PR_pch_line {
    struct PR_pch_line *next;
    uint8_t pad[PR_PCH_PAD > 0 ? PR_PCH_PAD : 1];
};

static struct PR_pch_line *PR_PCH_chunk = NULL;
static volatile uintptr_t  PR_PCH_sink  = 0;

void PR_POINTER_init(void)
{
    if (!PR_PCH_chunk) {
        PR_PCH_chunk = (struct PR_pch_line *)
            calloc((size_t)PR_POINTER_ELEMENTS, sizeof(struct PR_pch_line));
    }
    if (PR_PCH_chunk) {
        for (size_t j = 0; j < (size_t)PR_POINTER_ELEMENTS; j++) {
            size_t k = (j + (size_t)PR_POINTER_STRIDE) % (size_t)PR_POINTER_ELEMENTS;
            PR_PCH_chunk[j].next = &PR_PCH_chunk[k];
        }
    }
    PR_PCH_sink = 0;
}

void PR_POINTER_main(void)
{
    if (!PR_PCH_chunk) return;
    register struct PR_pch_line *p = PR_PCH_chunk;
    for (unsigned i = 0; i < (unsigned)PR_POINTER_ITER; i++) p = p->next;
    PR_PCH_sink ^= (uintptr_t)p;
}

int PR_POINTER_return(void) { return (int)(PR_PCH_sink & 0x7fffffff); }

int main(void) { PR_POINTER_init(); PR_POINTER_main(); return PR_POINTER_return(); }
