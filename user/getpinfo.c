#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/pstat.h"


int
main(int argc, char *argv[])
{
  fprintf(1, "calling getpinfo...\n");
  settickets(12);
  struct pstat ps;
  int ret = getpinfo(&ps);
  if (ret < 0){
    fprintf(2, "getpinfo error\n");
    exit(1);
  }
  
  for (int i = 0; i < NPROC; i++)
    fprintf(1, "%d- Process: %d\tTickets: %d\tTicks: %d\tUsed: %d\n", i, ps.pid[i], ps.tickets[i], ps.ticks[i], ps.inuse[i]);

  fprintf(1, "returning from getpinfo with value %d\n", ret);

  exit(0);
}
