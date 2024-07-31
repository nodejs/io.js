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

static uv_shutdown_t shutdown_req;

static void close_cb(uv_handle_t* handle) {

}

static void shutdown_cb(uv_shutdown_t* req, int status) {
  uv_close((uv_handle_t*) req->handle, close_cb);
}

static void connect_cb(uv_connect_t* req, int status) {
  int r;
  ASSERT_OK(status);

  r = uv_shutdown(&shutdown_req, req->handle, shutdown_cb);
  ASSERT_OK(r);

  ASSERT_OK(uv_is_writable(req->handle));
}

TEST_IMPL(not_writable_after_shutdown) {
  int r;
  struct sockaddr_in addr;
  uv_loop_t* loop;
  uv_tcp_t socket;
  uv_connect_t connect_req;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  loop = uv_default_loop();

  r = uv_tcp_init(loop, &socket);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &socket,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
