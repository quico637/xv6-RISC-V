#include "param.h"
#include "types.h"

#define NROUNDS 10

uint
random(int seed)
{
  int a = 1103515245, c = 12345, m = 32768;
  int xn = seed;
  int xn1;
  for (int i = 0; i < NROUNDS; i++) {
    xn1 = (a * xn + c) % m;
    xn = xn1;
  }
  return xn;
}

// Return a random integer between a given range.
int
randomrange(int seed, int lo, int hi)
{
  if (hi < lo) {
    int tmp = lo;
    lo = hi;
    hi = tmp;
  }
  int range = hi - lo + 1;
  return random(seed) % (range) + lo;
}
