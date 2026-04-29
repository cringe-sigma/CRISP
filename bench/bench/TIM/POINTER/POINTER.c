/*
 * POINTER bench (extracted from slaver_00_rpmsg-ping.c)
 *
 * Pointer-chasing workload. Builds a circular linked list of `ELEMENTS`
 * cache-line-aligned nodes connected with a fixed `STRIDE`. Each loop step
 * either follows the next pointer (load) or writes a data field (store);
 * the load fraction is controlled by `LOAD_RATIO` (out of 100).
 *
 * The original slaver_00_rpmsg-ping.c version used inline ARMv8 assembly.
 * For portability across the harness build host this version uses plain
 * C that the compiler still translates to a tight load-load chain.
 *
 * Tunable parameters (override at compile time with -D<NAME>=<value>):
 *   POINTER_ELEMENTS    number of list nodes                  (default 4096)
 *   POINTER_STRIDE      stride between consecutive nodes      (default 17)
 *   POINTER_LOAD_RATIO  percent of operations that are loads  (default 80)
 *   POINTER_ITER        total operations per _main()          (default 8192)
 *   POINTER_CACHE_LINE  cache line size in bytes              (default 64)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef POINTER_ELEMENTS
#define POINTER_ELEMENTS    4096u
#endif
#ifndef POINTER_STRIDE
#define POINTER_STRIDE      17u
#endif
#ifndef POINTER_LOAD_RATIO
#define POINTER_LOAD_RATIO  80
#endif
#ifndef POINTER_ITER
#define POINTER_ITER        8192
#endif
#ifndef POINTER_CACHE_LINE
#define POINTER_CACHE_LINE  64
#endif

void POINTER_init( void );
void POINTER_main( void );
int  POINTER_return( void );
int  main( void );

#define POINTER_PAD ( POINTER_CACHE_LINE - sizeof(void *) - sizeof(uint32_t) )

struct POINTER_line {
    struct POINTER_line *next;
    uint32_t             data;
    uint8_t              pad[ POINTER_PAD > 0 ? POINTER_PAD : 1 ];
};

static struct POINTER_line *POINTER_chunk = NULL;
static volatile uintptr_t   POINTER_sink  = 0;

void POINTER_init( void )
{
    if ( POINTER_chunk == NULL ) {
        POINTER_chunk = (struct POINTER_line *)calloc(
            POINTER_ELEMENTS, sizeof( struct POINTER_line ) );
    }
    if ( POINTER_chunk == NULL ) return;

    for ( unsigned long j = 0; j < POINTER_ELEMENTS; j++ ) {
        POINTER_chunk[ j ].next =
            &POINTER_chunk[ ( j + POINTER_STRIDE ) % POINTER_ELEMENTS ];
        POINTER_chunk[ j ].data = (uint32_t)j;
    }
    POINTER_sink = 0;
}

void POINTER_main( void )
{
    if ( POINTER_chunk == NULL ) return;

    register struct POINTER_line *cur = POINTER_chunk[ 0 ].next;
    /* Mix loads and stores with a deterministic pattern: every block of
     * 100 ops contains POINTER_LOAD_RATIO loads followed by the rest as
     * stores. This matches the threshold-counter logic from the original
     * ARMv8 asm version. */
    int counter = 0;
    for ( int i = 0; i < POINTER_ITER; i++ ) {
        if ( counter < POINTER_LOAD_RATIO ) {
            cur = cur->next;                 /* load (pointer chase) */
        } else {
            cur->data = 0;                   /* store               */
        }
        counter++;
        if ( counter >= 100 ) counter = 0;
    }
    POINTER_sink = (uintptr_t)cur;
}

int POINTER_return( void )
{
    return (int)( POINTER_sink & 0x7fffffff );
}

int main( void )
{
    POINTER_init();
    POINTER_main();
    return POINTER_return();
}
