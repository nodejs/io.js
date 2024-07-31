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

static int connect_cb_called;
static int write_cb_called;
static int close_cb_called;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_LT(status, 0);
  connect_cb_called++;
  uv_close((uv_handle_t*)req->handle, close_cb);
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_LT(status, 0);
  write_cb_called++;
}


/*
 * Try to connect to an address on which nothing listens, get ECONNREFUSED
 * (uv errno 12) and get connect_cb() called once with status != 0.
 * Related issue: https://github.com/joyent/libuv/issues/443
 */
TEST_IMPL(tcp_connect_error_after_write) {
#ifdef _WIN32
  RETURN_SKIP("This test is disabled on Windows for now. "
              "See https://github.com/joyent/libuv/issues/444\n");
#else

  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_write_t write_req;
  uv_tcp_t conn;
  uv_buf_t buf;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  buf = uv_buf_init("TEST", 4);

  r = uv_tcp_init(uv_default_loop(), &conn);
  ASSERT_OK(r);

  r = uv_write(&write_req, (uv_stream_t*)&conn, &buf, 1, write_cb);
  ASSERT_EQ(r, UV_EBADF);

  r = uv_tcp_connect(&connect_req,
                     &conn,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

  r = uv_write(&write_req, (uv_stream_t*)&conn, &buf, 1, write_cb);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}
