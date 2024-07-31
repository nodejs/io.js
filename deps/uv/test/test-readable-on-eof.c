/* Copyright the libuv project contributors. All rights reserved.
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

static uv_loop_t loop;
static uv_tcp_t tcp_client;
static uv_connect_t connect_req;
static uv_write_t write_req;
static char close_me_cmd[] = {'Q', 'S'};

static int connect_cb_called;
static int read_cb_called;
static int write_cb_called;
static int close_cb_called;

static void write_cb(uv_write_t* req, int status) {
  write_cb_called++;
  ASSERT_OK(status);
}

static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[64];
  buf->base = slab;
  buf->len = sizeof(slab);
}

static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  int r;

  ASSERT_EQ(nread, UV_EOF);
  ASSERT_EQ(1, uv_is_readable(handle));
  ASSERT_EQ(1, uv_is_writable(handle));

  if (++read_cb_called == 3) {
      uv_close((uv_handle_t*) handle, close_cb);
      ASSERT_OK(uv_is_readable(handle));
      ASSERT_OK(uv_is_writable(handle));
  } else {
      r = uv_read_start((uv_stream_t*) &tcp_client, alloc_cb, read_cb);
      ASSERT_OK(r);
  }
}

static void connect_cb(uv_connect_t* req, int status) {
  int r;
  uv_buf_t close_me;

  connect_cb_called++;
  ASSERT_OK(status);

  read_cb((uv_stream_t*) &tcp_client, UV_EOF, NULL);

  close_me = uv_buf_init(close_me_cmd, sizeof(close_me_cmd));

  r = uv_write(&write_req,
               (uv_stream_t*) &tcp_client,
               &close_me,
               1,
               write_cb);

  ASSERT_OK(r);
}

TEST_IMPL(readable_on_eof) {
  struct sockaddr_in sa;
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &sa));
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_tcp_init(&loop, &tcp_client));

  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &tcp_client,
                           (const struct sockaddr*) &sa,
                           connect_cb));

  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(3, read_cb_called);
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(&loop);
  return 0;
}
