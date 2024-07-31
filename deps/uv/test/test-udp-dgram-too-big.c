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

#define CHECK_HANDLE(handle) \
  ASSERT_PTR_EQ((uv_udp_t*)(handle), &handle_)

#define CHECK_REQ(req) \
  ASSERT_PTR_EQ((req), &req_);

static uv_udp_t handle_;
static uv_udp_send_t req_;

static int send_cb_called;
static int close_cb_called;


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  close_cb_called++;
}


static void send_cb(uv_udp_send_t* req, int status) {
  CHECK_REQ(req);
  CHECK_HANDLE(req->handle);

  ASSERT_EQ(status, UV_EMSGSIZE);

  uv_close((uv_handle_t*)req->handle, close_cb);
  send_cb_called++;
}


TEST_IMPL(udp_dgram_too_big) {
  char dgram[65536]; /* 64K MTU is unlikely, even on localhost */
  struct sockaddr_in addr;
  uv_buf_t buf;
  int r;

  memset(dgram, 42, sizeof dgram); /* silence valgrind */

  r = uv_udp_init(uv_default_loop(), &handle_);
  ASSERT_OK(r);

  buf = uv_buf_init(dgram, sizeof dgram);
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_udp_send(&req_,
                  &handle_,
                  &buf,
                  1,
                  (const struct sockaddr*) &addr,
                  send_cb);
  ASSERT_OK(r);

  ASSERT_OK(close_cb_called);
  ASSERT_OK(send_cb_called);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, send_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
