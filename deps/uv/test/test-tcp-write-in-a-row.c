/* Copyright libuv project contributors. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "task.h"
#include "uv.h"

static uv_tcp_t server;
static uv_tcp_t client;
static uv_tcp_t incoming;
static int connect_cb_called;
static int close_cb_called;
static int connection_cb_called;
static int write_cb_called;
static uv_write_t small_write;
static uv_write_t big_write;

/* 10 MB, which is large than the send buffer size and the recv buffer */
static char data[1024 * 1024 * 10];

static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void write_cb(uv_write_t* w, int status) {
  /* the small write should finish immediately after the big write */
  ASSERT_OK(uv_stream_get_write_queue_size((uv_stream_t*) &client));

  write_cb_called++;

  if (write_cb_called == 2) {
    /* we are done */
    uv_close((uv_handle_t*) &client, close_cb);
    uv_close((uv_handle_t*) &incoming, close_cb);
    uv_close((uv_handle_t*) &server, close_cb);
  }
}

static void connect_cb(uv_connect_t* _, int status) {
  int r;
  uv_buf_t buf;
  size_t write_queue_size0, write_queue_size1;

  ASSERT_OK(status);
  connect_cb_called++;

  /* fire a big write */
  buf = uv_buf_init(data, sizeof(data));
  r = uv_write(&small_write, (uv_stream_t*) &client, &buf, 1, write_cb);
  ASSERT_OK(r);

  /* check that the write process gets stuck */
  write_queue_size0 = uv_stream_get_write_queue_size((uv_stream_t*) &client);
  ASSERT_GT(write_queue_size0, 0);

  /* fire a small write, which should be queued */
  buf = uv_buf_init("A", 1);
  r = uv_write(&big_write, (uv_stream_t*) &client, &buf, 1, write_cb);
  ASSERT_OK(r);

  write_queue_size1 = uv_stream_get_write_queue_size((uv_stream_t*) &client);
  ASSERT_EQ(write_queue_size1, write_queue_size0 + 1);
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char base[1024];

  buf->base = base;
  buf->len = sizeof(base);
}

static void read_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {}

static void connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);
  connection_cb_called++;

  ASSERT_OK(uv_tcp_init(tcp->loop, &incoming));
  ASSERT_OK(uv_accept(tcp, (uv_stream_t*) &incoming));
  ASSERT_OK(uv_read_start((uv_stream_t*) &incoming, alloc_cb, read_cb));
}

static void start_server(void) {
  struct sockaddr_in addr;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &server));
  ASSERT_OK(uv_tcp_bind(&server, (struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 128, connection_cb));
}

TEST_IMPL(tcp_write_in_a_row) {
#if defined(_WIN32)
  RETURN_SKIP("tcp_write_in_a_row does not work on Windows");
#elif defined(__PASE__)
  RETURN_SKIP("tcp_write_in_a_row does not work on IBM i PASE");
#else
  uv_connect_t connect_req;
  struct sockaddr_in addr;

  start_server();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client,
                           (struct sockaddr*) &addr,
                           connect_cb));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(3, close_cb_called);
  ASSERT_EQ(1, connection_cb_called);
  ASSERT_EQ(2, write_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}
