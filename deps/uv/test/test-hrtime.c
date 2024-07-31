/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "task.h"

#ifndef MILLISEC
# define MILLISEC 1000
#endif

#ifndef NANOSEC
# define NANOSEC ((uint64_t) 1e9)
#endif


TEST_IMPL(hrtime) {
  uint64_t a, b, diff;
  int i = 75;
  while (i > 0) {
    a = uv_hrtime();
    uv_sleep(45);
    b = uv_hrtime();

    diff = b - a;

    /* The windows Sleep() function has only a resolution of 10-20 ms. Check
     * that the difference between the two hrtime values has a reasonable
     * lower bound.
     */
    ASSERT_UINT64_GT(diff, (uint64_t) 25 * NANOSEC / MILLISEC);
    --i;
  }
  return 0;
}


TEST_IMPL(clock_gettime) {
  uv_timespec64_t t;

  ASSERT_EQ(UV_EINVAL, uv_clock_gettime(1337, &t));
  ASSERT_EQ(UV_EFAULT, uv_clock_gettime(1337, NULL));
  ASSERT_OK(uv_clock_gettime(UV_CLOCK_MONOTONIC, &t));
  ASSERT_OK(uv_clock_gettime(UV_CLOCK_REALTIME, &t));
  ASSERT_GT(1682500000000ll, t.tv_sec);  /* 2023-04-26T09:06:40.000Z */

  return 0;
}
