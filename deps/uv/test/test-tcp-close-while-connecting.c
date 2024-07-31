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

static uv_timer_t timer1_handle;
static uv_timer_t timer2_handle;
static uv_tcp_t tcp_handle;

static int connect_cb_called;
static int timer1_cb_called;
static int close_cb_called;
static int netunreach_errors;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void connect_cb(uv_connect_t* req, int status) {
  /* The expected error is UV_ECANCELED but the test tries to connect to what
   * is basically an arbitrary address in the expectation that no network path
   * exists, so UV_ENETUNREACH is an equally plausible outcome.
   */
  ASSERT(status == UV_ECANCELED || status == UV_ENETUNREACH);
  uv_timer_stop(&timer2_handle);
  connect_cb_called++;
  if (status == UV_ENETUNREACH)
    netunreach_errors++;
}


static void timer1_cb(uv_timer_t* handle) {
  uv_close((uv_handle_t*)handle, close_cb);
  uv_close((uv_handle_t*)&tcp_handle, close_cb);
  timer1_cb_called++;
}


static void timer2_cb(uv_timer_t* handle) {
  ASSERT(0 && "should not be called");
}


TEST_IMPL(tcp_close_while_connecting) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();
  ASSERT_OK(uv_ip4_addr("1.2.3.4", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &tcp_handle));
  r = uv_tcp_connect(&connect_req,
                     &tcp_handle,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  if (r == UV_ENETUNREACH)
    RETURN_SKIP("Network unreachable.");
  ASSERT_OK(r);
  ASSERT_OK(uv_timer_init(loop, &timer1_handle));
  ASSERT_OK(uv_timer_start(&timer1_handle, timer1_cb, 1, 0));
  ASSERT_OK(uv_timer_init(loop, &timer2_handle));
  ASSERT_OK(uv_timer_start(&timer2_handle, timer2_cb, 86400 * 1000, 0));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, timer1_cb_called);
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);

  if (netunreach_errors > 0)
    RETURN_SKIP("Network unreachable.");

  return 0;
}
