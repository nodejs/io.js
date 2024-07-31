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
static int timer_called = 0;


static void timer_cb(uv_timer_t* handle) {
  ASSERT_PTR_EQ(handle, &timer_handle);
  timer_called = 1;
}


TEST_IMPL(run_nowait) {
  int r;
  uv_timer_init(uv_default_loop(), &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 100);

  r = uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  ASSERT(r);
  ASSERT_OK(timer_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
