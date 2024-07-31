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

#include "task.h"
#include "uv.h"

#include <string.h>

/* See test-ipc.c */
void spawn_helper(uv_pipe_t* channel,
                  uv_process_t* process,
                  const char* helper);

#define NUM_WRITES 256
#define BUFFERS_PER_WRITE 3
#define BUFFER_SIZE 0x2000 /* 8 kb. */
#define BUFFER_CONTENT 42

#define XFER_SIZE (NUM_WRITES * BUFFERS_PER_WRITE * BUFFER_SIZE)

struct write_info {
  uv_write_t write_req;
  char buffers[BUFFER_SIZE][BUFFERS_PER_WRITE];
};

static uv_shutdown_t shutdown_req;

static size_t bytes_written;
static size_t bytes_read;

static void write_cb(uv_write_t* req, int status) {
  struct write_info* write_info =
      container_of(req, struct write_info, write_req);
  ASSERT_OK(status);
  bytes_written += BUFFERS_PER_WRITE * BUFFER_SIZE;
  free(write_info);
}

static void shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT(status == 0 || status == UV_ENOTCONN);
  uv_close((uv_handle_t*) req->handle, NULL);
}

static void do_write(uv_stream_t* handle) {
  struct write_info* write_info;
  uv_buf_t bufs[BUFFERS_PER_WRITE];
  size_t i;
  int r;

  write_info = malloc(sizeof *write_info);
  ASSERT_NOT_NULL(write_info);

  for (i = 0; i < BUFFERS_PER_WRITE; i++) {
    memset(&write_info->buffers[i], BUFFER_CONTENT, BUFFER_SIZE);
    bufs[i] = uv_buf_init(write_info->buffers[i], BUFFER_SIZE);
  }

  r = uv_write(
      &write_info->write_req, handle, bufs, BUFFERS_PER_WRITE, write_cb);
  ASSERT_OK(r);
}

static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = (int) suggested_size;
}

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#endif

static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  ssize_t i;
  int r;

  ASSERT_GE(nread, 0);
  bytes_read += nread;

  for (i = 0; i < nread; i++)
    ASSERT_EQ(buf->base[i], BUFFER_CONTENT);
  free(buf->base);

  if (bytes_read >= XFER_SIZE) {
    r = uv_read_stop(handle);
    ASSERT_OK(r);
    r = uv_shutdown(&shutdown_req, handle, shutdown_cb);
    ASSERT_OK(r);
  }
}

static void do_writes_and_reads(uv_stream_t* handle) {
  size_t i;
  int r;

  bytes_written = 0;
  bytes_read = 0;

  for (i = 0; i < NUM_WRITES; i++) {
    do_write(handle);
  }

  r = uv_read_start(handle, alloc_cb, read_cb);
  ASSERT_OK(r);

  r = uv_run(handle->loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(bytes_written, XFER_SIZE);
  ASSERT_EQ(bytes_read, XFER_SIZE);
}

TEST_IMPL(ipc_heavy_traffic_deadlock_bug) {
  uv_pipe_t pipe;
  uv_process_t process;

  spawn_helper(&pipe, &process, "ipc_helper_heavy_traffic_deadlock_bug");
  do_writes_and_reads((uv_stream_t*) &pipe);

  MAKE_VALGRIND_HAPPY(pipe.loop);
  return 0;
}

int ipc_helper_heavy_traffic_deadlock_bug(void) {
  uv_pipe_t pipe;
  int r;

  r = uv_pipe_init(uv_default_loop(), &pipe, 1);
  ASSERT_OK(r);
  r = uv_pipe_open(&pipe, 0);
  ASSERT_OK(r);

  notify_parent_process();
  do_writes_and_reads((uv_stream_t*) &pipe);
  uv_sleep(100);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
