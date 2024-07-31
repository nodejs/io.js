/* Copyright (c) 2015, Ben Noordhuis <info@bnoordhuis.nl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "uv.h"
#include "task.h"

#include <string.h> /* memset */
#ifndef _WIN32
#include <unistd.h> /* close */
#endif

struct thread_ctx {
  uv_barrier_t barrier;
  uv_file fd;
};

static void thread_main(void* arg) {
  struct thread_ctx* ctx;
  uv_fs_t req;
  uv_buf_t bufs[1];
  char buf[4096];
  ssize_t n;
  int uv_errno;

  bufs[0] = uv_buf_init(buf, sizeof(buf));

  ctx = arg;
  uv_barrier_wait(&ctx->barrier);

  uv_sleep(100); /* make sure we are forcing the writer to block a bit */
  do {
    uv_errno = uv_fs_read(NULL, &req, ctx->fd, bufs, 1, -1, NULL);
    n = req.result;
    uv_fs_req_cleanup(&req);
  } while (n > 0 || (n == -1 && uv_errno == UV_EINTR));

  ASSERT_OK(n);
}


#ifdef _WIN32
static void write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  req->handle = NULL; /* signal completion of write_cb */
}
#endif

#ifdef _WIN32
#define NWRITES (10 << 16)
#else
#define NWRITES (10 << 20)
#endif


TEST_IMPL(pipe_set_non_blocking) {
  struct thread_ctx ctx;
  uv_pipe_t pipe_handle;
  uv_thread_t thread;
  size_t nwritten;
  char data[4096];
  uv_buf_t buf;
  uv_file fd[2];
  int n;
#ifdef _WIN32
  uv_write_t write_req;
#endif

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_handle, 0));
  ASSERT_OK(uv_pipe(fd, 0, 0));
  ASSERT_OK(uv_pipe_open(&pipe_handle, fd[1]));
  ASSERT_OK(uv_stream_set_blocking((uv_stream_t*) &pipe_handle, 1));
  fd[1] = -1; /* fd[1] is owned by pipe_handle now. */

  ctx.fd = fd[0];
  ASSERT_OK(uv_barrier_init(&ctx.barrier, 2));
  ASSERT_OK(uv_thread_create(&thread, thread_main, &ctx));
  uv_barrier_wait(&ctx.barrier);

  buf.len = sizeof(data);
  buf.base = data;
  memset(data, '.', sizeof(data));

  nwritten = 0;
  while (nwritten < NWRITES) {
    /* The stream is in blocking mode so uv_try_write() should always succeed
     * with the exact number of bytes that we wanted written.
     */
    n = uv_try_write((uv_stream_t*) &pipe_handle, &buf, 1);
#ifdef _WIN32
    ASSERT_EQ(n, UV_EAGAIN); /* E_NOTIMPL */
    ASSERT_OK(uv_write(&write_req,
                       (uv_stream_t*) &pipe_handle,
                       &buf,
                       1,
                       write_cb));
    ASSERT_NOT_NULL(write_req.handle);
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_ONCE));
    ASSERT_NULL(write_req.handle); /* check for signaled completion of write_cb */
    n = buf.len;
#endif
    ASSERT_EQ(n, sizeof(data));
    nwritten += n;
  }

  uv_close((uv_handle_t*) &pipe_handle, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_OK(uv_thread_join(&thread));
#ifdef _WIN32
  ASSERT_OK(_close(fd[0]));  /* fd[1] is closed by uv_close(). */
#else
  ASSERT_OK(close(fd[0]));  /* fd[1] is closed by uv_close(). */
#endif
  fd[0] = -1;
  uv_barrier_destroy(&ctx.barrier);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
