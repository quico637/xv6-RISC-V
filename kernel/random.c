#include "param.h"
#include "types.h"

#define NROUNDS 10

// Return a integer between 0 and ((2^32 - 1) / 2), which is 2147483647.
uint
random(int seed)
{
  // // Take from http://stackoverflow.com/questions/1167253/implementation-of-rand
  // static unsigned int z1 = 12345, z2 = 12345, z3 = 12345, z4 = 12345;
  // unsigned int b;
  // b = seed;
  // b  = ((z1 << 6) ^ z1) >> 13;
  // z1 = ((z1 & 4294967294U) << 18) ^ b;
  // b  = ((z2 << 2) ^ z2) >> 27; 
  // z2 = ((z2 & 4294967288U) << 2) ^ b;
  // b  = ((z3 << 13) ^ z3) >> 21;
  // z3 = ((z3 & 4294967280U) << 7) ^ b;
  // b  = ((z4 << 3) ^ z4) >> 12;
  // z4 = ((z4 & 4294967168U) << 13) ^ b;

  // return (z1 ^ z2 ^ z3 ^ z4) / 2;
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
