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
#if defined(_WIN32)
# include "../src/win/winapi.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * Synthetic errors (errors that originate from within libuv, not the system)
 * should produce sensible error messages when run through uv_strerror().
 *
 * See https://github.com/joyent/libuv/issues/210
 */
TEST_IMPL(error_message) {
#if defined(__ASAN__)
  RETURN_SKIP("Test does not currently work in ASAN");
#endif
  char buf[32];

  /* Cop out. Can't do proper checks on systems with
   * i18n-ized error messages...
   */
  if (strcmp(uv_strerror(0), "Success") != 0) {
    printf("i18n error messages detected, skipping test.\n");
    return 0;
  }

  ASSERT_NULL(strstr(uv_strerror(UV_EINVAL), "Success"));
  ASSERT_OK(strcmp(uv_strerror(1337), "Unknown error"));
  ASSERT_OK(strcmp(uv_strerror(-1337), "Unknown error"));

  ASSERT_NULL(strstr(uv_strerror_r(UV_EINVAL, buf, sizeof(buf)), "Success"));
  ASSERT_NOT_NULL(strstr(uv_strerror_r(1337, buf, sizeof(buf)), "1337"));
  ASSERT_NOT_NULL(strstr(uv_strerror_r(-1337, buf, sizeof(buf)), "-1337"));

  return 0;
}


TEST_IMPL(sys_error) {
#if defined(_WIN32)
  ASSERT_EQ(uv_translate_sys_error(ERROR_NOACCESS), UV_EACCES);
  ASSERT_EQ(uv_translate_sys_error(ERROR_ELEVATION_REQUIRED), UV_EACCES);
  ASSERT_EQ(uv_translate_sys_error(WSAEADDRINUSE), UV_EADDRINUSE);
  ASSERT_EQ(uv_translate_sys_error(ERROR_BAD_PIPE), UV_EPIPE);
#else
  ASSERT_EQ(uv_translate_sys_error(EPERM), UV_EPERM);
  ASSERT_EQ(uv_translate_sys_error(EPIPE), UV_EPIPE);
  ASSERT_EQ(uv_translate_sys_error(EINVAL), UV_EINVAL);
#endif
  ASSERT_EQ(uv_translate_sys_error(UV_EINVAL), UV_EINVAL);
  ASSERT_EQ(uv_translate_sys_error(UV_ERANGE), UV_ERANGE);
  ASSERT_EQ(uv_translate_sys_error(UV_EACCES), UV_EACCES);
  ASSERT_OK(uv_translate_sys_error(0));

  return 0;
}
