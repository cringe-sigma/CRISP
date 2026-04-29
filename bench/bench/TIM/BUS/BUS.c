/*
 * BUS bench (extracted from slaver_00_rpmsg-ping.c)
 *
 * Bus / memory-copy stress. Allocates two buffers and repeatedly issues
 * either a plain copy or a CPU-modifying copy between them. Three traffic
 * directions are mixed according to a ratio of 3 single-digit weights
 * (hundreds = mem1->mem2, tens = mem2->mem1, ones = mem1->mem1).
 *
 * Tunable parameters (override at compile time with -D<NAME>=<value>):
 *   BUS_SIZE_MB    buffer size in MiB                        (default 1)
 *   BUS_DATA_TYPE  1..8  uint8/int8/uint16/int16/uint32/int32/uint64/int64
 *                                                            (default 5 = uint32)
 *   BUS_DIR_RATIO  3-digit traffic mix, e.g. 333 -> 3:3:3    (default 333)
 *   BUS_CPU_RATIO  percent of ops that go through the CPU op (default 50)
 *   BUS_ITER       number of copy ops per _main()            (default 4)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef BUS_SIZE_MB
#define BUS_SIZE_MB     1
#endif
#ifndef BUS_DATA_TYPE
#define BUS_DATA_TYPE   5
#endif
#ifndef BUS_DIR_RATIO
#define BUS_DIR_RATIO   333
#endif
#ifndef BUS_CPU_RATIO
#define BUS_CPU_RATIO   50
#endif
#ifndef BUS_ITER
#define BUS_ITER        4
#endif

#define BUS_MB ((size_t)1024u * 1024u)

void BUS_init( void );
void BUS_main( void );
int  BUS_return( void );
int  main( void );

typedef void (*BUS_CopyFunc)( void *dest, void *src, size_t count );

typedef struct {
    size_t       type_size;
    BUS_CopyFunc copy;
    BUS_CopyFunc cpu_op;
} BUS_DataTypeOps;

#define BUS_DEFINE_FUNCS(type)                                        \
static void BUS_copy_##type( void *dest, void *src, size_t count ) {  \
    type *d = (type *)dest;                                           \
    type *s = (type *)src;                                            \
    for ( size_t i = 0; i < count; i++ ) d[ i ] = s[ i ];             \
}                                                                     \
static void BUS_cpu_##type( void *dest, void *src, size_t count ) {   \
    type *d = (type *)dest;                                           \
    type *s = (type *)src;                                            \
    for ( size_t i = 0; i < count; i++ )                              \
        d[ i ] = (type)( s[ i ] + (type)i );                          \
}

BUS_DEFINE_FUNCS( uint8_t  )
BUS_DEFINE_FUNCS( int8_t   )
BUS_DEFINE_FUNCS( uint16_t )
BUS_DEFINE_FUNCS( int16_t  )
BUS_DEFINE_FUNCS( uint32_t )
BUS_DEFINE_FUNCS( int32_t  )
BUS_DEFINE_FUNCS( uint64_t )
BUS_DEFINE_FUNCS( int64_t  )

static void BUS_init_ops( int data_type, BUS_DataTypeOps *ops )
{
    switch ( data_type ) {
        case 1: ops->type_size = 1; ops->copy = BUS_copy_uint8_t;  ops->cpu_op = BUS_cpu_uint8_t;  break;
        case 2: ops->type_size = 1; ops->copy = BUS_copy_int8_t;   ops->cpu_op = BUS_cpu_int8_t;   break;
        case 3: ops->type_size = 2; ops->copy = BUS_copy_uint16_t; ops->cpu_op = BUS_cpu_uint16_t; break;
        case 4: ops->type_size = 2; ops->copy = BUS_copy_int16_t;  ops->cpu_op = BUS_cpu_int16_t;  break;
        case 5: ops->type_size = 4; ops->copy = BUS_copy_uint32_t; ops->cpu_op = BUS_cpu_uint32_t; break;
        case 6: ops->type_size = 4; ops->copy = BUS_copy_int32_t;  ops->cpu_op = BUS_cpu_int32_t;  break;
        case 7: ops->type_size = 8; ops->copy = BUS_copy_uint64_t; ops->cpu_op = BUS_cpu_uint64_t; break;
        case 8: ops->type_size = 8; ops->copy = BUS_copy_int64_t;  ops->cpu_op = BUS_cpu_int64_t;  break;
        default:
            ops->type_size = 4; ops->copy = BUS_copy_uint32_t; ops->cpu_op = BUS_cpu_uint32_t; break;
    }
}

static void           *BUS_mem1     = NULL;
static void           *BUS_mem2     = NULL;
static size_t          BUS_elements = 0;
static BUS_DataTypeOps BUS_ops;
static uint32_t        BUS_rng      = 0xC0FFEEu;
static volatile int    BUS_checksum = 0;

static inline uint32_t BUS_xorshift( void )
{
    uint32_t x = BUS_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    BUS_rng = x;
    return x;
}

void BUS_init( void )
{
    BUS_init_ops( BUS_DATA_TYPE, &BUS_ops );

    size_t mem_size = (size_t)BUS_SIZE_MB * BUS_MB;
    if ( BUS_mem1 == NULL ) BUS_mem1 = malloc( mem_size );
    if ( BUS_mem2 == NULL ) BUS_mem2 = malloc( mem_size );
    if ( BUS_mem1 != NULL ) memset( BUS_mem1, 0xA5, mem_size );
    if ( BUS_mem2 != NULL ) memset( BUS_mem2, 0x5A, mem_size );

    BUS_elements = mem_size / BUS_ops.type_size;
    BUS_rng      = 0xC0FFEEu;
    BUS_checksum = 0;
}

void BUS_main( void )
{
    int r1 = BUS_DIR_RATIO / 100;
    int r2 = ( BUS_DIR_RATIO / 10 ) % 10;
    int r3 = BUS_DIR_RATIO % 10;
    int total = r1 + r2 + r3;
    if ( total <= 0 ) total = 1;

    for ( int it = 0; it < BUS_ITER; it++ ) {
        int dir     = (int)( BUS_xorshift() % (uint32_t)total );
        int use_cpu = (int)( BUS_xorshift() % 100u ) < BUS_CPU_RATIO;

        if ( dir < r1 ) {
            if ( use_cpu ) BUS_ops.cpu_op( BUS_mem2, BUS_mem1, BUS_elements );
            else           BUS_ops.copy  ( BUS_mem2, BUS_mem1, BUS_elements );
        } else if ( dir < r1 + r2 ) {
            if ( use_cpu ) BUS_ops.cpu_op( BUS_mem1, BUS_mem2, BUS_elements );
            else           BUS_ops.copy  ( BUS_mem1, BUS_mem2, BUS_elements );
        } else {
            if ( use_cpu ) BUS_ops.cpu_op( BUS_mem1, BUS_mem1, BUS_elements );
            else           BUS_ops.copy  ( BUS_mem1, BUS_mem1, BUS_elements );
        }
    }

    /* Touch a few bytes so the writes are observable. */
    BUS_checksum ^= ( (unsigned char *)BUS_mem1 )[ 0 ];
    BUS_checksum ^= ( (unsigned char *)BUS_mem2 )[ 0 ];
}

int BUS_return( void )
{
    return BUS_checksum;
}

int main( void )
{
    BUS_init();
    BUS_main();
    return BUS_return();
}
