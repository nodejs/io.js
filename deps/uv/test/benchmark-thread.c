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

#define NUM_THREADS (20 * 1000)

static volatile int num_threads;


static void thread_entry(void* arg) {
  ASSERT_PTR_EQ(arg, (void *) 42);
  num_threads++;
  /* FIXME write barrier? */
}


BENCHMARK_IMPL(thread_create) {
  uint64_t start_time;
  double duration;
  uv_thread_t tid;
  int i, r;

  start_time = uv_hrtime();

  for (i = 0; i < NUM_THREADS; i++) {
    r = uv_thread_create(&tid, thread_entry, (void *) 42);
    ASSERT_OK(r);

    r = uv_thread_join(&tid);
    ASSERT_OK(r);
  }

  duration = (uv_hrtime() - start_time) / 1e9;

  ASSERT_EQ(num_threads, NUM_THREADS);

  printf("%d threads created in %.2f seconds (%.0f/s)\n",
      NUM_THREADS, duration, NUM_THREADS / duration);

  return 0;
}
