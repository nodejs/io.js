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

static uv_udp_t udp;
static uv_tcp_t tcp;
static int close_cb_called;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void check_buffer_size(uv_handle_t* handle) {
  int value;

  value = 0;
  ASSERT_OK(uv_recv_buffer_size(handle, &value));
  ASSERT_GT(value, 0);

  value = 10000;
  ASSERT_OK(uv_recv_buffer_size(handle, &value));

  value = 0;
  ASSERT_OK(uv_recv_buffer_size(handle, &value));
  /* linux sets double the value */
  ASSERT(value == 10000 || value == 20000);
}


TEST_IMPL(socket_buffer_size) {
  struct sockaddr_in addr;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &tcp));
  ASSERT_OK(uv_tcp_bind(&tcp, (struct sockaddr*) &addr, 0));
  check_buffer_size((uv_handle_t*) &tcp);
  uv_close((uv_handle_t*) &tcp, close_cb);

  ASSERT_OK(uv_udp_init(uv_default_loop(), &udp));
  ASSERT_OK(uv_udp_bind(&udp, (struct sockaddr*) &addr, 0));
  check_buffer_size((uv_handle_t*) &udp);
  uv_close((uv_handle_t*) &udp, close_cb);

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
