/*
 * exec-sparse
 *
 * 	this creates a child process to run "sparse"
 *
 *   relies on fork, _exit, stdout and stderr, and execv
 *
 */

#include <unistd.h>
#include <err.h>

static char *spargv[2] = { (char *)"sparse", NULL };

static
void
spawnv(const char *prog, char **argv)
{
  pid_t pid = fork();
  switch (pid) {
  case -1:
    err(1, "fork");
  case 0:
    /* child */
    execv(prog, argv);
    err(1, "%s", prog);
  default:
    /* parent */
    break;
  }
}

int
main()
{
  spawnv("/uw-testbin/sparse", spargv);
  return 0;
}
