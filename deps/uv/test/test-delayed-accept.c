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

static int connection_cb_called = 0;
static int do_accept_called = 0;
static int close_cb_called = 0;
static int connect_cb_called = 0;


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  buf->len = size;
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);

  free(handle);

  close_cb_called++;
}


static void do_accept(uv_timer_t* timer_handle) {
  uv_tcp_t* server;
  uv_tcp_t* accepted_handle = (uv_tcp_t*)malloc(sizeof *accepted_handle);
  int r;

  ASSERT_NOT_NULL(timer_handle);
  ASSERT_NOT_NULL(accepted_handle);

  r = uv_tcp_init(uv_default_loop(), accepted_handle);
  ASSERT_OK(r);

  server = (uv_tcp_t*)timer_handle->data;
  r = uv_accept((uv_stream_t*)server, (uv_stream_t*)accepted_handle);
  ASSERT_OK(r);

  do_accept_called++;

  /* Immediately close the accepted handle. */
  uv_close((uv_handle_t*)accepted_handle, close_cb);

  /* After accepting the two clients close the server handle */
  if (do_accept_called == 2) {
    uv_close((uv_handle_t*)server, close_cb);
  }

  /* Dispose the timer. */
  uv_close((uv_handle_t*)timer_handle, close_cb);
}


static void connection_cb(uv_stream_t* tcp, int status) {
  int r;
  uv_timer_t* timer_handle;

  ASSERT_OK(status);

  timer_handle = (uv_timer_t*)malloc(sizeof *timer_handle);
  ASSERT_NOT_NULL(timer_handle);

  /* Accept the client after 1 second */
  r = uv_timer_init(uv_default_loop(), timer_handle);
  ASSERT_OK(r);

  timer_handle->data = tcp;

  r = uv_timer_start(timer_handle, do_accept, 1000, 0);
  ASSERT_OK(r);

  connection_cb_called++;
}


static void start_server(void) {
  struct sockaddr_in addr;
  uv_tcp_t* server = (uv_tcp_t*)malloc(sizeof *server);
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_NOT_NULL(server);

  r = uv_tcp_init(uv_default_loop(), server);
  ASSERT_OK(r);
  r = uv_tcp_bind(server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)server, 128, connection_cb);
  ASSERT_OK(r);
}


static void read_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  /* The server will not send anything, it should close gracefully. */

  if (buf->base) {
    free(buf->base);
  }

  if (nread >= 0) {
    ASSERT_OK(nread);
  } else {
    ASSERT_NOT_NULL(tcp);
    ASSERT_EQ(nread, UV_EOF);
    uv_close((uv_handle_t*)tcp, close_cb);
  }
}


static void connect_cb(uv_connect_t* req, int status) {
  int r;

  ASSERT_NOT_NULL(req);
  ASSERT_OK(status);

  /* Not that the server will send anything, but otherwise we'll never know
   * when the server closes the connection. */
  r = uv_read_start((uv_stream_t*)(req->handle), alloc_cb, read_cb);
  ASSERT_OK(r);

  connect_cb_called++;

  free(req);
}


static void client_connect(void) {
  struct sockaddr_in addr;
  uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof *client);
  uv_connect_t* connect_req = malloc(sizeof *connect_req);
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_NOT_NULL(client);
  ASSERT_NOT_NULL(connect_req);

  r = uv_tcp_init(uv_default_loop(), client);
  ASSERT_OK(r);

  r = uv_tcp_connect(connect_req,
                     client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);
}



TEST_IMPL(delayed_accept) {
  start_server();

  client_connect();
  client_connect();

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, connection_cb_called);
  ASSERT_EQ(2, do_accept_called);
  ASSERT_EQ(2, connect_cb_called);
  ASSERT_EQ(7, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
