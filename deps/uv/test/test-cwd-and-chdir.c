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
#include <string.h>

#define PATHMAX 4096

TEST_IMPL(cwd_and_chdir) {
  char buffer_orig[PATHMAX];
  char buffer_new[PATHMAX];
  size_t size1;
  size_t size2;
  int err;

  size1 = 1;
  err = uv_cwd(buffer_orig, &size1);
  ASSERT_EQ(err, UV_ENOBUFS);
  ASSERT_GT(size1, 1);

  size1 = sizeof buffer_orig;
  err = uv_cwd(buffer_orig, &size1);
  ASSERT_OK(err);
  ASSERT_GT(size1, 0);
  ASSERT_NE(buffer_orig[size1], '/');

  err = uv_chdir(buffer_orig);
  ASSERT_OK(err);

  size2 = sizeof buffer_new;
  err = uv_cwd(buffer_new, &size2);
  ASSERT_OK(err);

  ASSERT_EQ(size1, size2);
  ASSERT_OK(strcmp(buffer_orig, buffer_new));

  return 0;
}
