/*
 * MTH_PIPELINE bench (ported from multicore-test-harness/src/pipeline_set/pipeline.c)
 *
 * Computational stress via Taylor-style sin() evaluation.  Two forms are
 * available; selected by MTH_PIPELINE_FULL.
 *
 * Tunables:
 *   MTH_PIPELINE_ITER   iterations per _main()    (default 4096)
 *   MTH_PIPELINE_FULL   1=full form, 0=bubble Horner   (default 1)
 */

#include <stdint.h>

#ifndef MTH_PIPELINE_ITER
#define MTH_PIPELINE_ITER  4096
#endif
#ifndef MTH_PIPELINE_FULL
#define MTH_PIPELINE_FULL  1
#endif

void MTH_PIPELINE_init(void);
void MTH_PIPELINE_main(void);
int  MTH_PIPELINE_return(void);
int  main(void);

static const double MTH_PL_a0 = +1.0;
static const double MTH_PL_a1 = -1.666666666666580809419428987894207e-1;
static const double MTH_PL_a2 = +8.333333333262716094425037738346873e-3;
static const double MTH_PL_a3 = -1.984126982005911439283646346964929e-4;
static const double MTH_PL_a4 = +2.755731607338689220657382272783309e-6;
static const double MTH_PL_a5 = -2.505185130214293595900283001271652e-8;
static const double MTH_PL_a6 = +1.604729591825977403374012010065495e-10;
static const double MTH_PL_a7 = -7.364589573262279913270651228486670e-13;

static volatile double MTH_PIPELINE_sum = 0.0;
static uint32_t        MTH_PIPELINE_rng = 0x12345678u;

static inline uint32_t MTH_PL_xorshift(void)
{
    uint32_t x = MTH_PIPELINE_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    MTH_PIPELINE_rng = x;
    return x;
}

static double MTH_sin_full(double x)
{
    return x * MTH_PL_a0
         + x*x*x * MTH_PL_a1
         + x*x*x*x*x * MTH_PL_a2
         + x*x*x*x*x*x*x * MTH_PL_a3
         + x*x*x*x*x*x*x*x*x * MTH_PL_a4
         + x*x*x*x*x*x*x*x*x*x*x * MTH_PL_a5
         + x*x*x*x*x*x*x*x*x*x*x*x*x * MTH_PL_a6
         + x*x*x*x*x*x*x*x*x*x*x*x*x*x*x * MTH_PL_a7;
}

static double MTH_sin_bubble(double x)
{
    double x2 = x * x;
    return x * (MTH_PL_a0 + x2 * (MTH_PL_a1 + x2 * (MTH_PL_a2 + x2 *
            (MTH_PL_a3 + x2 * (MTH_PL_a4 + x2 * (MTH_PL_a5 + x2 *
            (MTH_PL_a6 + x2 * MTH_PL_a7)))))));
}

void MTH_PIPELINE_init(void)
{
    MTH_PIPELINE_sum = 0.0;
    MTH_PIPELINE_rng = 0x12345678u;
}

void MTH_PIPELINE_main(void)
{
    double sum = MTH_PIPELINE_sum;
    for (int i = 0; i < MTH_PIPELINE_ITER; i++) {
        double x = (double)(MTH_PL_xorshift() & 0xffff) / 65536.0;
#if MTH_PIPELINE_FULL
        sum += MTH_sin_full(x);
#else
        sum += MTH_sin_bubble(x);
#endif
    }
    MTH_PIPELINE_sum = sum;
}

int MTH_PIPELINE_return(void) { return (int)MTH_PIPELINE_sum; }

int main(void) { MTH_PIPELINE_init(); MTH_PIPELINE_main(); return MTH_PIPELINE_return(); }
