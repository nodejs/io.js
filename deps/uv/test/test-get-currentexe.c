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

#ifndef _WIN32
#include <unistd.h>
#endif

#define PATHMAX 4096
extern char executable_path[];

TEST_IMPL(get_currentexe) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif
#if defined(__OpenBSD__)
  RETURN_SKIP("Test does not currently work in OpenBSD");
#endif

  char buffer[PATHMAX];
  char path[PATHMAX];
  size_t size;
  char* match;
  int r;

  size = sizeof(buffer) / sizeof(buffer[0]);
  r = uv_exepath(buffer, &size);
  ASSERT(!r);

#ifdef _WIN32
  snprintf(path, sizeof(path), "%s", executable_path);
#else
  ASSERT_NOT_NULL(realpath(executable_path, path));
#endif

  match = strstr(buffer, path);
  /* Verify that the path returned from uv_exepath is a subdirectory of
   * executable_path.
   */
  ASSERT(match && !strcmp(match, path));
  ASSERT_EQ(size, strlen(buffer));

  /* Negative tests */
  size = sizeof(buffer) / sizeof(buffer[0]);
  r = uv_exepath(NULL, &size);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_exepath(buffer, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  size = 0;
  r = uv_exepath(buffer, &size);
  ASSERT_EQ(r, UV_EINVAL);

  memset(buffer, -1, sizeof(buffer));

  size = 1;
  r = uv_exepath(buffer, &size);
  ASSERT_OK(r);
  ASSERT_OK(size);
  ASSERT_EQ(buffer[0], '\0');

  memset(buffer, -1, sizeof(buffer));

  size = 2;
  r = uv_exepath(buffer, &size);
  ASSERT_OK(r);
  ASSERT_EQ(1, size);
  ASSERT_NE(buffer[0], '\0');
  ASSERT_EQ(buffer[1], '\0');

  /* Verify uv_exepath is not affected by uv_set_process_title(). */
  r = uv_set_process_title("foobar");
  ASSERT_OK(r);
  size = sizeof(buffer);
  r = uv_exepath(buffer, &size);
  ASSERT_OK(r);

  match = strstr(buffer, path);
  /* Verify that the path returned from uv_exepath is a subdirectory of
   * executable_path.
   */
  ASSERT_NOT_NULL(match);
  ASSERT_STR_EQ(match, path);
  ASSERT_EQ(size, strlen(buffer));
  return 0;
}
