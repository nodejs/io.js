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

#include "uv.h"
#include "task.h"

#include <errno.h>
#include <string.h> /* memset */

#ifdef _WIN32
# define INVALID_FD (INVALID_HANDLE_VALUE)
#else
# define INVALID_FD (-1)
#endif

static uv_loop_t* loop;
static uv_tcp_t tcp_server;
static uv_tcp_t tcp_client;
static uv_tcp_t tcp_accepted;
static uv_connect_t connect_req;
static uv_shutdown_t shutdown_req;
static uv_write_t write_reqs[4];

static int client_close;
static int shutdown_before_close;

static int write_cb_called;
static int close_cb_called;
static int shutdown_cb_called;

static void connect_cb(uv_connect_t* req, int status);
static void write_cb(uv_write_t* req, int status);
static void close_cb(uv_handle_t* handle);
static void shutdown_cb(uv_shutdown_t* req, int status);

static int read_size;


static void do_write(uv_tcp_t* handle) {
  uv_buf_t buf;
  unsigned i;
  int r;

  buf = uv_buf_init("PING", 4);
  for (i = 0; i < ARRAY_SIZE(write_reqs); i++) {
    r = uv_write(&write_reqs[i], (uv_stream_t*) handle, &buf, 1, write_cb);
    ASSERT_OK(r);
  }
}


static void do_close(uv_tcp_t* handle) {
  uv_os_fd_t fd;
  int r;

  if (shutdown_before_close == 1) {
    ASSERT_OK(uv_shutdown(&shutdown_req,
                          (uv_stream_t*) handle,
                          shutdown_cb));
    ASSERT_EQ(UV_EINVAL, uv_tcp_close_reset(handle, close_cb));
  } else if (shutdown_before_close == 2) {
    r = uv_fileno((const uv_handle_t*) handle, &fd);
    ASSERT_OK(r);
#ifdef _WIN32
    ASSERT_PTR_NE(fd, INVALID_FD);
    ASSERT_OK(shutdown((SOCKET)fd, SD_BOTH));
#else
    ASSERT_NE(fd, INVALID_FD);
    ASSERT_OK(shutdown(fd, SHUT_RDWR));
#endif
    ASSERT_OK(uv_tcp_close_reset(handle, close_cb));
  } else {
    ASSERT_OK(uv_tcp_close_reset(handle, close_cb));
    ASSERT_EQ(UV_ENOTCONN, uv_shutdown(&shutdown_req, (uv_stream_t*) handle,
              shutdown_cb));
  }

  uv_close((uv_handle_t*) &tcp_server, NULL);
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char slab[1024];
  buf->base = slab;
  buf->len = sizeof(slab);
}

static void read_cb2(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_PTR_EQ((uv_tcp_t*)stream, &tcp_client);
  if (nread == UV_EOF)
    uv_close((uv_handle_t*) stream, NULL);
}


static void connect_cb(uv_connect_t* conn_req, int status) {
  ASSERT_PTR_EQ(conn_req, &connect_req);
  uv_read_start((uv_stream_t*) &tcp_client, alloc_cb, read_cb2);
  do_write(&tcp_client);
  if (client_close)
    do_close(&tcp_client);
}


static void write_cb(uv_write_t* req, int status) {
  /* write callbacks should run before the close callback */
  ASSERT_OK(close_cb_called);
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*)&tcp_client);
  write_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  if (client_close)
    ASSERT_PTR_EQ(handle, (uv_handle_t*) &tcp_client);
  else
    ASSERT_PTR_EQ(handle, (uv_handle_t*) &tcp_accepted);

  close_cb_called++;
}

static void shutdown_cb(uv_shutdown_t* req, int status) {
  if (client_close)
    ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &tcp_client);
  else
    ASSERT_PTR_EQ(req->handle, (uv_stream_t*) &tcp_accepted);

  shutdown_cb_called++;
}


static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_PTR_EQ((uv_tcp_t*)stream, &tcp_accepted);
  if (nread < 0) {
    uv_close((uv_handle_t*) stream, NULL);
  } else {
    read_size += nread;
    if (read_size == 16 && client_close == 0)
      do_close(&tcp_accepted);
  }
}


static void connection_cb(uv_stream_t* server, int status) {
  ASSERT_OK(status);

  ASSERT_OK(uv_tcp_init(loop, &tcp_accepted));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) &tcp_accepted));

  uv_read_start((uv_stream_t*) &tcp_accepted, alloc_cb, read_cb);
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
}


static void do_connect(uv_loop_t* loop, uv_tcp_t* tcp_client) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(loop, tcp_client);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     tcp_client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);
}


/* Check that pending write requests have their callbacks
 * invoked when the handle is closed.
 */
TEST_IMPL(tcp_close_reset_client) {
  int r;

  loop = uv_default_loop();

  start_server(loop, &tcp_server);

  client_close = 1;
  shutdown_before_close = 0;

  do_connect(loop, &tcp_client);

  ASSERT_OK(write_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(4, write_cb_called);
  ASSERT_EQ(1, close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

TEST_IMPL(tcp_close_reset_client_after_shutdown) {
  int r;

  loop = uv_default_loop();

  start_server(loop, &tcp_server);

  client_close = 1;
  shutdown_before_close = 1;

  do_connect(loop, &tcp_client);

  ASSERT_OK(write_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(4, write_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

TEST_IMPL(tcp_close_reset_accepted) {
  int r;

  loop = uv_default_loop();

  start_server(loop, &tcp_server);

  client_close = 0;
  shutdown_before_close = 0;

  do_connect(loop, &tcp_client);

  ASSERT_OK(write_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(4, write_cb_called);
  ASSERT_EQ(1, close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

TEST_IMPL(tcp_close_reset_accepted_after_shutdown) {
  int r;

  loop = uv_default_loop();

  start_server(loop, &tcp_server);

  client_close = 0;
  shutdown_before_close = 1;

  do_connect(loop, &tcp_client);

  ASSERT_OK(write_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(4, write_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

TEST_IMPL(tcp_close_reset_accepted_after_socket_shutdown) {
  int r;

  loop = uv_default_loop();

  start_server(loop, &tcp_server);

  client_close = 0;
  shutdown_before_close = 2;

  do_connect(loop, &tcp_client);

  ASSERT_OK(write_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(4, write_cb_called);
  ASSERT_EQ(1, close_cb_called);
  ASSERT_OK(shutdown_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
