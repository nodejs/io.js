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

#include <stdio.h>
#include <stdlib.h>


static int close_cb_called = 0;


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void timer_cb(uv_timer_t* handle) {
  ASSERT(0 && "timer_cb should not have been called");
}


TEST_IMPL(active) {
  int r;
  uv_timer_t timer;

  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  /* uv_is_active() and uv_is_closing() should always return either 0 or 1. */
  ASSERT_OK(uv_is_active((uv_handle_t*) &timer));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &timer));

  r = uv_timer_start(&timer, timer_cb, 1000, 0);
  ASSERT_OK(r);

  ASSERT_EQ(1, uv_is_active((uv_handle_t*) &timer));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &timer));

  r = uv_timer_stop(&timer);
  ASSERT_OK(r);

  ASSERT_OK(uv_is_active((uv_handle_t*) &timer));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &timer));

  r = uv_timer_start(&timer, timer_cb, 1000, 0);
  ASSERT_OK(r);

  ASSERT_EQ(1, uv_is_active((uv_handle_t*) &timer));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &timer));

  uv_close((uv_handle_t*) &timer, close_cb);

  ASSERT_OK(uv_is_active((uv_handle_t*) &timer));
  ASSERT_EQ(1, uv_is_closing((uv_handle_t*) &timer));

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
