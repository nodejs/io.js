/* Copyright libuv project and contributors. All rights reserved.
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

static uv_tcp_t client;
static uv_tcp_t connection;
static uv_connect_t connect_req;
static uv_timer_t timer;

static int read_cb_called;
static int on_close_called;

static void on_connection(uv_stream_t* server, int status);

static void on_client_connect(uv_connect_t* req, int status);
static void on_client_alloc(uv_handle_t* handle,
                            size_t suggested_size,
                            uv_buf_t* buf);
static void on_client_read(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf);
static void on_client_timeout(uv_timer_t* handle);

static void on_close(uv_handle_t* handle);


static void on_client_connect(uv_connect_t* conn_req, int status) {
  int r;

  r = uv_read_start((uv_stream_t*) &client, on_client_alloc, on_client_read);
  ASSERT_OK(r);

  r = uv_timer_start(&timer, on_client_timeout, 1000, 0);
  ASSERT_OK(r);
}


static void on_client_alloc(uv_handle_t* handle,
                            size_t suggested_size,
                            uv_buf_t* buf) {
  static char slab[8];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void on_client_read(uv_stream_t* stream, ssize_t nread,
                           const uv_buf_t* buf) {
  ASSERT_LT(nread, 0);
  read_cb_called++;
}


static void on_client_timeout(uv_timer_t* handle) {
  ASSERT_PTR_EQ(handle, &timer);
  ASSERT_OK(read_cb_called);
  uv_read_stop((uv_stream_t*) &client);
  uv_close((uv_handle_t*) &client, on_close);
  uv_close((uv_handle_t*) &timer, on_close);
}


static void on_connection_alloc(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[8];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void on_connection_read(uv_stream_t* stream,
                               ssize_t nread,
                               const uv_buf_t* buf) {
  ASSERT_EQ(nread, UV_EOF);
  read_cb_called++;
  uv_close((uv_handle_t*) stream, on_close);
}


static void on_connection(uv_stream_t* server, int status) {
  int r;

  ASSERT_OK(status);
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &connection));

  r = uv_read_start((uv_stream_t*) &connection,
                    on_connection_alloc,
                    on_connection_read);
  ASSERT_OK(r);
}


static void on_close(uv_handle_t* handle) {
  ASSERT_NE(handle == (uv_handle_t*) &client ||
            handle == (uv_handle_t*) &connection ||
            handle == (uv_handle_t*) &timer, 0);
  on_close_called++;
}


static void start_server(uv_loop_t* loop, uv_tcp_t* handle) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(loop, handle);
  ASSERT_OK(r);

  r = uv_tcp_bind(handle, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*) handle, 128, on_connection);
  ASSERT_OK(r);

  uv_unref((uv_handle_t*) handle);
}


/* Check that pending write requests have their callbacks
 * invoked when the handle is closed.
 */
TEST_IMPL(tcp_close_after_read_timeout) {
  struct sockaddr_in addr;
  uv_tcp_t tcp_server;
  uv_loop_t* loop;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  loop = uv_default_loop();

  /* We can't use the echo server, it doesn't handle ECONNRESET. */
  start_server(loop, &tcp_server);

  r = uv_tcp_init(loop, &client);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &client,
                     (const struct sockaddr*) &addr,
                     on_client_connect);
  ASSERT_OK(r);

  r = uv_tcp_init(loop, &connection);
  ASSERT_OK(r);

  r = uv_timer_init(loop, &timer);
  ASSERT_OK(r);

  ASSERT_OK(read_cb_called);
  ASSERT_OK(on_close_called);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, read_cb_called);
  ASSERT_EQ(3, on_close_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
