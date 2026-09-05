#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* 17.14 fixed-point real arithmetic.

   Implement here.  The standalone test in src/tests/fixed-point/ expects:

     typedef int fixed_point_t;
     #define FIXED_F  (1 << 14)          -- scaling factor, 16384

     int_to_fixed(int n)               -- n to fixed point
     fixed_to_int_trunc(fixed_point_t)  -- to int, round toward zero
     fixed_to_int_round(fixed_point_t)  -- to int, round to nearest
     fixed_add / fixed_sub              -- fixed +/- fixed
     fixed_add_int / fixed_sub_int      -- fixed +/- int
     fixed_mul / fixed_div              -- fixed * or / fixed
     fixed_mul_int / fixed_div_int      -- fixed * or / int

   Run: make -C src/tests/fixed-point check */

typedef int32_t fixed_point_t;

#define FIXED_F (1 << 14) // fixed point of 17.14

static inline fixed_point_t int_to_fixed(int n) {
  return n * FIXED_F;
}

static inline int fixed_to_int_trunc(fixed_point_t x) {
  return x / FIXED_F;
}

static inline int fixed_to_int_round(fixed_point_t x) {
  return x >= 0 ? (x + FIXED_F / 2) / FIXED_F
               : (x - FIXED_F / 2) / FIXED_F;
}

static inline fixed_point_t fixed_add(fixed_point_t x, fixed_point_t y) {
  return x + y;
}

static inline fixed_point_t fixed_sub(fixed_point_t x, fixed_point_t y) {
  return x - y;
}

static inline fixed_point_t fixed_add_int(fixed_point_t x, int n) {
  return x + n * FIXED_F;
}

static inline fixed_point_t fixed_sub_int(fixed_point_t x, int n) {
  return x - n * FIXED_F;
}

static inline fixed_point_t fixed_mul(fixed_point_t x, fixed_point_t y) {
  return ((int64_t) x) * y / FIXED_F;
}

static inline fixed_point_t fixed_div(fixed_point_t x, fixed_point_t y) {
  return ((int64_t) x) * FIXED_F / y;
}

static inline fixed_point_t fixed_mul_int(fixed_point_t x, int n) {
  return x * n;
}

static inline fixed_point_t fixed_div_int(fixed_point_t x, int n) {
  return x / n;
}


#endif /* threads/fixed-point.h */
