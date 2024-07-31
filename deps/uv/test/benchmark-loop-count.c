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

#include <stdio.h>
#include <stdlib.h>

#define NUM_TICKS (2 * 1000 * 1000)
#define NUM_TICKS2 (2 * 1000 * 1000 * 100)

static unsigned long ticks;
static uv_idle_t idle_handle;
static uv_timer_t timer_handle;


static void idle_cb(uv_idle_t* handle) {
  if (++ticks == NUM_TICKS)
    uv_idle_stop(handle);
}

static void idle_alive_cb(uv_idle_t* handle) {
  int ticks = 0;

  while (++ticks < NUM_TICKS2) {
    int r = uv_loop_alive(handle->loop);
    if (r == 0)
      abort();
  }

  *(int*)handle->data = ticks;
  uv_idle_stop(handle);
}


static void idle2_cb(uv_idle_t* handle) {
  ticks++;
}


static void timer_cb(uv_timer_t* handle) {
  uv_idle_stop(&idle_handle);
  uv_timer_stop(&timer_handle);
}


BENCHMARK_IMPL(loop_count) {
  uv_loop_t* loop = uv_default_loop();
  uint64_t ns;

  uv_idle_init(loop, &idle_handle);
  uv_idle_start(&idle_handle, idle_cb);

  ns = uv_hrtime();
  uv_run(loop, UV_RUN_DEFAULT);
  ns = uv_hrtime() - ns;

  ASSERT_UINT64_EQ(ticks, NUM_TICKS);

  fprintf(stderr, "loop_count: %d ticks in %.2fs (%.0f/s)\n",
          NUM_TICKS,
          ns / 1e9,
          NUM_TICKS / (ns / 1e9));
  fflush(stderr);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


BENCHMARK_IMPL(loop_count_timed) {
  uv_loop_t* loop = uv_default_loop();

  uv_idle_init(loop, &idle_handle);
  uv_idle_start(&idle_handle, idle2_cb);

  uv_timer_init(loop, &timer_handle);
  uv_timer_start(&timer_handle, timer_cb, 5000, 0);

  uv_run(loop, UV_RUN_DEFAULT);

  fprintf(stderr, "loop_count: %lu ticks (%.0f ticks/s)\n", ticks, ticks / 5.0);
  fflush(stderr);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

/* Measure the performance of running uv_loop_alive(). Adding this so we can get
 * some sort of metric for the impact of switching active_reqs.count to use
 * atomics. No other code sits in a hot path. */
BENCHMARK_IMPL(loop_alive) {
  uv_loop_t* loop = uv_default_loop();
  int ticks = 0;
  uint64_t ns;

  uv_idle_init(loop, &idle_handle);
  idle_handle.data = &ticks;
  uv_idle_start(&idle_handle, idle_alive_cb);

  ns = uv_hrtime();
  uv_run(loop, UV_RUN_DEFAULT);
  ns = uv_hrtime() - ns;

  ASSERT_EQ(ticks, NUM_TICKS2);

  fprintf(stderr, "loop_alive: %d ticks in %.2fs (%.0f/s)\n",
          NUM_TICKS2,
          ns / 1e9,
          NUM_TICKS2 / (ns / 1e9));
  fflush(stderr);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
