/*
 * MAX_LLC bench  —  最猛 LLC 攻击者 (RAMPART E2.1 自定义)
 *
 * 设计目标：在 i.MX 8M Mini (A53, 512 KiB L2, 64B line, 16-way) 上
 *           把 L2 cache 容量打到饱和。
 *
 * 三条原则：
 *   1) 工作集 = 1.5 MiB，远大于 L2 — 任何替换策略都救不回 hit。
 *   2) 访问索引由 xorshift32 生成，stride 不可预测 — 关掉 stride 预取器。
 *   3) 每访问做 read-modify-write，强制 line 进入 L2 dirty，提高 evict 代价。
 *
 * Tunables (compile-time):
 *   MAX_LLC_KB    工作集大小 KiB                  (default 1536, ≈ 3× L2)
 *   MAX_LLC_ITER  每次 _main() 内循环次数         (default 1)
 *   MAX_LLC_OPS   每个 iter 内访问次数            (default = N (一遍))
 */

#include <stdint.h>
#include <stdlib.h>

#ifndef MAX_LLC_KB
#define MAX_LLC_KB   1536u
#endif
#ifndef MAX_LLC_ITER
#define MAX_LLC_ITER 1
#endif

#define MAX_LLC_LINE  64u
#define MAX_LLC_BYTES (MAX_LLC_KB * 1024u)

void MAX_LLC_init(void);
void MAX_LLC_main(void);
int  MAX_LLC_return(void);
int  main(void);

static volatile uint8_t *MAX_LLC_buf  = NULL;
static volatile uint64_t MAX_LLC_sink = 0;

void MAX_LLC_init(void)
{
    if (!MAX_LLC_buf) MAX_LLC_buf = (volatile uint8_t *)malloc(MAX_LLC_BYTES);
    if (MAX_LLC_buf) {
        /* touch every line so MMU mapping is committed */
        for (uint32_t i = 0; i < MAX_LLC_BYTES; i += MAX_LLC_LINE) {
            MAX_LLC_buf[i] = (uint8_t)(i & 0xff);
        }
    }
    MAX_LLC_sink = 0;
}

void MAX_LLC_main(void)
{
    if (!MAX_LLC_buf) return;
    register volatile uint8_t *b = MAX_LLC_buf;
    /* xorshift32 PRNG — defeats hardware stride prefetch */
    register uint32_t s = 0xdeadbeefu ^ (uint32_t)(uintptr_t)b;
    /* number of accesses per iter ≈ buffer / line — i.e. one full sweep */
    const uint32_t ops = MAX_LLC_BYTES / MAX_LLC_LINE;
    register uint64_t acc = MAX_LLC_sink;

    for (int it = 0; it < MAX_LLC_ITER; it++) {
        for (uint32_t k = 0; k < ops; k++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            uint32_t idx = (s & (MAX_LLC_BYTES - 1)) & ~(MAX_LLC_LINE - 1u);
            /* read-modify-write: pulls line + dirties it */
            uint8_t v = b[idx];
            b[idx] = v + 1;
            acc ^= (uint64_t)v;
        }
    }
    MAX_LLC_sink = acc;
}

int MAX_LLC_return(void) { return (int)(MAX_LLC_sink & 0x7fffffff); }

int main(void) { MAX_LLC_init(); MAX_LLC_main(); return MAX_LLC_return(); }
