#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/pstat.h"

#define NCHILDS 3

int main(int argc, char *argv[])
{
	fprintf(1, "calling getpinfo...\n");
	settickets(7);

	for (int i = 0; i < NCHILDS; i++)
	{
		int pid;
		switch (pid = fork())
		{
		case -1: /* fork() fallÃ³ */
			fprintf(1, "perror(fork");
			exit(-1);
			break;
		case 0: /* Ejecucion del proceso hijo tras fork() con Ã©xito */
			settickets(10 * (i + 1));
			int b = 0;
			for (int i = 0; i < 99999999999; i++)
			{
				b = 40 * 40 + i;
			}
			fprintf(1, "%d\n", b);
			exit(0);
			break;
		default:
			break;
		}
	}

	sleep(50);

	struct pstat ps;
	int ret = getpinfo(&ps);
	if (ret < 0)
	{
		fprintf(2, "getpinfo error\n");
		exit(1);
	}

	for (int i = 0; i < NPROC; i++)
		fprintf(1, "%d- Process: %d\tTickets: %d\tTicks: %d\tUsed: %d\n", i, ps.pid[i], ps.tickets[i], ps.ticks[i], ps.inuse[i]);

	fprintf(1, "returning from getpinfo with value %d\n", ret);


	exit(0);
}
