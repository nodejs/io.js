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

#ifdef _WIN32

TEST_IMPL(eintr_handling) {
  RETURN_SKIP("Test not implemented on Windows.");
}

#else  /* !_WIN32 */

#include <string.h>
#include <unistd.h>

static uv_loop_t* loop;
static uv_fs_t read_req;
static uv_buf_t iov;

static char buf[32];
static char test_buf[] = "test-buffer\n";
int pipe_fds[2];

struct thread_ctx {
  uv_barrier_t barrier;
  int fd;
};

static void thread_main(void* arg) {
  int nwritten;
  ASSERT_OK(kill(getpid(), SIGUSR1));

  do
    nwritten = write(pipe_fds[1], test_buf, sizeof(test_buf));
  while (nwritten == -1 && errno == EINTR);

  ASSERT_EQ(nwritten, sizeof(test_buf));
}

static void sig_func(uv_signal_t* handle, int signum) {
  uv_signal_stop(handle);
}

TEST_IMPL(eintr_handling) {
  struct thread_ctx ctx;
  uv_thread_t thread;
  uv_signal_t signal;
  int nread;

  iov = uv_buf_init(buf, sizeof(buf));
  loop = uv_default_loop();

  ASSERT_OK(uv_signal_init(loop, &signal));
  ASSERT_OK(uv_signal_start(&signal, sig_func, SIGUSR1));

  ASSERT_OK(pipe(pipe_fds));
  ASSERT_OK(uv_thread_create(&thread, thread_main, &ctx));

  nread = uv_fs_read(loop, &read_req, pipe_fds[0], &iov, 1, -1, NULL);

  ASSERT_EQ(nread, sizeof(test_buf));
  ASSERT_OK(strcmp(buf, test_buf));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_OK(close(pipe_fds[0]));
  ASSERT_OK(close(pipe_fds[1]));
  uv_close((uv_handle_t*) &signal, NULL);

  ASSERT_OK(uv_thread_join(&thread));

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

#endif  /* !_WIN32 */
