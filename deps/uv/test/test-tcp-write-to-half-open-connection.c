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

static void connection_cb(uv_stream_t* server, int status);
static void connect_cb(uv_connect_t* req, int status);
static void write_cb(uv_write_t* req, int status);
static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

static uv_tcp_t tcp_server;
static uv_tcp_t tcp_client;
static uv_tcp_t tcp_peer; /* client socket as accept()-ed by server */
static uv_connect_t connect_req;
static uv_write_t write_req;

static int write_cb_called;
static int read_cb_called;

static void connection_cb(uv_stream_t* server, int status) {
  int r;
  uv_buf_t buf;

  ASSERT_PTR_EQ(server, (uv_stream_t*)&tcp_server);
  ASSERT_OK(status);

  r = uv_tcp_init(server->loop, &tcp_peer);
  ASSERT_OK(r);

  r = uv_accept(server, (uv_stream_t*)&tcp_peer);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*)&tcp_peer, alloc_cb, read_cb);
  ASSERT_OK(r);

  buf.base = "hello\n";
  buf.len = 6;

  r = uv_write(&write_req, (uv_stream_t*)&tcp_peer, &buf, 1, write_cb);
  ASSERT_OK(r);
}


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[1024];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  if (nread < 0) {
    fprintf(stderr, "read_cb error: %s\n", uv_err_name(nread));
    ASSERT(nread == UV_ECONNRESET || nread == UV_EOF);

    uv_close((uv_handle_t*)&tcp_server, NULL);
    uv_close((uv_handle_t*)&tcp_peer, NULL);
  }

  read_cb_called++;
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);

  /* Close the client. */
  uv_close((uv_handle_t*)&tcp_client, NULL);
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  write_cb_called++;
}


TEST_IMPL(tcp_write_to_half_open_connection) {
  struct sockaddr_in addr;
  uv_loop_t* loop;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  loop = uv_default_loop();
  ASSERT_NOT_NULL(loop);

  r = uv_tcp_init(loop, &tcp_server);
  ASSERT_OK(r);

  r = uv_tcp_bind(&tcp_server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&tcp_server, 1, connection_cb);
  ASSERT_OK(r);

  r = uv_tcp_init(loop, &tcp_client);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &tcp_client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_GT(write_cb_called, 0);
  ASSERT_GT(read_cb_called, 0);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
