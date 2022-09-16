#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/pstat.h"


int
main(int argc, char *argv[])
{
  fprintf(1, "calling getpinfo...\n");
  struct pstat * ps;
  int ret = getpinfo(ps);


  fprintf(1, "returning from getpinfo with value %d\n", ret);

  exit(0);
}
