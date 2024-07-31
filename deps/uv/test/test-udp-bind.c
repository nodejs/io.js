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
#include <stdlib.h>
#include <string.h>


TEST_IMPL(udp_bind) {
  struct sockaddr_in addr;
  uv_loop_t* loop;
  uv_udp_t h1, h2;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  loop = uv_default_loop();

  r = uv_udp_init(loop, &h1);
  ASSERT_OK(r);

  r = uv_udp_init(loop, &h2);
  ASSERT_OK(r);

  r = uv_udp_bind(&h1, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_udp_bind(&h2, (const struct sockaddr*) &addr, 0);
  ASSERT_EQ(r, UV_EADDRINUSE);

  uv_close((uv_handle_t*) &h1, NULL);
  uv_close((uv_handle_t*) &h2, NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(udp_bind_reuseaddr) {
  struct sockaddr_in addr;
  uv_loop_t* loop;
  uv_udp_t h1, h2;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  loop = uv_default_loop();

  r = uv_udp_init(loop, &h1);
  ASSERT_OK(r);

  r = uv_udp_init(loop, &h2);
  ASSERT_OK(r);

  r = uv_udp_bind(&h1, (const struct sockaddr*) &addr, UV_UDP_REUSEADDR);
  ASSERT_OK(r);

  r = uv_udp_bind(&h2, (const struct sockaddr*) &addr, UV_UDP_REUSEADDR);
  ASSERT_OK(r);

  uv_close((uv_handle_t*) &h1, NULL);
  uv_close((uv_handle_t*) &h2, NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
