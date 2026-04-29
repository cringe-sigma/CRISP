/*
 * MEM bench (extracted from slaver_00_rpmsg-ping.c)
 *
 * Memory-write stress. Allocates SIZE_MB MiB of memory, splits it into
 * PAGE_SIZE-byte chunks, and issues memset-style writes whose offset and
 * length pattern is chosen by a 4-digit OP_RATIO (the four digits give
 * the percentage weight, in tens, of the four memset variants).
 *
 * Tunable parameters (override at compile time with -D<NAME>=<value>):
 *   MEM_SIZE_MB     buffer size in MiB                       (default 1)
 *   MEM_PAGE_SIZE   chunk size in bytes                      (default 4096)
 *   MEM_OP_RATIO    4-digit op-mix, e.g. 2521 -> 20/50/20/10 (default 2521)
 *   MEM_ITER        number of (chunk * 5) ops per _main()    (default 4)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MEM_SIZE_MB
#define MEM_SIZE_MB    1
#endif
#ifndef MEM_PAGE_SIZE
#define MEM_PAGE_SIZE  4096
#endif
#ifndef MEM_OP_RATIO
#define MEM_OP_RATIO   2521
#endif
#ifndef MEM_ITER
#define MEM_ITER       4
#endif

#define MEM_MB ((size_t)1024u * 1024u)

void MEM_init( void );
void MEM_main( void );
int  MEM_return( void );
int  main( void );

typedef void (*MEM_func)( void *mem, size_t offset, size_t page_size, int fill );

static void MEM_normal( void *mem, size_t offset, size_t page_size, int fill ) {
    memset( (char *)mem + offset, fill, page_size );
}
static void MEM_half_page( void *mem, size_t offset, size_t page_size, int fill ) {
    memset( (char *)mem + offset, fill, page_size / 2 );
}
static void MEM_half_offset( void *mem, size_t offset, size_t page_size, int fill ) {
    memset( (char *)mem + offset / 2, fill, page_size / 2 );
}
static void MEM_half( void *mem, size_t offset, size_t page_size, int fill ) {
    memset( (char *)mem + offset / 2, fill, page_size / 2 );
}

static volatile void *MEM_buf       = NULL;
static size_t         MEM_chunks    = 0;
static MEM_func       MEM_table[ 100 ];
static uint32_t       MEM_rng       = 0xDEADBEEFu;
static volatile int   MEM_checksum  = 0;

static inline uint32_t MEM_xorshift( void )
{
    uint32_t x = MEM_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    MEM_rng = x;
    return x;
}

void MEM_init( void )
{
    /* Decode 4-digit op_ratio into 4 percentages (tens digit -> *10). */
    int op_ratio = MEM_OP_RATIO;
    int percentages[ 4 ];
    for ( int i = 3; i >= 0; i-- ) {
        percentages[ i ] = ( op_ratio % 10 ) * 10;
        op_ratio /= 10;
    }
    const MEM_func funcs[ 4 ] = {
        MEM_normal, MEM_half_page, MEM_half_offset, MEM_half
    };

    int index = 0;
    for ( int i = 0; i < 4; i++ ) {
        for ( int j = 0; j < percentages[ i ] && index < 100; j++ ) {
            MEM_table[ index++ ] = funcs[ i ];
        }
    }
    /* Pad the rest with the first variant in case percentages don't sum to 100. */
    while ( index < 100 ) MEM_table[ index++ ] = MEM_normal;

    size_t mem_size = (size_t)MEM_SIZE_MB * MEM_MB;
    MEM_chunks = mem_size / MEM_PAGE_SIZE;
    if ( MEM_chunks == 0 ) MEM_chunks = 1;

    if ( MEM_buf == NULL ) {
        MEM_buf = malloc( mem_size );
    }
    if ( MEM_buf != NULL ) {
        memset( (void *)MEM_buf, 0, mem_size );
    }
    MEM_rng      = 0xDEADBEEFu;
    MEM_checksum = 0;
}

void MEM_main( void )
{
    if ( MEM_buf == NULL ) return;

    for ( int it = 0; it < MEM_ITER; it++ ) {
        size_t chunk  = (size_t)( MEM_xorshift() % (uint32_t)MEM_chunks );
        size_t offset = chunk * (size_t)MEM_PAGE_SIZE;

        for ( int i = 0; i < 5; i++ ) {
            unsigned r    = MEM_xorshift() % 100u;
            int      fill = (int)( MEM_xorshift() & 0xff );
            MEM_table[ r ]( (void *)MEM_buf, offset, (size_t)MEM_PAGE_SIZE, fill );
        }
    }
    MEM_checksum ^= ( (unsigned char *)MEM_buf )[ 0 ];
}

int MEM_return( void )
{
    return MEM_checksum;
}

int main( void )
{
    MEM_init();
    MEM_main();
    return MEM_return();
}
