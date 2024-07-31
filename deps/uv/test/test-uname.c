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

#ifndef _WIN32
# include <sys/utsname.h>
#endif

TEST_IMPL(uname) {
#ifndef _WIN32
  struct utsname buf;
#endif
#ifdef _AIX
  char temp[256];
#endif
  uv_utsname_t buffer;
  int r;

  /* Verify that NULL is handled properly. */
  r = uv_os_uname(NULL);
  ASSERT_EQ(r, UV_EINVAL);

  /* Verify the happy path. */
  r = uv_os_uname(&buffer);
  ASSERT_OK(r);

#ifndef _WIN32
  ASSERT_NE(uname(&buf), -1);
  ASSERT_OK(strcmp(buffer.sysname, buf.sysname));
  ASSERT_OK(strcmp(buffer.version, buf.version));

# ifdef _AIX
  snprintf(temp, sizeof(temp), "%s.%s", buf.version, buf.release);
  ASSERT_OK(strcmp(buffer.release, temp));
# else
  ASSERT_OK(strcmp(buffer.release, buf.release));
# endif /* _AIX */

# if defined(_AIX) || defined(__PASE__)
  ASSERT_OK(strcmp(buffer.machine, "ppc64"));
# else
  ASSERT_OK(strcmp(buffer.machine, buf.machine));
# endif /* defined(_AIX) || defined(__PASE__) */

#endif /* _WIN32 */

  return 0;
}
