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

/*
 * These tests verify that the uv_shutdown callback is always made, even when
 * it is immediately followed by an uv_close call.
 */

#include "uv.h"
#include "task.h"


static uv_shutdown_t shutdown_req;
static uv_connect_t connect_req;

static int connect_cb_called = 0;
static int shutdown_cb_called = 0;
static int close_cb_called = 0;


static void shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT_PTR_EQ(req, &shutdown_req);
  ASSERT(status == 0 || status == UV_ECANCELED);
  shutdown_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void connect_cb(uv_connect_t* req, int status) {
  int r;

  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);

  r = uv_shutdown(&shutdown_req, req->handle, shutdown_cb);
  ASSERT_OK(r);
  ASSERT_OK(uv_is_closing((uv_handle_t*) req->handle));
  uv_close((uv_handle_t*) req->handle, close_cb);
  ASSERT_EQ(1, uv_is_closing((uv_handle_t*) req->handle));

  connect_cb_called++;
}


TEST_IMPL(shutdown_close_tcp) {
  struct sockaddr_in addr;
  uv_tcp_t h;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  r = uv_tcp_init(uv_default_loop(), &h);
  ASSERT_OK(r);
  r = uv_tcp_connect(&connect_req,
                     &h,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);
  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(shutdown_close_pipe) {
  uv_pipe_t h;
  int r;

  r = uv_pipe_init(uv_default_loop(), &h, 0);
  ASSERT_OK(r);
  uv_pipe_connect(&connect_req, &h, TEST_PIPENAME, connect_cb);
  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
