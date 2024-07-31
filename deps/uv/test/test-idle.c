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


static uv_idle_t idle_handle;
static uv_check_t check_handle;
static uv_timer_t timer_handle;

static int idle_cb_called = 0;
static int check_cb_called = 0;
static int timer_cb_called = 0;
static int close_cb_called = 0;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void timer_cb(uv_timer_t* handle) {
  ASSERT_PTR_EQ(handle, &timer_handle);

  uv_close((uv_handle_t*) &idle_handle, close_cb);
  uv_close((uv_handle_t*) &check_handle, close_cb);
  uv_close((uv_handle_t*) &timer_handle, close_cb);

  timer_cb_called++;
  fprintf(stderr, "timer_cb %d\n", timer_cb_called);
  fflush(stderr);
}


static void idle_cb(uv_idle_t* handle) {
  ASSERT_PTR_EQ(handle, &idle_handle);

  idle_cb_called++;
  fprintf(stderr, "idle_cb %d\n", idle_cb_called);
  fflush(stderr);
}


static void check_cb(uv_check_t* handle) {
  ASSERT_PTR_EQ(handle, &check_handle);

  check_cb_called++;
  fprintf(stderr, "check_cb %d\n", check_cb_called);
  fflush(stderr);
}


TEST_IMPL(idle_starvation) {
  int r;

  r = uv_idle_init(uv_default_loop(), &idle_handle);
  ASSERT_OK(r);
  r = uv_idle_start(&idle_handle, idle_cb);
  ASSERT_OK(r);

  r = uv_check_init(uv_default_loop(), &check_handle);
  ASSERT_OK(r);
  r = uv_check_start(&check_handle, check_cb);
  ASSERT_OK(r);

  r = uv_timer_init(uv_default_loop(), &timer_handle);
  ASSERT_OK(r);
  r = uv_timer_start(&timer_handle, timer_cb, 50, 0);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_GT(idle_cb_called, 0);
  ASSERT_EQ(1, timer_cb_called);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void idle_stop(uv_idle_t* handle) {
  uv_idle_stop(handle);
}


TEST_IMPL(idle_check) {
  ASSERT_OK(uv_idle_init(uv_default_loop(), &idle_handle));
  ASSERT_OK(uv_idle_start(&idle_handle, idle_stop));

  ASSERT_OK(uv_check_init(uv_default_loop(), &check_handle));
  ASSERT_OK(uv_check_start(&check_handle, check_cb));

  ASSERT_EQ(1, uv_run(uv_default_loop(), UV_RUN_ONCE));
  ASSERT_EQ(1, check_cb_called);

  ASSERT_OK(close_cb_called);
  uv_close((uv_handle_t*) &idle_handle, close_cb);
  uv_close((uv_handle_t*) &check_handle, close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_ONCE));
  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
