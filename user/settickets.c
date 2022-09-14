#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int
main(int argc, char *argv[])
{
  fprintf(1, "calling settickets...\n");
  int ret = settickets(10);
  fprintf(1, "returning from settickets with value %d\n", ret);

  exit(0);
}
