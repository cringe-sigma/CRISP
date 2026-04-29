/*
 * CACHE bench (extracted from slaver_00_rpmsg-ping.c)
 *
 * Cache stress workload. Allocates an array of MEM_SIZE bytes, generates
 * a randomized read/write operation sequence, and walks it with a given
 * STRIDE doing a forward pass up to a forward/reverse split point and a
 * reverse pass for the remainder.
 *
 * Tunable parameters (override at compile time with -D<NAME>=<value>):
 *   CACHE_MEM_SIZE   bytes of working set                     (default 256 KiB)
 *   CACHE_STRIDE     element stride between accesses          (default 16)
 *   CACHE_RW_RATIO   read percentage [0..100]                 (default 50)
 *   CACHE_FR_RATIO   forward/reverse split percentage [0..100](default 50)
 *   CACHE_ITER       number of inner iterations per _main()   (default 1)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef CACHE_MEM_SIZE
#define CACHE_MEM_SIZE  (256u * 1024u)
#endif
#ifndef CACHE_STRIDE
#define CACHE_STRIDE    16
#endif
#ifndef CACHE_RW_RATIO
#define CACHE_RW_RATIO  50
#endif
#ifndef CACHE_FR_RATIO
#define CACHE_FR_RATIO  50
#endif
#ifndef CACHE_ITER
#define CACHE_ITER      1
#endif

#define CACHE_ARR_SIZE  ((int64_t)(CACHE_MEM_SIZE / sizeof(int32_t)))

void CACHE_init( void );
void CACHE_main( void );
int  CACHE_return( void );
int  main( void );

typedef enum { CACHE_Read = 0, CACHE_Write = 1 } CACHE_OPER;

typedef struct {
    CACHE_OPER type;
    int        index;
} CACHE_Operation;

static volatile int32_t   *CACHE_array      = NULL;
static CACHE_Operation    *CACHE_operations = NULL;
static int64_t             CACHE_fr_sep     = 0;
static volatile uint64_t   CACHE_sum        = 0;

static int64_t CACHE_scale( int64_t size, uint8_t ratio )
{
    switch ( ratio % 25 ) {
        case 0:
            return ( ratio / 25 ) * size / 4;
        case 5: case 10: case 15: case 20:
            return ratio / 5 * size / 20;
        default:
            return ratio * size / 100;
    }
}

static void CACHE_generateOperations( CACHE_Operation *ops, int64_t size, uint8_t ratio )
{
    int64_t readCount = CACHE_scale( size, ratio );
    int64_t i;

    for ( i = 0; i < readCount; i++ ) {
        ops[ i ].type  = CACHE_Read;
        ops[ i ].index = (int)i;
    }
    for ( i = readCount; i < size; i++ ) {
        ops[ i ].type  = CACHE_Write;
        ops[ i ].index = (int)i;
    }

    /* Deterministic Fisher-Yates shuffle (no srand(time())) so runs are
     * reproducible across measurement samples. */
    uint32_t state = 0x9E3779B9u;
    for ( i = 0; i < size; i++ ) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        int64_t j = i + (int64_t)( state % (uint32_t)( size - i ) );
        CACHE_Operation tmp = ops[ i ];
        ops[ i ] = ops[ j ];
        ops[ j ] = tmp;
    }
}

void CACHE_init( void )
{
    if ( CACHE_array == NULL ) {
        CACHE_array = (int32_t *)malloc( CACHE_MEM_SIZE );
    }
    if ( CACHE_operations == NULL ) {
        CACHE_operations = (CACHE_Operation *)malloc( sizeof( CACHE_Operation ) * (size_t)CACHE_ARR_SIZE );
    }

    for ( int64_t i = 0; i < CACHE_ARR_SIZE; i++ ) {
        CACHE_array[ i ] = (int32_t)( i + 1 );
    }
    CACHE_fr_sep = CACHE_scale( CACHE_ARR_SIZE, CACHE_FR_RATIO );
    CACHE_generateOperations( CACHE_operations, CACHE_ARR_SIZE, CACHE_RW_RATIO );
    CACHE_sum = 0;
}

void CACHE_main( void )
{
    register volatile int32_t *array = CACHE_array;
    register uint64_t          sum   = CACHE_sum;
    const    int64_t           sep   = CACHE_fr_sep;
    const    int64_t           N     = CACHE_ARR_SIZE;

    for ( int it = 0; it < CACHE_ITER; it++ ) {
        for ( int64_t j = 0; j < sep; j += CACHE_STRIDE ) {
            if ( CACHE_operations[ j ].type == CACHE_Read ) {
                sum += (uint64_t)array[ j ];
            } else {
                array[ j ] = 0xff;
            }
        }
        for ( int64_t j = N - 1; j >= sep; j -= CACHE_STRIDE ) {
            if ( CACHE_operations[ j ].type == CACHE_Read ) {
                sum += (uint64_t)array[ j ];
            } else {
                array[ j ] = 0xff;
            }
        }
    }
    CACHE_sum = sum;
}

int CACHE_return( void )
{
    return (int)( CACHE_sum & 0x7fffffff );
}

int main( void )
{
    CACHE_init();
    CACHE_main();
    return CACHE_return();
}
