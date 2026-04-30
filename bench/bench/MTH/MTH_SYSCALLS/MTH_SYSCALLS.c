/*
 * MTH_SYSCALLS bench (ported from multicore-test-harness/src/system_calls_set/system_calls.c)
 *
 * Generates software interrupts via fopen/fseek/fputc/fclose/remove,
 * one cycle per inner iteration.  The file is created in /tmp under a
 * pid-suffixed name so multiple instances don't collide.
 *
 * Tunables:
 *   MTH_SYS_ITER     ops per _main()                (default 64)
 *   MTH_SYS_DIR      target directory for dummy file (default "/tmp")
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef MTH_SYS_ITER
#define MTH_SYS_ITER  64
#endif
#ifndef MTH_SYS_DIR
#define MTH_SYS_DIR   "/tmp"
#endif

void MTH_SYSCALLS_init(void);
void MTH_SYSCALLS_main(void);
int  MTH_SYSCALLS_return(void);
int  main(void);

static char           MTH_SYS_path[256];
static uint32_t       MTH_SYS_rng = 0xC001D00Du;
static volatile int   MTH_SYS_sink = 0;

static inline uint32_t MTH_SYS_xorshift(void)
{
    uint32_t x = MTH_SYS_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    MTH_SYS_rng = x;
    return x;
}

void MTH_SYSCALLS_init(void)
{
    snprintf(MTH_SYS_path, sizeof(MTH_SYS_path),
             "%s/mth_sys_%d_%u.tmp",
             MTH_SYS_DIR, (int)getpid(), (unsigned)MTH_SYS_xorshift());
    MTH_SYS_rng = 0xC001D00Du;
    MTH_SYS_sink = 0;
}

void MTH_SYSCALLS_main(void)
{
    for (int i = 0; i < MTH_SYS_ITER; i++) {
        int  off = (int)(MTH_SYS_xorshift() % 4096u);
        char ch  = (char)(MTH_SYS_xorshift() & 0x7f);
        FILE *fp = fopen(MTH_SYS_path, "w");
        if (!fp) continue;
        if (fseek(fp, off, SEEK_SET) == 0) {
            int r = fputc(ch, fp);
            MTH_SYS_sink ^= r;
        }
        fclose(fp);
        remove(MTH_SYS_path);
    }
}

int MTH_SYSCALLS_return(void) { return MTH_SYS_sink; }

int main(void) { MTH_SYSCALLS_init(); MTH_SYSCALLS_main(); return MTH_SYSCALLS_return(); }
