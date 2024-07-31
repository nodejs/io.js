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

static uv_thread_t thread;
static uv_mutex_t mutex;

static uv_prepare_t prepare;
static uv_async_t async;

static volatile int async_cb_called;
static int prepare_cb_called;
static int close_cb_called;


static void thread_cb(void *arg) {
  int n;
  int r;

  for (;;) {
    uv_mutex_lock(&mutex);
    n = async_cb_called;
    uv_mutex_unlock(&mutex);

    if (n == 3) {
      break;
    }

    r = uv_async_send(&async);
    ASSERT_OK(r);

    /* Work around a bug in Valgrind.
     *
     * Valgrind runs threads not in parallel but sequentially, i.e. one after
     * the other. It also doesn't preempt them, instead it depends on threads
     * yielding voluntarily by making a syscall.
     *
     * That never happens here: the pipe that is associated with the async
     * handle is written to once but that's too early for Valgrind's scheduler
     * to kick in. Afterwards, the thread busy-loops, starving the main thread.
     * Therefore, we yield.
     *
     * This behavior has been observed with Valgrind 3.7.0 and 3.9.0.
     */
    uv_sleep(0);
  }
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void async_cb(uv_async_t* handle) {
  int n;

  ASSERT_PTR_EQ(handle, &async);

  uv_mutex_lock(&mutex);
  n = ++async_cb_called;
  uv_mutex_unlock(&mutex);

  if (n == 3) {
    uv_close((uv_handle_t*)&async, close_cb);
    uv_close((uv_handle_t*)&prepare, close_cb);
  }
}


static void prepare_cb(uv_prepare_t* handle) {
  int r;

  ASSERT_PTR_EQ(handle, &prepare);

  if (prepare_cb_called++)
    return;

  r = uv_thread_create(&thread, thread_cb, NULL);
  ASSERT_OK(r);
  uv_mutex_unlock(&mutex);
}


TEST_IMPL(async) {
  int r;

  r = uv_mutex_init(&mutex);
  ASSERT_OK(r);
  uv_mutex_lock(&mutex);

  r = uv_prepare_init(uv_default_loop(), &prepare);
  ASSERT_OK(r);
  r = uv_prepare_start(&prepare, prepare_cb);
  ASSERT_OK(r);

  r = uv_async_init(uv_default_loop(), &async, async_cb);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_GT(prepare_cb_called, 0);
  ASSERT_EQ(3, async_cb_called);
  ASSERT_EQ(2, close_cb_called);

  ASSERT_OK(uv_thread_join(&thread));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
