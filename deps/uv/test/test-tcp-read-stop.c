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

static uv_timer_t timer_handle;
static uv_tcp_t tcp_handle;
static uv_write_t write_req;


static void fail_cb(void) {
  ASSERT(0 && "fail_cb called");
}


static void write_cb(uv_write_t* req, int status) {
  uv_close((uv_handle_t*) &timer_handle, NULL);
  uv_close((uv_handle_t*) &tcp_handle, NULL);
}


static void timer_cb(uv_timer_t* handle) {
  uv_buf_t buf = uv_buf_init("PING", 4);
  ASSERT_OK(uv_write(&write_req,
                     (uv_stream_t*) &tcp_handle,
                     &buf,
                     1,
                     write_cb));
  ASSERT_OK(uv_read_stop((uv_stream_t*) &tcp_handle));
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_timer_start(&timer_handle, timer_cb, 50, 0));
  ASSERT_OK(uv_read_start((uv_stream_t*) &tcp_handle,
                          (uv_alloc_cb) fail_cb,
                          (uv_read_cb) fail_cb));
}


TEST_IMPL(tcp_read_stop) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_timer_init(uv_default_loop(), &timer_handle));
  ASSERT_OK(uv_tcp_init(uv_default_loop(), &tcp_handle));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &tcp_handle,
                           (const struct sockaddr*) &addr,
                           connect_cb));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  MAKE_VALGRIND_HAPPY(uv_default_loop());

  return 0;
}
