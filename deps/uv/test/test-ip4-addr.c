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

#include <stdio.h>
#include <string.h>


TEST_IMPL(ip4_addr) {
  struct sockaddr_in addr;
  char dst[16];

  ASSERT_OK(uv_inet_ntop(AF_INET, "\xFF\xFF\xFF\xFF", dst, sizeof(dst)));
  ASSERT_OK(strcmp(dst, "255.255.255.255"));
  ASSERT_EQ(UV_ENOSPC, uv_inet_ntop(AF_INET, "\xFF\xFF\xFF\xFF",
                                    dst, sizeof(dst) - 1));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_ip4_addr("255.255.255.255", TEST_PORT, &addr));
  ASSERT_EQ(UV_EINVAL, uv_ip4_addr("255.255.255*000", TEST_PORT, &addr));
  ASSERT_EQ(UV_EINVAL, uv_ip4_addr("255.255.255.256", TEST_PORT, &addr));
  ASSERT_EQ(UV_EINVAL, uv_ip4_addr("2555.0.0.0", TEST_PORT, &addr));
  ASSERT_EQ(UV_EINVAL, uv_ip4_addr("255", TEST_PORT, &addr));

#ifdef SIN6_LEN
  ASSERT_EQ(addr.sin_len, sizeof(addr));
#endif

  /* for broken address family */
  ASSERT_EQ(UV_EAFNOSUPPORT, uv_inet_pton(42, "127.0.0.1",
                                          &addr.sin_addr.s_addr));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
