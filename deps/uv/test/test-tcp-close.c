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

#include <errno.h>
#include <string.h> /* memset */

#define NUM_WRITE_REQS 32

static uv_tcp_t tcp_handle;
static uv_connect_t connect_req;

static int write_cb_called;
static int close_cb_called;

static void connect_cb(uv_connect_t* req, int status);
static void write_cb(uv_write_t* req, int status);
static void close_cb(uv_handle_t* handle);


static void connect_cb(uv_connect_t* conn_req, int status) {
  uv_write_t* req;
  uv_buf_t buf;
  int i, r;

  buf = uv_buf_init("PING", 4);
  for (i = 0; i < NUM_WRITE_REQS; i++) {
    req = malloc(sizeof *req);
    ASSERT_NOT_NULL(req);

    r = uv_write(req, (uv_stream_t*)&tcp_handle, &buf, 1, write_cb);
    ASSERT_OK(r);
  }

  uv_close((uv_handle_t*)&tcp_handle, close_cb);
}


static void write_cb(uv_write_t* req, int status) {
  /* write callbacks should run before the close callback */
  ASSERT_OK(close_cb_called);
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*)&tcp_handle);
  write_cb_called++;
  free(req);
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_PTR_EQ(handle, (uv_handle_t*)&tcp_handle);
  close_cb_called++;
}


static void connection_cb(uv_stream_t* server, int status) {
  ASSERT_OK(status);
}


static void start_server(uv_loop_t* loop, uv_tcp_t* handle) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(loop, handle);
  ASSERT_OK(r);

  r = uv_tcp_bind(handle, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)handle, 128, connection_cb);
  ASSERT_OK(r);

  uv_unref((uv_handle_t*)handle);
}


/* Check that pending write requests have their callbacks
 * invoked when the handle is closed.
 */
TEST_IMPL(tcp_close) {
  struct sockaddr_in addr;
  uv_tcp_t tcp_server;
  uv_loop_t* loop;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  loop = uv_default_loop();

  /* We can't use the echo server, it doesn't handle ECONNRESET. */
  start_server(loop, &tcp_server);

  r = uv_tcp_init(loop, &tcp_handle);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &tcp_handle,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

  ASSERT_OK(write_cb_called);
  ASSERT_OK(close_cb_called);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  printf("%d of %d write reqs seen\n", write_cb_called, NUM_WRITE_REQS);

  ASSERT_EQ(write_cb_called, NUM_WRITE_REQS);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
