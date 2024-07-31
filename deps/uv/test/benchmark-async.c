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

#define NUM_PINGS (1000 * 1000)

struct ctx {
  uv_loop_t loop;
  uv_thread_t thread;
  uv_async_t main_async;    /* wake up main thread */
  uv_async_t worker_async;  /* wake up worker */
  unsigned int nthreads;
  unsigned int main_sent;
  unsigned int main_seen;
  unsigned int worker_sent;
  unsigned int worker_seen;
};


static void worker_async_cb(uv_async_t* handle) {
  struct ctx* ctx = container_of(handle, struct ctx, worker_async);

  ASSERT_OK(uv_async_send(&ctx->main_async));
  ctx->worker_sent++;
  ctx->worker_seen++;

  if (ctx->worker_sent >= NUM_PINGS)
    uv_close((uv_handle_t*) &ctx->worker_async, NULL);
}


static void main_async_cb(uv_async_t* handle) {
  struct ctx* ctx = container_of(handle, struct ctx, main_async);

  ASSERT_OK(uv_async_send(&ctx->worker_async));
  ctx->main_sent++;
  ctx->main_seen++;

  if (ctx->main_sent >= NUM_PINGS)
    uv_close((uv_handle_t*) &ctx->main_async, NULL);
}


static void worker(void* arg) {
  struct ctx* ctx = arg;
  ASSERT_OK(uv_async_send(&ctx->main_async));
  ASSERT_OK(uv_run(&ctx->loop, UV_RUN_DEFAULT));
  uv_loop_close(&ctx->loop);
}


static int test_async(int nthreads) {
  char fmtbuf[32];
  struct ctx* threads;
  struct ctx* ctx;
  uint64_t time;
  int i;

  threads = calloc(nthreads, sizeof(threads[0]));
  ASSERT_NOT_NULL(threads);

  for (i = 0; i < nthreads; i++) {
    ctx = threads + i;
    ctx->nthreads = nthreads;
    ASSERT_OK(uv_loop_init(&ctx->loop));
    ASSERT_OK(uv_async_init(&ctx->loop, &ctx->worker_async, worker_async_cb));
    ASSERT_OK(uv_async_init(uv_default_loop(),
                            &ctx->main_async,
                            main_async_cb));
    ASSERT_OK(uv_thread_create(&ctx->thread, worker, ctx));
  }

  time = uv_hrtime();

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  for (i = 0; i < nthreads; i++)
    ASSERT_OK(uv_thread_join(&threads[i].thread));

  time = uv_hrtime() - time;

  for (i = 0; i < nthreads; i++) {
    ctx = threads + i;
    ASSERT_EQ(ctx->worker_sent, NUM_PINGS);
    ASSERT_EQ(ctx->worker_seen, NUM_PINGS);
    ASSERT_EQ(ctx->main_sent, (unsigned int) NUM_PINGS);
    ASSERT_EQ(ctx->main_seen, (unsigned int) NUM_PINGS);
  }

  printf("async%d: %.2f sec (%s/sec)\n",
         nthreads,
         time / 1e9,
         fmt(&fmtbuf, NUM_PINGS / (time / 1e9)));

  free(threads);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


BENCHMARK_IMPL(async1) {
  return test_async(1);
}


BENCHMARK_IMPL(async2) {
  return test_async(2);
}


BENCHMARK_IMPL(async4) {
  return test_async(4);
}


BENCHMARK_IMPL(async8) {
  return test_async(8);
}
