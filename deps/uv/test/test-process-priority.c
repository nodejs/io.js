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


TEST_IMPL(process_priority) {
  int priority;
  int r;
  int i;

#if defined(__MVS__)
  if (uv_os_setpriority(0, 0) == UV_ENOSYS)
    RETURN_SKIP("functionality not supported on zOS");
#endif

  /* Verify that passing a NULL pointer returns UV_EINVAL. */
  r = uv_os_getpriority(0, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  /* Verify that all valid values work. */
  for (i = UV_PRIORITY_HIGHEST; i <= UV_PRIORITY_LOW; i++) {
    r = uv_os_setpriority(0, i);

    /* If UV_EACCES is returned, the current user doesn't have permission to
       set this specific priority. */
    if (r == UV_EACCES)
      continue;

    ASSERT_OK(r);
    ASSERT_OK(uv_os_getpriority(0, &priority));

    /* Verify that the priority values match on Unix, and are range mapped
       on Windows. */
#ifndef _WIN32
    ASSERT_EQ(priority, i);
#else
    /* On Windows, only elevated users can set UV_PRIORITY_HIGHEST. Other
       users will silently be set to UV_PRIORITY_HIGH. */
    if (i < UV_PRIORITY_HIGH)
      ASSERT(priority == UV_PRIORITY_HIGHEST || priority == UV_PRIORITY_HIGH);
    else if (i < UV_PRIORITY_ABOVE_NORMAL)
      ASSERT_EQ(priority, UV_PRIORITY_HIGH);
    else if (i < UV_PRIORITY_NORMAL)
      ASSERT_EQ(priority, UV_PRIORITY_ABOVE_NORMAL);
    else if (i < UV_PRIORITY_BELOW_NORMAL)
      ASSERT_EQ(priority, UV_PRIORITY_NORMAL);
    else if (i < UV_PRIORITY_LOW)
      ASSERT_EQ(priority, UV_PRIORITY_BELOW_NORMAL);
    else
      ASSERT_EQ(priority, UV_PRIORITY_LOW);
#endif

    /* Verify that the current PID and 0 are equivalent. */
    ASSERT_OK(uv_os_getpriority(uv_os_getpid(), &r));
    ASSERT_EQ(priority, r);
  }

  /* Verify that invalid priorities return UV_EINVAL. */
  ASSERT_EQ(uv_os_setpriority(0, UV_PRIORITY_HIGHEST - 1), UV_EINVAL);
  ASSERT_EQ(uv_os_setpriority(0, UV_PRIORITY_LOW + 1), UV_EINVAL);

  return 0;
}
