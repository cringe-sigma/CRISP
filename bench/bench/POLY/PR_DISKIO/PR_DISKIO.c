/*
 * PR_DISKIO bench (ported from PolyRhythm/src/Disk_IO_Attack.c)
 *
 * Disk-bandwidth stress via posix_fadvise + repeated pwrite at a stride
 * across a file backed by /tmp.  PolyRhythm targeted SD-card-bound
 * embedded victims; on tmpfs this still exercises the FS / VFS / page
 * cache layers.  Each instance uses a private file (pid-suffixed).
 *
 * Tunables:
 *   PR_DISK_PAGES    file size in pages              (default 256)
 *   PR_DISK_PAGE     page bytes                      (default 4096)
 *   PR_DISK_CONTENT  bytes written per pwrite        (default 256)
 *   PR_DISK_STRIDE   bytes skipped between writes    (default 4096)
 *   PR_DISK_ITER     ops per _main()                 (default 64)
 *   PR_DISK_DIR      target directory                (default "/tmp")
 *   PR_DISK_RANDOM   1=POSIX_FADV_RANDOM 0=SEQUENTIAL (default 1)
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PR_DISK_PAGES
#define PR_DISK_PAGES    256
#endif
#ifndef PR_DISK_PAGE
#define PR_DISK_PAGE     4096
#endif
#ifndef PR_DISK_CONTENT
#define PR_DISK_CONTENT  256
#endif
#ifndef PR_DISK_STRIDE
#define PR_DISK_STRIDE   4096
#endif
#ifndef PR_DISK_ITER
#define PR_DISK_ITER     64
#endif
#ifndef PR_DISK_DIR
#define PR_DISK_DIR      "/tmp"
#endif
#ifndef PR_DISK_RANDOM
#define PR_DISK_RANDOM   1
#endif

void PR_DISKIO_init(void);
void PR_DISKIO_main(void);
int  PR_DISKIO_return(void);
int  main(void);

static int      PR_DK_fd = -1;
static char     PR_DK_path[256];
static char    *PR_DK_buf = NULL;
static off_t    PR_DK_filesize = 0;
static uint32_t PR_DK_rng = 0xFADEBABEu;
static volatile int PR_DK_sink = 0;

static inline uint32_t PR_DK_xorshift(void)
{
    uint32_t x = PR_DK_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    PR_DK_rng = x;
    return x;
}

void PR_DISKIO_init(void)
{
    PR_DK_filesize = (off_t)PR_DISK_PAGE * (off_t)PR_DISK_PAGES;
    PR_DK_rng = 0xFADEBABEu;
    if (!PR_DK_buf) {
        PR_DK_buf = (char *)malloc((size_t)PR_DISK_CONTENT);
        if (PR_DK_buf) {
            for (int i = 0; i < PR_DISK_CONTENT; i++)
                PR_DK_buf[i] = (char)('A' + (PR_DK_xorshift() % 26u));
        }
    }
    snprintf(PR_DK_path, sizeof(PR_DK_path),
             "%s/pr_disk_%d_%u.tmp",
             PR_DISK_DIR, (int)getpid(), (unsigned)PR_DK_xorshift());
    if (PR_DK_fd < 0) {
        PR_DK_fd = open(PR_DK_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (PR_DK_fd >= 0) {
            (void)ftruncate(PR_DK_fd, PR_DK_filesize);
#ifdef POSIX_FADV_RANDOM
#if PR_DISK_RANDOM
            (void)posix_fadvise(PR_DK_fd, 0, 0, POSIX_FADV_RANDOM);
#else
            (void)posix_fadvise(PR_DK_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
#endif
        }
    }
    PR_DK_sink = 0;
}

void PR_DISKIO_main(void)
{
    if (PR_DK_fd < 0 || !PR_DK_buf) return;
    for (int i = 0; i < PR_DISK_ITER; i++) {
        off_t off = (off_t)((PR_DK_xorshift() %
                             (uint32_t)(PR_DK_filesize / (off_t)PR_DISK_STRIDE)) *
                            (uint32_t)PR_DISK_STRIDE);
        ssize_t w = pwrite(PR_DK_fd, PR_DK_buf,
                           (size_t)PR_DISK_CONTENT, off);
        PR_DK_sink ^= (int)w;
    }
}

int PR_DISKIO_return(void)
{
    if (PR_DK_fd >= 0) { close(PR_DK_fd); PR_DK_fd = -1; }
    if (PR_DK_path[0]) { remove(PR_DK_path); PR_DK_path[0] = 0; }
    return PR_DK_sink;
}

int main(void) { PR_DISKIO_init(); PR_DISKIO_main(); return PR_DISKIO_return(); }
