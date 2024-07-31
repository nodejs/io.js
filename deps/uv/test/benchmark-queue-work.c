/* Copyright libuv contributors. All rights reserved.
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

#include "task.h"
#include "uv.h"

static int done = 0;
static unsigned events = 0;
static unsigned result;

static unsigned fastrand(void) {
  static unsigned g = 0;
  g = g * 214013 + 2531011;
  return g;
}

static void work_cb(uv_work_t* req) {
  req->data = &result;
  *(unsigned*)req->data = fastrand();
}

static void after_work_cb(uv_work_t* req, int status) {
  events++;
  if (!done)
    ASSERT_OK(uv_queue_work(req->loop, req, work_cb, after_work_cb));
}

static void timer_cb(uv_timer_t* handle) { done = 1; }

BENCHMARK_IMPL(queue_work) {
  char fmtbuf[2][32];
  uv_timer_t timer_handle;
  uv_work_t work;
  uv_loop_t* loop;
  int timeout;

  loop = uv_default_loop();
  timeout = 5000;

  ASSERT_OK(uv_timer_init(loop, &timer_handle));
  ASSERT_OK(uv_timer_start(&timer_handle, timer_cb, timeout, 0));

  ASSERT_OK(uv_queue_work(loop, &work, work_cb, after_work_cb));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  printf("%s async jobs in %.1f seconds (%s/s)\n",
         fmt(&fmtbuf[0], events),
         timeout / 1000.,
         fmt(&fmtbuf[1], events / (timeout / 1000.)));

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
