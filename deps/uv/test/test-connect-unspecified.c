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

static void connect_4(uv_connect_t* req, int status) {
  ASSERT_NE(status, UV_EADDRNOTAVAIL);
}

static void connect_6(uv_connect_t* req, int status) {
  ASSERT_NE(status, UV_EADDRNOTAVAIL);
}

TEST_IMPL(connect_unspecified) {
  uv_loop_t* loop;
  uv_tcp_t socket4;
  struct sockaddr_in addr4;
  uv_connect_t connect4;
  uv_tcp_t socket6;
  struct sockaddr_in6 addr6;
  uv_connect_t connect6;

  loop = uv_default_loop();

  ASSERT_OK(uv_tcp_init(loop, &socket4));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr4));
  ASSERT_OK(uv_tcp_connect(&connect4,
                           &socket4,
                           (const struct sockaddr*) &addr4,
                           connect_4));

  if (can_ipv6()) {
    ASSERT_OK(uv_tcp_init(loop, &socket6));
    ASSERT_OK(uv_ip6_addr("::", TEST_PORT, &addr6));
    ASSERT_OK(uv_tcp_connect(&connect6,
                             &socket6,
                             (const struct sockaddr*) &addr6,
                             connect_6));
  }

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
