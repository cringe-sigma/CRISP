/*
 * MAX_MEM bench  —  最猛 LPDDR4 行缓冲攻击者
 *
 * 设计目标：让 DRAM controller 的 row-buffer hit 率掉到接近 0。
 *   1) 工作集 = 16 MiB，明显大于任意 row buffer (~8 KiB×bank)。
 *   2) 每次跳到「上次地址 + N×row-size + 随机偏移」，强制 row
 *      activate / precharge 双倍开销。
 *   3) 半读半写 — 同时压满读写队列。
 *
 * Tunables:
 *   MAX_MEM_KB    工作集 KiB                       (default 16384 = 16 MiB)
 *   MAX_MEM_ROW   行大小猜测 (bytes)               (default 8192)
 *   MAX_MEM_ITER  每次 _main() iter                (default 1)
 */

#include <stdint.h>
#include <stdlib.h>

#ifndef MAX_MEM_KB
#define MAX_MEM_KB   16384u
#endif
#ifndef MAX_MEM_ROW
#define MAX_MEM_ROW  8192u
#endif
#ifndef MAX_MEM_ITER
#define MAX_MEM_ITER 1
#endif

#define MAX_MEM_LINE  64u
#define MAX_MEM_BYTES (MAX_MEM_KB * 1024u)

void MAX_MEM_init(void);
void MAX_MEM_main(void);
int  MAX_MEM_return(void);
int  main(void);

static volatile uint8_t *MAX_MEM_buf  = NULL;
static volatile uint64_t MAX_MEM_sink = 0;

void MAX_MEM_init(void)
{
    if (!MAX_MEM_buf) MAX_MEM_buf = (volatile uint8_t *)malloc(MAX_MEM_BYTES);
    if (MAX_MEM_buf) {
        for (uint32_t i = 0; i < MAX_MEM_BYTES; i += MAX_MEM_LINE)
            MAX_MEM_buf[i] = (uint8_t)i;
    }
    MAX_MEM_sink = 0;
}

void MAX_MEM_main(void)
{
    if (!MAX_MEM_buf) return;
    register volatile uint8_t *b = MAX_MEM_buf;
    register uint32_t s = 0xc0ffeebau ^ (uint32_t)(uintptr_t)b;
    /* 行数（猜测） */
    const uint32_t rows = MAX_MEM_BYTES / MAX_MEM_ROW;
    /* 一次 _main() 做 rows*8 次访问，确保跨完所有行 */
    const uint32_t n_ops = rows * 8u;
    register uint64_t acc = MAX_MEM_sink;

    for (int it = 0; it < MAX_MEM_ITER; it++) {
        for (uint32_t k = 0; k < n_ops; k++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            /* 选一个 row，然后在 row 内拿一个 cacheline 偏移 */
            uint32_t row    = s % rows;
            uint32_t in_row = ((s >> 8) & (MAX_MEM_ROW - 1)) & ~(MAX_MEM_LINE - 1u);
            uint32_t idx    = row * MAX_MEM_ROW + in_row;
            if (idx + MAX_MEM_LINE > MAX_MEM_BYTES) continue;
            if (s & 1u) {
                /* read */
                acc ^= b[idx] ^ b[idx + 32];
            } else {
                /* write — dirty 出去时再产生一次 row activate */
                b[idx]      = (uint8_t)s;
                b[idx + 32] = (uint8_t)~s;
            }
        }
    }
    MAX_MEM_sink = acc;
}

int MAX_MEM_return(void) { return (int)(MAX_MEM_sink & 0x7fffffff); }

int main(void) { MAX_MEM_init(); MAX_MEM_main(); return MAX_MEM_return(); }
