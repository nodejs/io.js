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

TEST_IMPL(iouring_pollhup) {
  RETURN_SKIP("Not on Windows.");
}

#else  /* !_WIN32 */

#include <unistd.h>  /* close() */

static uv_pipe_t p1;
static uv_pipe_t p2;
static uv_idle_t idle_handle;
static int iters;
static int duped_fd;
static int newpipefds[2];

static void alloc_buffer(uv_handle_t* handle,
                         size_t suggested_size,
                         uv_buf_t* buf) {
  static char slab[32];
  *buf = uv_buf_init(slab, sizeof(slab));
}

static void read_data2(uv_stream_t* stream,
                       ssize_t nread,
                       const uv_buf_t* buf) {
  if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);
    ASSERT_OK(close(duped_fd));
    duped_fd = -1;
    uv_close((uv_handle_t*) &p2, NULL);
    uv_close((uv_handle_t*) &idle_handle, NULL);
  } else {
    /* If nread == 0 is because of POLLHUP received still from pipefds[0] file
     * description which is still referenced in duped_fd. It should not happen
     * if close(p1) was called after EPOLL_CTL_DEL.
     */
    ASSERT_GT(nread, 0);
  }
}

static void idle_cb(uv_idle_t* handle) {
  if (++iters == 1) {
    ASSERT_OK(uv_pipe_open(&p2, newpipefds[0]));
    ASSERT_OK(uv_read_start((uv_stream_t*) &p2, alloc_buffer, read_data2));
  } else {
    ASSERT_OK(uv_idle_stop(handle));
    ASSERT_OK(close(newpipefds[1]));
    newpipefds[1] = -1;
  }
}

static void read_data(uv_stream_t* stream,
                      ssize_t nread,
                      const uv_buf_t* buf) {
  ASSERT_EQ(nread, UV_EOF);
  uv_close((uv_handle_t*) stream, NULL);
  ASSERT_OK(uv_idle_start(&idle_handle, idle_cb));
}

TEST_IMPL(iouring_pollhup) {
  uv_loop_t* loop;
  int pipefds[2];

  loop = uv_default_loop();
  ASSERT_OK(uv_pipe_init(loop, &p1, 0));
  ASSERT_OK(uv_pipe_init(loop, &p2, 0));
  ASSERT_OK(uv_idle_init(loop, &idle_handle));
  ASSERT_OK(pipe(pipefds));
  ASSERT_OK(pipe(newpipefds));

  ASSERT_OK(uv_pipe_open(&p1, pipefds[0]));
  duped_fd = dup(pipefds[0]);
  ASSERT_NE(duped_fd, -1);

  ASSERT_OK(uv_read_start((uv_stream_t*) &p1, alloc_buffer, read_data));
  ASSERT_OK(close(pipefds[1]));  /* Close write end, generate POLLHUP. */
  pipefds[1] = -1;

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#endif  /* !_WIN32 */
