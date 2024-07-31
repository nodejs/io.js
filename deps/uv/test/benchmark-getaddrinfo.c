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
#include <stdlib.h>

#define CONCURRENT_CALLS 10
#define TOTAL_CALLS 10000

static const char* name = "localhost";

static uv_loop_t* loop;

static uv_getaddrinfo_t handles[CONCURRENT_CALLS];

static int calls_initiated = 0;
static int calls_completed = 0;
static int64_t start_time;
static int64_t end_time;


static void getaddrinfo_initiate(uv_getaddrinfo_t* handle);


static void getaddrinfo_cb(uv_getaddrinfo_t* handle, int status,
    struct addrinfo* res) {
  ASSERT_OK(status);
  calls_completed++;
  if (calls_initiated < TOTAL_CALLS) {
    getaddrinfo_initiate(handle);
  }

  uv_freeaddrinfo(res);
}


static void getaddrinfo_initiate(uv_getaddrinfo_t* handle) {
  int r;

  calls_initiated++;

  r = uv_getaddrinfo(loop, handle, &getaddrinfo_cb, name, NULL, NULL);
  ASSERT_OK(r);
}


BENCHMARK_IMPL(getaddrinfo) {
  int i;

  loop = uv_default_loop();

  uv_update_time(loop);
  start_time = uv_now(loop);

  for (i = 0; i < CONCURRENT_CALLS; i++) {
    getaddrinfo_initiate(&handles[i]);
  }

  uv_run(loop, UV_RUN_DEFAULT);

  uv_update_time(loop);
  end_time = uv_now(loop);

  ASSERT_EQ(calls_initiated, TOTAL_CALLS);
  ASSERT_EQ(calls_completed, TOTAL_CALLS);

  fprintf(stderr, "getaddrinfo: %.0f req/s\n",
          (double) calls_completed / (double) (end_time - start_time) * 1000.0);
  fflush(stderr);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
