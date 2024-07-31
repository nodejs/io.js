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

#define NUM_PINGS               (1000 * 1000)
#define ACCESS_ONCE(type, var)  (*(volatile type*) &(var))

static unsigned int callbacks;
static volatile int done;

static const char running[] = "running";
static const char stop[]    = "stop";
static const char stopped[] = "stopped";


static void async_cb(uv_async_t* handle) {
  if (++callbacks == NUM_PINGS) {
    /* Tell the pummel thread to stop. */
    ACCESS_ONCE(const char*, handle->data) = stop;

    /* Wait for the pummel thread to acknowledge that it has stoppped. */
    while (ACCESS_ONCE(const char*, handle->data) != stopped)
      uv_sleep(0);

    uv_close((uv_handle_t*) handle, NULL);
  }
}


static void pummel(void* arg) {
  uv_async_t* handle = (uv_async_t*) arg;

  while (ACCESS_ONCE(const char*, handle->data) == running)
    uv_async_send(handle);

  /* Acknowledge that we've seen handle->data change. */
  ACCESS_ONCE(const char*, handle->data) = stopped;
}


static int test_async_pummel(int nthreads) {
  char fmtbuf[2][32];
  uv_thread_t* tids;
  uv_async_t handle;
  uint64_t time;
  int i;

  tids = calloc(nthreads, sizeof(tids[0]));
  ASSERT_NOT_NULL(tids);

  ASSERT_OK(uv_async_init(uv_default_loop(), &handle, async_cb));
  ACCESS_ONCE(const char*, handle.data) = running;

  for (i = 0; i < nthreads; i++)
    ASSERT_OK(uv_thread_create(tids + i, pummel, &handle));

  time = uv_hrtime();

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  time = uv_hrtime() - time;
  done = 1;

  for (i = 0; i < nthreads; i++)
    ASSERT_OK(uv_thread_join(tids + i));

  printf("async_pummel_%d: %s callbacks in %.2f seconds (%s/sec)\n",
         nthreads,
         fmt(&fmtbuf[0], callbacks),
         time / 1e9,
         fmt(&fmtbuf[1], callbacks / (time / 1e9)));

  free(tids);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


BENCHMARK_IMPL(async_pummel_1) {
  return test_async_pummel(1);
}


BENCHMARK_IMPL(async_pummel_2) {
  return test_async_pummel(2);
}


BENCHMARK_IMPL(async_pummel_4) {
  return test_async_pummel(4);
}


BENCHMARK_IMPL(async_pummel_8) {
  return test_async_pummel(8);
}
