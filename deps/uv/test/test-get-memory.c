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

TEST_IMPL(get_memory) {
  uint64_t free_mem = uv_get_free_memory();
  uint64_t total_mem = uv_get_total_memory();
  uint64_t constrained_mem = uv_get_constrained_memory();
  uint64_t available_mem = uv_get_available_memory();

  printf("free_mem=%llu, total_mem=%llu, constrained_mem=%llu, "
         "available_mem=%llu\n",
         (unsigned long long) free_mem,
         (unsigned long long) total_mem,
         (unsigned long long) constrained_mem,
         (unsigned long long) available_mem);

  ASSERT_GT(free_mem, 0);
  ASSERT_GT(total_mem, 0);
  /* On IBMi PASE, the amount of memory in use is always zero. */
#ifdef __PASE__
  ASSERT_EQ(total_mem, free_mem);
#else
  ASSERT_GT(total_mem, free_mem);
#endif
  ASSERT_LE(available_mem, total_mem);
  /* we'd really want to test if available <= free, but that is fragile:
   * with no limit set, get_available calls and returns get_free; so if
   * any memory was freed between our calls to get_free and get_available
   * we would fail such a test test (as observed on CI).
   */
  return 0;
}
