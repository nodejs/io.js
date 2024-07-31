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

#include "task.h"
#include "uv.h"

#define NUM_TIMERS (10 * 1000 * 1000)

static int timer_cb_called;
static int close_cb_called;


static void timer_cb(uv_timer_t* handle) {
  timer_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


BENCHMARK_IMPL(million_timers) {
  uv_timer_t* timers;
  uv_loop_t* loop;
  uint64_t before_all;
  uint64_t before_run;
  uint64_t after_run;
  uint64_t after_all;
  int timeout;
  int i;

  timers = malloc(NUM_TIMERS * sizeof(timers[0]));
  ASSERT_NOT_NULL(timers);

  loop = uv_default_loop();
  timeout = 0;

  before_all = uv_hrtime();
  for (i = 0; i < NUM_TIMERS; i++) {
    if (i % 1000 == 0) timeout++;
    ASSERT_OK(uv_timer_init(loop, timers + i));
    ASSERT_OK(uv_timer_start(timers + i, timer_cb, timeout, 0));
  }

  before_run = uv_hrtime();
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  after_run = uv_hrtime();

  for (i = 0; i < NUM_TIMERS; i++)
    uv_close((uv_handle_t*) (timers + i), close_cb);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  after_all = uv_hrtime();

  ASSERT_EQ(timer_cb_called, NUM_TIMERS);
  ASSERT_EQ(close_cb_called, NUM_TIMERS);
  free(timers);

  fprintf(stderr, "%.2f seconds total\n", (after_all - before_all) / 1e9);
  fprintf(stderr, "%.2f seconds init\n", (before_run - before_all) / 1e9);
  fprintf(stderr, "%.2f seconds dispatch\n", (after_run - before_run) / 1e9);
  fprintf(stderr, "%.2f seconds cleanup\n", (after_all - after_run) / 1e9);
  fflush(stderr);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
