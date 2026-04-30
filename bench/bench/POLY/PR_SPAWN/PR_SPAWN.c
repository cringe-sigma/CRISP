/*
 * PR_SPAWN bench (ported, but DEFANGED, from PolyRhythm/src/Attacks.c -- spawn_attack)
 *
 * Original PolyRhythm spawn_attack is a self-replicating fork bomb (it
 * does posix_spawn(self) in an infinite loop, and after reaching
 * max_THREAD it deadlocks the children in `while(1){}`).  That cannot
 * be safely embedded in the harness because:
 *   - multi_proc_pmu already itself fork()s background workers;
 *     compounding posix_spawn() inside each worker explodes O(N^k);
 *   - the original loop has no termination, so the worker process
 *     would never honor SIGTERM/SIGKILL cleanup paths.
 *
 * This bench preserves the *interference channel* (kernel scheduler /
 * fork+execve / wait4 path) but caps it to a finite, deterministic
 * burst per call.  No child re-spawns; each child just execve()s a
 * bounded "no-op" program (default /bin/true) and exits, then we
 * waitpid() all of them.  multi_proc_pmu's outer loop on cpu1..cpuN
 * provides the steady-state stress.
 *
 * Tunables:
 *   PR_SPAWN_BURST   children per _main() call         (default 8, max 32)
 *   PR_SPAWN_PROG    program path passed to execve     (default "/bin/true")
 */

#include <errno.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PR_SPAWN_BURST
#define PR_SPAWN_BURST  8
#endif
#if PR_SPAWN_BURST > 32
#error "PR_SPAWN_BURST is hard-capped at 32 to prevent fork-bomb behavior"
#endif
#ifndef PR_SPAWN_PROG
#define PR_SPAWN_PROG   "/bin/true"
#endif

void PR_SPAWN_init(void);
void PR_SPAWN_main(void);
int  PR_SPAWN_return(void);
int  main(void);

extern char **environ;

static volatile int PR_SPAWN_sink = 0;
static char *const  PR_SPAWN_argv[] = { (char *)PR_SPAWN_PROG, NULL };

void PR_SPAWN_init(void) { PR_SPAWN_sink = 0; }

void PR_SPAWN_main(void)
{
    pid_t pids[PR_SPAWN_BURST];
    int   spawned = 0;

    for (int i = 0; i < PR_SPAWN_BURST; i++) {
        pid_t p = -1;
        int rc = posix_spawn(&p, PR_SPAWN_PROG, NULL, NULL,
                             PR_SPAWN_argv, environ);
        if (rc == 0 && p > 0) {
            pids[spawned++] = p;
        } else {
            /* On EAGAIN (RLIMIT_NPROC) just stop spawning more in this
             * burst; the harness will retry next iteration. */
            break;
        }
    }

    /* Reap every child we created -- never leave zombies and never
     * loop unbounded. */
    for (int i = 0; i < spawned; i++) {
        int status = 0;
        if (waitpid(pids[i], &status, 0) > 0) {
            PR_SPAWN_sink ^= status;
        }
    }
    PR_SPAWN_sink ^= spawned;
}

int PR_SPAWN_return(void) { return PR_SPAWN_sink; }

int main(void) { PR_SPAWN_init(); PR_SPAWN_main(); return PR_SPAWN_return(); }
