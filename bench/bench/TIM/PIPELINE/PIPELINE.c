/*
 * PIPELINE bench (extracted from slaver_00_rpmsg-ping.c)
 *
 * Floating-point pipeline stress. Computes sin(x) via two Taylor-style
 * polynomials: a "full" form (long dependency-chain-friendly) and a
 * "bubble" form using Horner's scheme. The fraction that takes the full
 * form is controlled by RATIO; coefficients are pre-rounded to PRECISION
 * decimal digits.
 *
 * Tunable parameters (override at compile time with -D<NAME>=<value>):
 *   PIPELINE_RATIO      percent of ops using full form [0..100] (default 50)
 *   PIPELINE_PRECISION  decimal digits to keep in coefficients  (default 6)
 *   PIPELINE_ITER       number of FP ops per _main()            (default 4096)
 */

#include <stdint.h>
#include <stdlib.h>

#ifndef PIPELINE_RATIO
#define PIPELINE_RATIO      50
#endif
#ifndef PIPELINE_PRECISION
#define PIPELINE_PRECISION  6
#endif
#ifndef PIPELINE_ITER
#define PIPELINE_ITER       4096
#endif

void PIPELINE_init( void );
void PIPELINE_main( void );
int  PIPELINE_return( void );
int  main( void );

static const double PIPELINE_a0 = +1.0;
static const double PIPELINE_a1 = -1.666666666666580809419428987894207e-1;
static const double PIPELINE_a2 = +8.333333333262716094425007738346873e-3;
static const double PIPELINE_a3 = -1.984126982005911439283646346964929e-4;
static const double PIPELINE_a4 = +2.755731607338689220657382272783309e-6;
static const double PIPELINE_a5 = -2.505185130214293595900283000271652e-8;
static const double PIPELINE_a6 = +1.604729591825977400374002000065495e-10;
static const double PIPELINE_a7 = -7.364589573262279913270651228486670e-13;

static double          PIPELINE_A0, PIPELINE_A1, PIPELINE_A2, PIPELINE_A3;
static double          PIPELINE_A4, PIPELINE_A5, PIPELINE_A6, PIPELINE_A7;
static uint32_t        PIPELINE_rng = 0xBADC0DEu;
static volatile double PIPELINE_sum = 0.0;

static inline uint32_t PIPELINE_xorshift( void )
{
    uint32_t x = PIPELINE_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    PIPELINE_rng = x;
    return x;
}

static int PIPELINE_power10( int n )
{
    int result = 1;
    while ( n-- > 0 ) result *= 10;
    return result;
}

static double PIPELINE_truncate( double value, int precision )
{
    const int factor = PIPELINE_power10( precision );
    return (double)( (long long)( value * factor ) ) / factor;
}

static double PIPELINE_sin_full( double x )
{
    return x * PIPELINE_A0
         + x * x * x * PIPELINE_A1
         + x * x * x * x * x * PIPELINE_A2
         + x * x * x * x * x * x * x * PIPELINE_A3
         + x * x * x * x * x * x * x * x * x * PIPELINE_A4
         + x * x * x * x * x * x * x * x * x * x * x * PIPELINE_A5
         + x * x * x * x * x * x * x * x * x * x * x * x * x * PIPELINE_A6
         + x * x * x * x * x * x * x * x * x * x * x * x * x * x * x * PIPELINE_A7;
}

static double PIPELINE_sin_bubble( double x )
{
    const double x2 = x * x;
    return x * ( PIPELINE_A0 + x2 * ( PIPELINE_A1 + x2 * ( PIPELINE_A2 + x2 *
           ( PIPELINE_A3 + x2 * ( PIPELINE_A4 + x2 * ( PIPELINE_A5 + x2 *
           ( PIPELINE_A6 + x2 * PIPELINE_A7 ) ) ) ) ) ) );
}

void PIPELINE_init( void )
{
    int p = PIPELINE_PRECISION;
    PIPELINE_A0 = PIPELINE_truncate( PIPELINE_a0, p );
    PIPELINE_A1 = PIPELINE_truncate( PIPELINE_a1, p );
    PIPELINE_A2 = PIPELINE_truncate( PIPELINE_a2, p );
    PIPELINE_A3 = PIPELINE_truncate( PIPELINE_a3, p );
    PIPELINE_A4 = PIPELINE_truncate( PIPELINE_a4, p );
    PIPELINE_A5 = PIPELINE_truncate( PIPELINE_a5, p );
    PIPELINE_A6 = PIPELINE_truncate( PIPELINE_a6, p );
    PIPELINE_A7 = PIPELINE_truncate( PIPELINE_a7, p );
    PIPELINE_rng = 0xBADC0DEu;
    PIPELINE_sum = 0.0;
}

void PIPELINE_main( void )
{
    double sum = PIPELINE_sum;
    for ( int i = 0; i < PIPELINE_ITER; i++ ) {
        unsigned r    = PIPELINE_xorshift();
        int      dec  = (int)( r % 100u );
        /* Use the rng for x as well so we don't depend on rand()/time(). */
        double   x    = (double)( r & 0xFFFFu ) * ( 1.0 / 65536.0 );

        sum += ( dec < PIPELINE_RATIO )
                 ? PIPELINE_sin_full(   x )
                 : PIPELINE_sin_bubble( x );

        /* Defeat trivial dead-code elimination. */
        __asm__ volatile ( "" : "+g"( sum ) );
    }
    PIPELINE_sum = sum;
}

int PIPELINE_return( void )
{
    /* Squash to int but keep low bits sensitive to the sum. */
    union { double d; uint64_t u; } v;
    v.d = PIPELINE_sum;
    return (int)( v.u & 0x7fffffff );
}

int main( void )
{
    PIPELINE_init();
    PIPELINE_main();
    return PIPELINE_return();
}
