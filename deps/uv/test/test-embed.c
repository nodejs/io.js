/* Copyright libuv project contributors. All rights reserved.
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
#include <errno.h>

#if !defined(_WIN32) && !defined(_AIX)
#include <poll.h>
#endif

static uv_async_t async;
static uv_barrier_t barrier;


static void thread_main(void* arg) {
  ASSERT_LE(0, uv_barrier_wait(&barrier));
  uv_sleep(250);
  ASSERT_OK(uv_async_send(&async));
}


static void async_cb(uv_async_t* handle) {
  uv_close((uv_handle_t*) handle, NULL);
}


TEST_IMPL(embed) {
  uv_thread_t thread;
  uv_loop_t* loop;

  loop = uv_default_loop();
  ASSERT_OK(uv_async_init(loop, &async, async_cb));
  ASSERT_OK(uv_barrier_init(&barrier, 2));
  ASSERT_OK(uv_thread_create(&thread, thread_main, NULL));
  ASSERT_LE(0, uv_barrier_wait(&barrier));

  while (uv_loop_alive(loop)) {
#if defined(_WIN32) || defined(_AIX)
    ASSERT_LE(0, uv_run(loop, UV_RUN_ONCE));
#else
    int rc;
    do {
      struct pollfd p;
      p.fd = uv_backend_fd(loop);
      p.events = POLLIN;
      p.revents = 0;
      rc = poll(&p, 1, uv_backend_timeout(loop));
    } while (rc == -1 && errno == EINTR);
    ASSERT_LE(0, uv_run(loop, UV_RUN_NOWAIT));
#endif
  }

  ASSERT_OK(uv_thread_join(&thread));
  uv_barrier_destroy(&barrier);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
