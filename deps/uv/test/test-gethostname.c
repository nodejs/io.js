/* Copyright libuv contributors. All rights reserved.
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

TEST_IMPL(gethostname) {
  char buf[UV_MAXHOSTNAMESIZE];
  size_t size;
  size_t enobufs_size;
  int r;

  /* Reject invalid inputs */
  size = 1;
  r = uv_os_gethostname(NULL, &size);
  ASSERT_EQ(r, UV_EINVAL);
  r = uv_os_gethostname(buf, NULL);
  ASSERT_EQ(r, UV_EINVAL);
  size = 0;
  r = uv_os_gethostname(buf, &size);
  ASSERT_EQ(r, UV_EINVAL);

  /* Return UV_ENOBUFS if the buffer cannot hold the hostname */
  enobufs_size = 1;
  buf[0] = '\0';
  r = uv_os_gethostname(buf, &enobufs_size);
  ASSERT_EQ(r, UV_ENOBUFS);
  ASSERT_EQ(buf[0], '\0');
  ASSERT_GT(enobufs_size, 1);

  /* Successfully get the hostname */
  size = UV_MAXHOSTNAMESIZE;
  r = uv_os_gethostname(buf, &size);
  ASSERT_OK(r);
  ASSERT(size > 0 && size == strlen(buf));
  ASSERT_EQ(size + 1, enobufs_size);

  return 0;
}
