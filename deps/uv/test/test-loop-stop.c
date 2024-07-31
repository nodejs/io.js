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

static uv_prepare_t prepare_handle;
static uv_timer_t timer_handle;
static int prepare_called = 0;
static int timer_called = 0;
static int num_ticks = 10;


static void prepare_cb(uv_prepare_t* handle) {
  ASSERT_PTR_EQ(handle, &prepare_handle);
  prepare_called++;
  if (prepare_called == num_ticks)
    uv_prepare_stop(handle);
}


static void timer_cb(uv_timer_t* handle) {
  ASSERT_PTR_EQ(handle, &timer_handle);
  timer_called++;
  if (timer_called == 1)
    uv_stop(uv_default_loop());
  else if (timer_called == num_ticks)
    uv_timer_stop(handle);
}


TEST_IMPL(loop_stop) {
  int r;
  uv_prepare_init(uv_default_loop(), &prepare_handle);
  uv_prepare_start(&prepare_handle, prepare_cb);
  uv_timer_init(uv_default_loop(), &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 100);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT(r);
  ASSERT_EQ(1, timer_called);

  r = uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  ASSERT(r);
  ASSERT_GT(prepare_called, 1);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);
  ASSERT_EQ(10, timer_called);
  ASSERT_EQ(10, prepare_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(loop_stop_before_run) {
  ASSERT_OK(uv_timer_init(uv_default_loop(), &timer_handle));
  ASSERT_OK(uv_timer_start(&timer_handle, (uv_timer_cb) abort, 0, 0));
  uv_stop(uv_default_loop());
  ASSERT_NE(0, uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
