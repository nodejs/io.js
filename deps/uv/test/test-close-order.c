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

static int check_cb_called;
static int timer_cb_called;
static int close_cb_called;

static uv_check_t check_handle;
static uv_timer_t timer_handle1;
static uv_timer_t timer_handle2;


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


/* check_cb should run before any close_cb */
static void check_cb(uv_check_t* handle) {
  ASSERT_OK(check_cb_called);
  ASSERT_EQ(1, timer_cb_called);
  ASSERT_OK(close_cb_called);
  uv_close((uv_handle_t*) handle, close_cb);
  uv_close((uv_handle_t*) &timer_handle2, close_cb);
  check_cb_called++;
}


static void timer_cb(uv_timer_t* handle) {
  uv_close((uv_handle_t*) handle, close_cb);
  timer_cb_called++;
}


TEST_IMPL(close_order) {
  uv_loop_t* loop;
  loop = uv_default_loop();

  uv_check_init(loop, &check_handle);
  uv_check_start(&check_handle, check_cb);
  uv_timer_init(loop, &timer_handle1);
  uv_timer_start(&timer_handle1, timer_cb, 0, 0);
  uv_timer_init(loop, &timer_handle2);
  uv_timer_start(&timer_handle2, timer_cb, 100000, 0);

  ASSERT_OK(check_cb_called);
  ASSERT_OK(close_cb_called);
  ASSERT_OK(timer_cb_called);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(1, check_cb_called);
  ASSERT_EQ(3, close_cb_called);
  ASSERT_EQ(1, timer_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
