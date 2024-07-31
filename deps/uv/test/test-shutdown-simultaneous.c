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
#include <stdio.h>
#include <stdlib.h>

static uv_tcp_t tcp;
static uv_connect_t connect_req;
static uv_shutdown_t shutdown_req;
static uv_buf_t qbuf;
static int got_q;
static int got_eof;
static int called_connect_cb;
static int called_shutdown_cb;
static int called_tcp_close_cb;


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  buf->len = size;
}


static void shutdown_cb(uv_shutdown_t *req, int status) {
  ASSERT_PTR_EQ(req, &shutdown_req);

  ASSERT_EQ(1, called_connect_cb);
  ASSERT_OK(called_tcp_close_cb);
}


static void read_cb(uv_stream_t* t, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_PTR_EQ((uv_tcp_t*)t, &tcp);

  if (nread == 0) {
    free(buf->base);
    return;
  }

  if (!got_q) {
    ASSERT_EQ(4, nread);
    ASSERT_OK(got_eof);
    ASSERT_MEM_EQ(buf->base, "QQSS", 4);
    free(buf->base);
    got_q = 1;
    puts("got QQSS");
    /* Shutdown our end of the connection simultaneously */
    uv_shutdown(&shutdown_req, (uv_stream_t*) &tcp, shutdown_cb);
    called_shutdown_cb++;
  } else {
    ASSERT_EQ(nread, UV_EOF);
    if (buf->base) {
      free(buf->base);
    }
    got_eof = 1;
    puts("got EOF");
  }
}


static void connect_cb(uv_connect_t *req, int status) {
  ASSERT_OK(status);
  ASSERT_PTR_EQ(req, &connect_req);

  /* Start reading from our connection so we can receive the EOF.  */
  ASSERT_OK(uv_read_start((uv_stream_t*)&tcp, alloc_cb, read_cb));

  /* Check error handling. */
  ASSERT_EQ(UV_EALREADY, uv_read_start((uv_stream_t*)&tcp, alloc_cb, read_cb));
  ASSERT_EQ(UV_EINVAL, uv_read_start(NULL, alloc_cb, read_cb));
  ASSERT_EQ(UV_EINVAL, uv_read_start((uv_stream_t*)&tcp, NULL, read_cb));
  ASSERT_EQ(UV_EINVAL, uv_read_start((uv_stream_t*)&tcp, alloc_cb, NULL));

  /*
   * Write the letter 'Q' and 'QSS` to gracefully kill the echo-server. This
   * will not effect our connection.
   */
  ASSERT_EQ(qbuf.len, uv_try_write((uv_stream_t*) &tcp, &qbuf, 1));

  called_connect_cb++;
  ASSERT_OK(called_shutdown_cb);
}


/*
 * This test has a client which connects to the echo_server and immediately
 * issues a shutdown. We check that this does not cause libuv to hang.
 */
TEST_IMPL(shutdown_simultaneous) {
  struct sockaddr_in server_addr;
  int r;

  qbuf.base = "QQSS";
  qbuf.len = 4;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  r = uv_tcp_init(uv_default_loop(), &tcp);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &tcp,
                     (const struct sockaddr*) &server_addr,
                     connect_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, called_connect_cb);
  ASSERT_EQ(1, called_shutdown_cb);
  ASSERT_EQ(1, got_eof);
  ASSERT_EQ(1, got_q);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
