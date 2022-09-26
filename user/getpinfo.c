#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/pstat.h"

#define NCHILDS 3

int main(int argc, char *argv[])
{
	fprintf(1, "calling getpinfo...\n");
	settickets(29);

	if (argc != 2)
	{
		fprintf(2, "USAGE: %s <rows_csv_file>\n", argv[0]);
		exit(-1);
	}

	const int NROWS = atoi(argv[1]);

	int pids[NCHILDS];

	for (int i = 0; i < NCHILDS; i++)
	{
		switch (pids[i] = fork())
		{
		case -1: /* fork() fallÃ³ */
			fprintf(1, "perror(fork");
			exit(-1);
			break;
		case 0: /* Ejecucion del proceso hijo tras fork() con Ã©xito */
			settickets(10 * (i + 1));
			int b = 0;
			for (int i = 0; i < 9999999990; i++)
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

	int fd = open("salida.csv", 0x200 | 0x001); // O_CREATE | O_WRONLY
	if (fd < 0)
	{
		fprintf(2, "Cannot open file!.");
		exit(-1);
	}

	fprintf(fd, "Process A;Process B;Process C\n");
	
	for (int j = 0; j < NROWS; j++)
	{
		struct pstat ps;
		int ret = getpinfo(&ps);
		if (ret < 0)
		{
			fprintf(2, "getpinfo error\n");
			exit(1);
		}

		for (int i = 0; i < NPROC; i++)
		{
			fprintf(1, "\t-------------------------\n");
			if (ps.inuse[i])
			{
				char endl;
				if (ps.tickets[i] == 30)
					endl = '\n';
				else
					endl = ';';
				if (ps.tickets[i] % 10 == 0)
				{
					fprintf(1, "%d - Process: %d\tTickets: %d\tTicks: %d\tUsed: %d\n", i, ps.pid[i], ps.tickets[i], ps.ticks[i], ps.inuse[i]);
					fprintf(fd, "%d%c", ps.ticks[i], endl);
				}
			}
		}

		sleep(100);		
	}

	for (int i = 0; i < NCHILDS; i++)
		kill(pids[i]);

	fprintf(1, "Returning from getinfo.c with status 0!\n");

	exit(0);
}
