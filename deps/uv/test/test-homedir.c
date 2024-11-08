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

TEST_IMPL(homedir) {
  char homedir[PATHMAX];
  size_t len;
  int r;

  /* Test the normal case */
  len = sizeof homedir;
  homedir[0] = '\0';
  ASSERT_OK(strlen(homedir));
  r = uv_os_homedir(homedir, &len);
  ASSERT_OK(r);
  ASSERT_EQ(strlen(homedir), len);
  ASSERT_GT(len, 0);
  ASSERT_EQ(homedir[len], '\0');

#ifdef _WIN32
  if (len == 3 && homedir[1] == ':')
    ASSERT_EQ(homedir[2], '\\');
  else
    ASSERT_NE(homedir[len - 1], '\\');
#else
  if (len == 1)
    ASSERT_EQ(homedir[0], '/');
  else
    ASSERT_NE(homedir[len - 1], '/');
#endif

  /* Test the case where the buffer is too small */
  len = SMALLPATH;
  r = uv_os_homedir(homedir, &len);
  ASSERT_EQ(r, UV_ENOBUFS);
  ASSERT_GT(len, SMALLPATH);

  /* Test invalid inputs */
  r = uv_os_homedir(NULL, &len);
  ASSERT_EQ(r, UV_EINVAL);
  r = uv_os_homedir(homedir, NULL);
  ASSERT_EQ(r, UV_EINVAL);
  len = 0;
  r = uv_os_homedir(homedir, &len);
  ASSERT_EQ(r, UV_EINVAL);

#ifdef _WIN32
  /* Test empty environment variable */
  r = uv_os_setenv("USERPROFILE", "");
  ASSERT_EQ(r, 0);
  len = sizeof homedir;
  r = uv_os_homedir(homedir, &len);
  ASSERT_EQ(r, UV_ENOENT);
#endif

  return 0;
}
