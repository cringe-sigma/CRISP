/*
 * PR_TLB bench (ported from PolyRhythm/src/TLB_Attacks.c)
 *
 * Force TLB pressure by mmap()ing a region, touching every page,
 * mprotecting / memcpying / munmapping each page individually, and
 * finally munmapping the whole region.
 *
 * Tunables:
 *   PR_TLB_PAGES   number of pages mapped per cycle  (default 256)
 *   PR_TLB_PAGE    page size in bytes                (default 4096)
 *   PR_TLB_ITER    cycles per _main()                (default 1)
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifndef PR_TLB_PAGES
#define PR_TLB_PAGES  256
#endif
#ifndef PR_TLB_PAGE
#define PR_TLB_PAGE   4096
#endif
#ifndef PR_TLB_ITER
#define PR_TLB_ITER   1
#endif

void PR_TLB_init(void);
void PR_TLB_main(void);
int  PR_TLB_return(void);
int  main(void);

static volatile int PR_TLB_sink = 0;

void PR_TLB_init(void) { PR_TLB_sink = 0; }

void PR_TLB_main(void)
{
    size_t mmap_size = (size_t)PR_TLB_PAGE * (size_t)PR_TLB_PAGES;
    char buffer[PR_TLB_PAGE];

    for (int it = 0; it < PR_TLB_ITER; it++) {
        uint8_t *mem = (uint8_t *)mmap(NULL, mmap_size,
                                       PROT_WRITE | PROT_READ,
                                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return;
        memset(mem, 0, mmap_size);
        for (uint8_t *ptr = mem; ptr < mem + mmap_size; ptr += (size_t)PR_TLB_PAGE) {
            (void)mprotect(ptr, (size_t)PR_TLB_PAGE, PROT_READ);
            (void)memcpy(buffer, ptr, (size_t)PR_TLB_PAGE);
            (void)munmap(ptr, (size_t)PR_TLB_PAGE);
        }
        PR_TLB_sink ^= buffer[0];
    }
}

int PR_TLB_return(void) { return PR_TLB_sink; }

int main(void) { PR_TLB_init(); PR_TLB_main(); return PR_TLB_return(); }
