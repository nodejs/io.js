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

static uv_tcp_t server;
static uv_tcp_t connection;
static int read_cb_called = 0;

static uv_tcp_t client;
static uv_connect_t connect_req;


static void on_read2(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

static void on_write_close_immediately(uv_write_t* req, int status) {
  ASSERT_OK(status);

  uv_close((uv_handle_t*)req->handle, NULL); /* Close immediately */
  free(req);
}

static void on_write(uv_write_t* req, int status) {
  ASSERT_OK(status);

  free(req);
}

static void do_write(uv_stream_t* stream, uv_write_cb cb) {
  uv_write_t* req = malloc(sizeof(*req));
  uv_buf_t buf;
  buf.base = "1234578";
  buf.len = 8;
  ASSERT_OK(uv_write(req, stream, &buf, 1, cb));
}

static void on_alloc(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  buf->base = slab;
  buf->len = sizeof(slab);
}

static void on_read1(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_GE(nread, 0);

  /* Do write on a half open connection to force WSAECONNABORTED (on Windows)
   * in the subsequent uv_read_start()
   */
  do_write(stream, on_write);

  ASSERT_OK(uv_read_stop(stream));

  ASSERT_OK(uv_read_start(stream, on_alloc, on_read2));

  read_cb_called++;
}

static void on_read2(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_LT(nread, 0);

  uv_close((uv_handle_t*)stream, NULL);
  uv_close((uv_handle_t*)&server, NULL);

  read_cb_called++;
}

static void on_connection(uv_stream_t* server, int status) {
  ASSERT_OK(status);

  ASSERT_OK(uv_tcp_init(server->loop, &connection));

  ASSERT_OK(uv_accept(server, (uv_stream_t* )&connection));

  ASSERT_OK(uv_read_start((uv_stream_t*)&connection, on_alloc, on_read1));
}

static void on_connect(uv_connect_t* req, int status) {
  ASSERT_OK(status);

  do_write((uv_stream_t*)&client, on_write_close_immediately);
}

TEST_IMPL(tcp_read_stop_start) {
  uv_loop_t* loop = uv_default_loop();

  { /* Server */
    struct sockaddr_in addr;

    ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

    ASSERT_OK(uv_tcp_init(loop, &server));

    ASSERT_OK(uv_tcp_bind(&server, (struct sockaddr*) & addr, 0));

    ASSERT_OK(uv_listen((uv_stream_t*)&server, 10, on_connection));
  }

  { /* Client */
    struct sockaddr_in addr;

    ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

    ASSERT_OK(uv_tcp_init(loop, &client));

    ASSERT_OK(uv_tcp_connect(&connect_req, &client,
                             (const struct sockaddr*) & addr, on_connect));
  }

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_GE(read_cb_called, 2);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
