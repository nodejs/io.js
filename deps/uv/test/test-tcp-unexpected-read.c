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

static uv_check_t check_handle;
static uv_timer_t timer_handle;
static uv_tcp_t server_handle;
static uv_tcp_t client_handle;
static uv_tcp_t peer_handle;
static uv_write_t write_req;
static uv_connect_t connect_req;

static unsigned long ticks; /* event loop ticks */


static void check_cb(uv_check_t* handle) {
  ticks++;
}


static void timer_cb(uv_timer_t* handle) {
  uv_close((uv_handle_t*) &check_handle, NULL);
  uv_close((uv_handle_t*) &timer_handle, NULL);
  uv_close((uv_handle_t*) &server_handle, NULL);
  uv_close((uv_handle_t*) &client_handle, NULL);
  uv_close((uv_handle_t*) &peer_handle, NULL);
}


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  ASSERT(0 && "alloc_cb should not have been called");
}


static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  ASSERT(0 && "read_cb should not have been called");
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &client_handle);
  ASSERT_OK(status);
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &peer_handle);
  ASSERT_OK(status);
}


static void connection_cb(uv_stream_t* handle, int status) {
  uv_buf_t buf;

  buf = uv_buf_init("PING", 4);

  ASSERT_OK(status);
  ASSERT_OK(uv_accept(handle, (uv_stream_t*) &peer_handle));
  ASSERT_OK(uv_read_start((uv_stream_t*) &peer_handle, alloc_cb, read_cb));
  ASSERT_OK(uv_write(&write_req, (uv_stream_t*) &peer_handle,
                     &buf, 1, write_cb));
}


TEST_IMPL(tcp_unexpected_read) {
  struct sockaddr_in addr;
  uv_loop_t* loop;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  loop = uv_default_loop();

  ASSERT_OK(uv_timer_init(loop, &timer_handle));
  ASSERT_OK(uv_timer_start(&timer_handle, timer_cb, 1000, 0));
  ASSERT_OK(uv_check_init(loop, &check_handle));
  ASSERT_OK(uv_check_start(&check_handle, check_cb));
  ASSERT_OK(uv_tcp_init(loop, &server_handle));
  ASSERT_OK(uv_tcp_init(loop, &client_handle));
  ASSERT_OK(uv_tcp_init(loop, &peer_handle));
  ASSERT_OK(uv_tcp_bind(&server_handle, (const struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server_handle, 1, connection_cb));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client_handle,
                           (const struct sockaddr*) &addr,
                           connect_cb));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  /* This is somewhat inexact but the idea is that the event loop should not
   * start busy looping when the server sends a message and the client isn't
   * reading.
   */
  ASSERT_LE(ticks, 20);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
