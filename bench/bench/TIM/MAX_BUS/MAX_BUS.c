/*
 * MAX_BUS bench  —  最猛 NIC-400 总线攻击者
 *
 * 设计目标：把 L2?DRAM 仲裁路径上的 64B burst 数最大化。
 *   1) 每访问写一个完整 cacheline (64B 连续 stq)，触发 write-allocate +
 *      writeback 两个总线事务。
 *   2) 跨 4 KiB 页频繁切换 — 让总线仲裁器吃不到「连续地址压缩」。
 *   3) 不依赖 cache 容量；2 MiB 工作集刚好让 L2 来回换出，请求始终下
 *      到 NIC-400。
 *
 * Tunables:
 *   MAX_BUS_KB    工作集 KiB                       (default 2048)
 *   MAX_BUS_ITER  每次 _main() 的 iter             (default 1)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX_BUS_KB
#define MAX_BUS_KB   2048u
#endif
#ifndef MAX_BUS_ITER
#define MAX_BUS_ITER 1
#endif

#define MAX_BUS_PAGE  4096u
#define MAX_BUS_LINE  64u
#define MAX_BUS_BYTES (MAX_BUS_KB * 1024u)

void MAX_BUS_init(void);
void MAX_BUS_main(void);
int  MAX_BUS_return(void);
int  main(void);

static volatile uint8_t *MAX_BUS_buf  = NULL;
static volatile uint64_t MAX_BUS_sink = 0;

void MAX_BUS_init(void)
{
    if (!MAX_BUS_buf) MAX_BUS_buf = (volatile uint8_t *)malloc(MAX_BUS_BYTES);
    if (MAX_BUS_buf) memset((void *)MAX_BUS_buf, 0xa5, MAX_BUS_BYTES);
    MAX_BUS_sink = 0;
}

void MAX_BUS_main(void)
{
    if (!MAX_BUS_buf) return;
    register volatile uint64_t *b = (volatile uint64_t *)MAX_BUS_buf;
    register uint32_t s = 0x12345678u ^ (uint32_t)(uintptr_t)b;
    const uint32_t pages = MAX_BUS_BYTES / MAX_BUS_PAGE;
    /* 8 个 uint64_t = 64B = 1 cacheline */
    register uint64_t acc = MAX_BUS_sink;

    for (int it = 0; it < MAX_BUS_ITER; it++) {
        for (uint32_t k = 0; k < pages * 4; k++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            uint32_t page  = s % pages;
            uint32_t off64 = ((s >> 16) & 63u) * 8u; /* 8 lines / page */
            uint32_t base  = (page * MAX_BUS_PAGE + off64 * sizeof(uint64_t)) / sizeof(uint64_t);
            /* 写整条 cacheline — 触发 burst write-allocate + writeback */
            b[base+0] = s;
            b[base+1] = ~s;
            b[base+2] = s ^ 0xa5a5a5a5u;
            b[base+3] = s + 1u;
            b[base+4] = s - 1u;
            b[base+5] = s << 1;
            b[base+6] = s >> 1;
            b[base+7] = ~s ^ 0x5a5a5a5au;
            acc ^= b[base+0];
        }
    }
    MAX_BUS_sink = acc;
}

int MAX_BUS_return(void) { return (int)(MAX_BUS_sink & 0x7fffffff); }

int main(void) { MAX_BUS_init(); MAX_BUS_main(); return MAX_BUS_return(); }
