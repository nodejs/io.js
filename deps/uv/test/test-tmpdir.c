/* Copyright libuv project contributors. All rights reserved.
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
#define SMALLPATH 1

TEST_IMPL(tmpdir) {
  char tmpdir[PATHMAX];
  size_t len;
  char last;
  int r;

  /* Test the normal case */
  len = sizeof tmpdir;
  tmpdir[0] = '\0';

  ASSERT_OK(strlen(tmpdir));
  r = uv_os_tmpdir(tmpdir, &len);
  ASSERT_OK(r);
  ASSERT_EQ(strlen(tmpdir), len);
  ASSERT_GT(len, 0);
  ASSERT_EQ(tmpdir[len], '\0');

  if (len > 1) {
    last = tmpdir[len - 1];
#ifdef _WIN32
    ASSERT_NE(last, '\\');
#else
    ASSERT_NE(last, '/');
#endif
  }

  /* Test the case where the buffer is too small */
  len = SMALLPATH;
  r = uv_os_tmpdir(tmpdir, &len);
  ASSERT_EQ(r, UV_ENOBUFS);
  ASSERT_GT(len, SMALLPATH);

  /* Test invalid inputs */
  r = uv_os_tmpdir(NULL, &len);
  ASSERT_EQ(r, UV_EINVAL);
  r = uv_os_tmpdir(tmpdir, NULL);
  ASSERT_EQ(r, UV_EINVAL);
  len = 0;
  r = uv_os_tmpdir(tmpdir, &len);
  ASSERT_EQ(r, UV_EINVAL);

#ifdef _WIN32
  const char *name = "TMP";
  char tmpdir_win[] = "C:\\xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  r = uv_os_setenv(name, tmpdir_win);
  ASSERT_OK(r);
  char tmpdirx[PATHMAX];
  size_t lenx = sizeof tmpdirx;
  r = uv_os_tmpdir(tmpdirx, &lenx);
  ASSERT_OK(r);
#endif

  return 0;
}
