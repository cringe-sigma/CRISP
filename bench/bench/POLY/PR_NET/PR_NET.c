/*
 * PR_NET bench (ported from PolyRhythm/src/Network_Attack.c -- stress_udp_flood)
 *
 * UDP flood toward localhost.  The original PolyRhythm version targeted
 * a hard-coded 127.0.0.1 port and used /proc/net/{tcp,udp} to discover
 * other open ports for online profiling -- that profiling layer is
 * RL-specific and is omitted; we just send to 127.0.0.1:<port>.  The
 * receiver does not need to exist; the kernel still walks the socket
 * stack (and emits ICMP unreachable on close), generating syscall and
 * network-stack contention.
 *
 * Tunables:
 *   PR_NET_PACKET    bytes per packet                (default 1024)
 *   PR_NET_PORT      destination UDP port            (default 11311)
 *   PR_NET_ITER      sendto() calls per _main()      (default 256)
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef PR_NET_PACKET
#define PR_NET_PACKET  1024
#endif
#ifndef PR_NET_PORT
#define PR_NET_PORT    11311
#endif
#ifndef PR_NET_ITER
#define PR_NET_ITER    256
#endif

void PR_NET_init(void);
void PR_NET_main(void);
int  PR_NET_return(void);
int  main(void);

static int               PR_NET_sock = -1;
static struct sockaddr_in PR_NET_to;
static char             *PR_NET_buf = NULL;
static volatile int      PR_NET_sink = 0;
static uint32_t          PR_NET_rng = 0xFEEDFACEu;

static inline uint32_t PR_NET_xorshift(void)
{
    uint32_t x = PR_NET_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    PR_NET_rng = x;
    return x;
}

void PR_NET_init(void)
{
    if (!PR_NET_buf) {
        PR_NET_buf = (char *)malloc((size_t)PR_NET_PACKET);
        if (PR_NET_buf) {
            for (int i = 0; i < PR_NET_PACKET; i++)
                PR_NET_buf[i] = (char)('A' + (PR_NET_xorshift() % 26u));
        }
    }
    if (PR_NET_sock < 0) {
        PR_NET_sock = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&PR_NET_to, 0, sizeof(PR_NET_to));
        PR_NET_to.sin_family = AF_INET;
        PR_NET_to.sin_port   = htons((uint16_t)PR_NET_PORT);
        inet_aton("127.0.0.1", &PR_NET_to.sin_addr);
    }
    PR_NET_sink = 0;
}

void PR_NET_main(void)
{
    if (PR_NET_sock < 0 || !PR_NET_buf) return;
    for (int i = 0; i < PR_NET_ITER; i++) {
        ssize_t n = sendto(PR_NET_sock, PR_NET_buf,
                           (size_t)PR_NET_PACKET, 0,
                           (struct sockaddr *)&PR_NET_to,
                           sizeof(PR_NET_to));
        PR_NET_sink ^= (int)n;
    }
}

int PR_NET_return(void)
{
    if (PR_NET_sock >= 0) { close(PR_NET_sock); PR_NET_sock = -1; }
    return PR_NET_sink;
}

int main(void) { PR_NET_init(); PR_NET_main(); return PR_NET_return(); }
