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
#ifndef _WIN32
# include <unistd.h>
#endif


static int connect_cb_called = 0;
static int write_cb_called = 0;
static int close_cb_called = 0;

static uv_connect_t connect_req;
static uv_write_t write_req;


static void close_socket(uv_tcp_t* sock) {
  uv_os_fd_t fd;
  int r;

  r = uv_fileno((uv_handle_t*)sock, &fd);
  ASSERT_OK(r);
#ifdef _WIN32
  r = closesocket((uv_os_sock_t)fd);
#else
  r = close(fd);
#endif
  ASSERT_OK(r);
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);

  ASSERT(status);
  fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
  write_cb_called++;

  uv_close((uv_handle_t*)(req->handle), close_cb);
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  uv_stream_t* stream;
  int r;

  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);

  stream = req->handle;
  connect_cb_called++;

  /* close the socket, the hard way */
  close_socket((uv_tcp_t*)stream);

  buf = uv_buf_init("hello\n", 6);
  r = uv_write(&write_req, stream, &buf, 1, write_cb);
  ASSERT_OK(r);
}


TEST_IMPL(tcp_write_fail) {
  struct sockaddr_in addr;
  uv_tcp_t client;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
