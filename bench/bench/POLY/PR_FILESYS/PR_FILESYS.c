/*
 * PR_FILESYS bench (ported from PolyRhythm/src/Attacks.c -- filesys_attack)
 *
 * Inode / VFS / page-cache contention through repeated file lifecycle
 * operations: create-write-close, access (read 1 byte), move (rename),
 * delete (unlink).  Each instance uses a private per-pid working
 * directory under /tmp.
 *
 * Tunables:
 *   PR_FS_DIR     working directory                  (default "/tmp")
 *   PR_FS_BYTES   bytes written per CREATE op        (default 4096)
 *   PR_FS_ITER    file-ops per _main()               (default 64)
 *   PR_FS_MODE    0=cycle all 1=CREATE 2=ACCESS 3=MOVE 4=DELETE (default 0)
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PR_FS_DIR
#define PR_FS_DIR    "/tmp"
#endif
#ifndef PR_FS_BYTES
#define PR_FS_BYTES  4096
#endif
#ifndef PR_FS_ITER
#define PR_FS_ITER   64
#endif
#ifndef PR_FS_MODE
#define PR_FS_MODE   0
#endif

void PR_FILESYS_init(void);
void PR_FILESYS_main(void);
int  PR_FILESYS_return(void);
int  main(void);

static char    PR_FS_workdir[256];
static char    PR_FS_buf[PR_FS_BYTES];
static uint32_t PR_FS_rng = 0xC0DEBA5Eu;
static volatile int PR_FS_sink = 0;
static int     PR_FS_seq = 0;

static inline uint32_t PR_FS_xorshift(void)
{
    uint32_t x = PR_FS_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    PR_FS_rng = x;
    return x;
}

static void PR_FS_path(char *out, size_t outsz, unsigned id)
{
    snprintf(out, outsz, "%s/pr_fs_%d_%u.tmp",
             PR_FS_workdir, (int)getpid(), id);
}

static int PR_FS_create(const char *p)
{
    int fd = open(p, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t w = write(fd, PR_FS_buf, sizeof(PR_FS_buf));
    close(fd);
    return (int)w;
}

static int PR_FS_access(const char *p)
{
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    char b;
    ssize_t r = read(fd, &b, 1);
    close(fd);
    return (int)(r == 1 ? (unsigned char)b : -1);
}

void PR_FILESYS_init(void)
{
    snprintf(PR_FS_workdir, sizeof(PR_FS_workdir), "%s", PR_FS_DIR);
    memset(PR_FS_buf, 'x', sizeof(PR_FS_buf));
    PR_FS_rng = 0xC0DEBA5Eu;
    PR_FS_sink = 0;
    PR_FS_seq = 0;
}

void PR_FILESYS_main(void)
{
    char p1[256], p2[256];
    for (int i = 0; i < PR_FS_ITER; i++) {
        unsigned id = (unsigned)PR_FS_seq++;
        PR_FS_path(p1, sizeof(p1), id);
        int op;
#if PR_FS_MODE == 0
        op = (int)(PR_FS_xorshift() & 0x3);   /* 0..3 */
#elif PR_FS_MODE == 1
        op = 0;
#elif PR_FS_MODE == 2
        op = 1;
#elif PR_FS_MODE == 3
        op = 2;
#else
        op = 3;
#endif
        switch (op) {
        case 0: /* CREATE */
            PR_FS_sink ^= PR_FS_create(p1);
            unlink(p1);
            break;
        case 1: /* ACCESS */
            PR_FS_create(p1);
            PR_FS_sink ^= PR_FS_access(p1);
            unlink(p1);
            break;
        case 2: /* MOVE */
            PR_FS_create(p1);
            PR_FS_path(p2, sizeof(p2), id ^ 0xA5A5u);
            PR_FS_sink ^= rename(p1, p2);
            unlink(p2);
            break;
        case 3: /* DELETE */
            PR_FS_create(p1);
            PR_FS_sink ^= unlink(p1);
            break;
        }
    }
}

int PR_FILESYS_return(void) { return PR_FS_sink; }

int main(void) { PR_FILESYS_init(); PR_FILESYS_main(); return PR_FILESYS_return(); }
