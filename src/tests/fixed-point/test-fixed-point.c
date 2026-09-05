/** Standalone unit test for threads/fixed-point.h.

   This is NOT part of the tests/threads kernel suite.  fixed-point.h is
   pure <stdint.h> arithmetic, so it is compiled and run directly on the
   host: `make -C src/tests/fixed-point check`.

   All expected values are precomputed for the 17.14 representation
   (FIXED_F == 16384). */

#include <stdint.h>
#include <stdio.h>
#include "threads/fixed-point.h"

static int total, failures;

#define CHECK(e)                                 \
  do {                                           \
    total++;                                     \
    if (e)                                       \
      printf ("  ok   %s\n", #e);                \
    else {                                       \
      printf ("  FAIL %s\n", #e);                \
      failures++;                                \
    }                                            \
  } while (0)

int
main (void)
{
  printf ("conversions\n");
  CHECK (int_to_fixed (0) == 0);
  CHECK (int_to_fixed (1) == 16384);
  CHECK (int_to_fixed (-3) == -49152);
  CHECK (fixed_to_int_trunc (int_to_fixed (5)) == 5);
  CHECK (fixed_to_int_trunc (int_to_fixed (1) + FIXED_F / 2) == 1);       /* 1.5 -> 1 */
  CHECK (fixed_to_int_trunc (-(int_to_fixed (1) + FIXED_F / 2)) == -1);   /* -1.5 -> -1 */
  CHECK (fixed_to_int_round (int_to_fixed (1) + FIXED_F / 2) == 2);       /* 1.5 -> 2 */
  CHECK (fixed_to_int_round (-(int_to_fixed (1) + FIXED_F / 2)) == -2);   /* -1.5 -> -2 */
  CHECK (fixed_to_int_round (int_to_fixed (1) + FIXED_F / 4) == 1);       /* 1.25 -> 1 */
  CHECK (fixed_to_int_round (int_to_fixed (1) + 3 * (FIXED_F / 4)) == 2); /* 1.75 -> 2 */

  printf ("add / subtract\n");
  CHECK (fixed_add (int_to_fixed (2), int_to_fixed (3)) == int_to_fixed (5));
  CHECK (fixed_sub (int_to_fixed (2), int_to_fixed (3)) == int_to_fixed (-1));
  CHECK (fixed_add_int (int_to_fixed (2), 3) == int_to_fixed (5));
  CHECK (fixed_sub_int (int_to_fixed (2), 5) == int_to_fixed (-3));

  printf ("multiply\n");
  CHECK (fixed_mul (FIXED_F / 2, FIXED_F / 2) == FIXED_F / 4);              /* 0.5*0.5 = 0.25 */
  CHECK (fixed_to_int_round (fixed_mul (int_to_fixed (6), int_to_fixed (7))) == 42);
  CHECK (fixed_mul_int (int_to_fixed (3), 4) == int_to_fixed (12));
  /* 4915200 * 4915200 ~= 2.4e13 overflows int32; needs the int64 cast. */
  CHECK (fixed_to_int_round (fixed_mul (int_to_fixed (300), int_to_fixed (300))) == 90000);

  printf ("divide\n");
  CHECK (fixed_div (int_to_fixed (1), int_to_fixed (2)) == FIXED_F / 2);
  CHECK (fixed_to_int_trunc (fixed_div (int_to_fixed (10), int_to_fixed (3))) == 3);
  CHECK (fixed_to_int_round (fixed_div (int_to_fixed (10), int_to_fixed (3))) == 3); /* 3.333 -> 3 */
  CHECK (fixed_to_int_round (fixed_div (int_to_fixed (7), int_to_fixed (2))) == 4);  /* 3.5 -> 4 */
  CHECK (fixed_div_int (int_to_fixed (10), 4) == 40960);        /* 2.5 * 16384 */
  /* 131072 * 16384 == 2^31 overflows signed int32; needs the int64 cast. */
  CHECK (fixed_div (int_to_fixed (8), int_to_fixed (2)) == int_to_fixed (4));

  printf ("mlfqs coefficients\n");
  CHECK (fixed_div (int_to_fixed (59), int_to_fixed (60)) == 16110); /* 59*16384/60 = 16110.93 */
  CHECK (fixed_div (int_to_fixed (1), int_to_fixed (60)) == 273);    /* 16384/60 = 273.06 */

  printf ("\n%d checks, %d failed\n", total, failures);
  return failures != 0;
}
