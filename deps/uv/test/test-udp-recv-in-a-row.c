/* Copyright The libuv project and contributors. All rights reserved.
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

static uv_udp_t server;
static uv_udp_t client;
static uv_check_t check_handle;
static uv_buf_t buf;
static struct sockaddr_in addr;
static char send_data[10];
static int check_cb_called;

#define N 5
static int recv_cnt;

static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[sizeof(send_data)];
  buf->base = slab;
  buf->len = sizeof(slab);
}

static void sv_recv_cb(uv_udp_t* handle,
                       ssize_t nread,
                       const uv_buf_t* rcvbuf,
                       const struct sockaddr* addr,
                       unsigned flags) {
  if (++ recv_cnt < N) {
    ASSERT_EQ(sizeof(send_data), nread);
  } else {
    ASSERT_OK(nread);
  }
}

static void check_cb(uv_check_t* handle) {
  ASSERT_PTR_EQ(&check_handle, handle);

  /**
   * sv_recv_cb() is called with nread set to zero to indicate
   * there is no more udp packet in the kernel, so the actual
   * recv_cnt is one larger than N.
   */
  ASSERT_EQ(N+1, recv_cnt);
  check_cb_called = 1;

  /* we are done */
  ASSERT_OK(uv_check_stop(handle));
  uv_close((uv_handle_t*) &client, NULL);
  uv_close((uv_handle_t*) &check_handle, NULL);
  uv_close((uv_handle_t*) &server, NULL);
}


TEST_IMPL(udp_recv_in_a_row) {
  int i, r;
  
  ASSERT_OK(uv_check_init(uv_default_loop(), &check_handle));
  ASSERT_OK(uv_check_start(&check_handle, check_cb));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_udp_init(uv_default_loop(), &server));
  ASSERT_OK(uv_udp_bind(&server, (const struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_udp_recv_start(&server, alloc_cb, sv_recv_cb));

  ASSERT_OK(uv_udp_init(uv_default_loop(), &client));

  /* send N-1 udp packets */
  buf = uv_buf_init(send_data, sizeof(send_data));
  for (i = 0; i < N - 1; i ++) {
    r = uv_udp_try_send(&client,
                        &buf,
                        1,
                        (const struct sockaddr*) &addr);
    ASSERT_EQ(sizeof(send_data), r);
  }

  /* send an empty udp packet */
  buf = uv_buf_init(NULL, 0);
  r = uv_udp_try_send(&client,
                      &buf,
                      1,
                      (const struct sockaddr*) &addr);
  ASSERT_OK(r);

  /* check_cb() asserts that the N packets can be received
   * before it gets called. 
   */

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT(check_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
