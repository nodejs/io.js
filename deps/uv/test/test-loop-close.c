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

static void timer_cb(uv_timer_t* handle) {
  ASSERT(handle);
  uv_stop(handle->loop);
}


TEST_IMPL(loop_close) {
  int r;
  uv_loop_t loop;

  loop.data = &loop;
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_PTR_EQ(loop.data, (void*) &loop);

  uv_timer_init(&loop, &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 100, 100);

  ASSERT_EQ(UV_EBUSY, uv_loop_close(&loop));

  uv_run(&loop, UV_RUN_DEFAULT);

  uv_close((uv_handle_t*) &timer_handle, NULL);
  r = uv_run(&loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_PTR_EQ(loop.data, (void*) &loop);
  ASSERT_OK(uv_loop_close(&loop));
  ASSERT_PTR_EQ(loop.data, (void*) &loop);

  return 0;
}

static void loop_instant_close_work_cb(uv_work_t* req) {
}

static void loop_instant_close_after_work_cb(uv_work_t* req, int status) {
}

/* It's impossible to properly cleanup after this test because loop can't be
 * closed while work has been queued. */
TEST_IMPL(loop_instant_close) {
  static uv_loop_t loop;
  static uv_work_t req;
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_queue_work(&loop,
                          &req,
                          loop_instant_close_work_cb,
                          loop_instant_close_after_work_cb));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
