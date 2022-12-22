#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fs.h"
#include "user/user.h"

void fork_test();
char buf[BSIZE];

int main(int argc, char *argv[])
{
  fork_test();
  printf("mmaptest: all tests succeeded\n");
  exit(0);
}

char *testname = "???";

void err(char *why)
{
  printf("mmaptest: %s failed: %s, pid=%d\n", testname, why, getpid());
  exit(1);
}

//
// check the content of the two mapped pages.
//
void _v1(char *p)
{
  int i;
  for (i = 0; i < PGSIZE * 2; i++)
  {
    if (i < PGSIZE + (PGSIZE / 2))
    {
      if (p[i] != 'A')
      {
        printf("mismatch at %d, wanted 'A', got 0x%x\n", i, p[i]);
        err("v1 mismatch (1)");
      }
    }
    else
    {
      if (p[i] != 0)
      {
        printf("mismatch at %d, wanted zero, got 0x%x\n", i, p[i]);
        err("v1 mismatch (2)");
      }
    }
  }
}

//
// check the content of the two mapped pages.
//
void _v2(char *p)
{
  int i;
  for (i = 0; i < PGSIZE * 2; i++)
  {
    if (i < PGSIZE)
    {
      if (p[i] != 'Z')
      {
        printf("mismatch at %d, wanted 'Z', got 0x%x\n", i, p[i]);
        err("v2 mismatch (1)");
      }
    }
    else if (i < PGSIZE + (PGSIZE / 2))
    {
      if (p[i] != 'A')
      {
        printf("mismatch at %d, wanted 'A', got 0x%x\n", i, p[i]);
        err("v2 mismatch (2)");
      }
    }
    else
    {
      if (p[i] != 0)
      {
        printf("mismatch at %d, wanted zero, got 0x%x\n", i, p[i]);
        err("v2 mismatch (3)");
      }
    }
  }
}

//
// create a file to be mapped, containing
// 1.5 pages of 'A' and half a page of zeros.
//
void makefile(const char *f)
{
  int i;
  int n = PGSIZE / BSIZE;

  unlink(f);
  int fd = open(f, O_WRONLY | O_CREATE);
  if (fd == -1)
    err("open");
  memset(buf, 'A', BSIZE);
  // write 1.5 page
  for (i = 0; i < n + n / 2; i++)
  {
    if (write(fd, buf, BSIZE) != BSIZE)
      err("write 0 makefile");
  }
  if (close(fd) == -1)
    err("close");
}

//
// mmap a file, then fork.
// check that the child sees the mapped file.
//
void fork_test(void)
{
  int fd;
  int pid;
  const char *const f = "mmap.dur";

  printf("fork_test starting\n");
  testname = "fork_test";

  // mmap the file twice.
  makefile(f);
  if ((fd = open(f, O_RDWR)) == -1)
    err("open");
  unlink(f);
  char *p1 = mmap(0, PGSIZE * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p1 == MAP_FAILED)
    err("mmap (4)");
  char *p2 = mmap(0, PGSIZE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (p2 == MAP_FAILED)
    err("mmap (5)");

  // read just 1st page.
  if (*(p1) != 'A')
    err("fork mismatch (1)");

  if (*(p2) != 'A')
    err("fork mismatch (2)");

  if ((pid = fork()) < 0)
    err("fork");
  if (pid == 0)
  {
    _v1(p1);
    for (int i = 0; i < PGSIZE; i++)
    {
      p1[i] = 'Z';
      p2[i] = 'Z';
    }
    munmap(p1, PGSIZE); // just the first page
    munmap(p2, PGSIZE);
    exit(0);            // tell the parent that the mapping looks OK.
  }

  int status = -1;
  wait(&status);

  if (status != 0)
  {
    printf("fork_test failed\n");
    exit(1);
  }

  // check that the parent's mappings are still there.
  _v2(p1);
  _v1(p2);

  printf("fork_test OK\n");
}
