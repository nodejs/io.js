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
#include <string.h>


static char exepath[1024];
static size_t exepath_size = 1024;
static char* args[3];
static uv_process_options_t options;
static int close_cb_called;
static int exit_cb_called;
static int on_read_cb_called;
static int after_write_cb_called;
static uv_pipe_t in;
static uv_pipe_t out;
static uv_loop_t* loop;
#define OUTPUT_SIZE 1024
static char output[OUTPUT_SIZE];
static int output_used;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void exit_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  printf("exit_cb\n");
  exit_cb_called++;
  ASSERT_OK(exit_status);
  ASSERT_OK(term_signal);
  uv_close((uv_handle_t*)process, close_cb);
  uv_close((uv_handle_t*)&in, close_cb);
  uv_close((uv_handle_t*)&out, close_cb);
}


static void init_process_options(char* test, uv_exit_cb exit_cb) {
  int r = uv_exepath(exepath, &exepath_size);
  ASSERT_OK(r);
  exepath[exepath_size] = '\0';
  args[0] = exepath;
  args[1] = test;
  args[2] = NULL;
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;
}


static void on_alloc(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  buf->base = output + output_used;
  buf->len = OUTPUT_SIZE - output_used;
}


static void after_write(uv_write_t* req, int status) {
  if (status) {
    fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
    ASSERT(0);
  }

  /* Free the read/write buffer and the request */
  free(req);

  after_write_cb_called++;
}


static void on_read(uv_stream_t* pipe, ssize_t nread, const uv_buf_t* rdbuf) {
  uv_write_t* req;
  uv_buf_t wrbuf;
  int r;

  ASSERT(nread > 0 || nread == UV_EOF);

  if (nread > 0) {
    output_used += nread;
    if (output_used % 12 == 0) {
      ASSERT_OK(memcmp("hello world\n", output, 12));
      wrbuf = uv_buf_init(output, 12);
      req = malloc(sizeof(*req));
      r = uv_write(req, (uv_stream_t*) &in, &wrbuf, 1, after_write);
      ASSERT_OK(r);
    }
  }

  on_read_cb_called++;
}


static void test_stdio_over_pipes(int overlapped) {
  int r;
  uv_process_t process;
  uv_stdio_container_t stdio[3];

  loop = uv_default_loop();

  init_process_options("stdio_over_pipes_helper", exit_cb);

  uv_pipe_init(loop, &out, 0);
  uv_pipe_init(loop, &in, 0);

  options.stdio = stdio;
  options.stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE |
      (overlapped ?  UV_OVERLAPPED_PIPE : 0);
  options.stdio[0].data.stream = (uv_stream_t*) &in;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE |
      (overlapped ? UV_OVERLAPPED_PIPE : 0);
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio[2].flags = UV_INHERIT_FD;
  options.stdio[2].data.fd = 2;
  options.stdio_count = 3;

  r = uv_spawn(loop, &process, &options);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_GT(on_read_cb_called, 1);
  ASSERT_EQ(2, after_write_cb_called);
  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(3, close_cb_called);
  ASSERT_OK(memcmp("hello world\nhello world\n", output, 24));
  ASSERT_EQ(24, output_used);

  MAKE_VALGRIND_HAPPY(loop);
}

TEST_IMPL(stdio_over_pipes) {
  test_stdio_over_pipes(0);
  return 0;
}

TEST_IMPL(stdio_emulate_iocp) {
  test_stdio_over_pipes(1);
  return 0;
}


/* Everything here runs in a child process. */

static int on_pipe_read_called;
static int after_write_called;
static uv_pipe_t stdin_pipe1;
static uv_pipe_t stdout_pipe1;
static uv_pipe_t stdin_pipe2;
static uv_pipe_t stdout_pipe2;

static void on_pipe_read(uv_stream_t* pipe, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_GT(nread, 0);
  ASSERT_OK(memcmp("hello world\n", buf->base, nread));
  on_pipe_read_called++;

  free(buf->base);

  uv_read_stop(pipe);
}


static void after_pipe_write(uv_write_t* req, int status) {
  ASSERT_OK(status);
  after_write_called++;
}


static void on_read_alloc(uv_handle_t* handle,
                          size_t suggested_size,
                          uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}


int stdio_over_pipes_helper(void) {
  /* Write several buffers to test that the write order is preserved. */
  char* buffers[] = {
    "he",
    "ll",
    "o ",
    "wo",
    "rl",
    "d",
    "\n"
  };

  uv_write_t write_req[ARRAY_SIZE(buffers)];
  uv_buf_t buf[ARRAY_SIZE(buffers)];
  unsigned int i;
  int j;
  int r;
  uv_loop_t* loop = uv_default_loop();

  ASSERT_EQ(UV_NAMED_PIPE, uv_guess_handle(0));
  ASSERT_EQ(UV_NAMED_PIPE, uv_guess_handle(1));

  r = uv_pipe_init(loop, &stdin_pipe1, 0);
  ASSERT_OK(r);
  r = uv_pipe_init(loop, &stdout_pipe1, 0);
  ASSERT_OK(r);
  r = uv_pipe_init(loop, &stdin_pipe2, 0);
  ASSERT_OK(r);
  r = uv_pipe_init(loop, &stdout_pipe2, 0);
  ASSERT_OK(r);

  uv_pipe_open(&stdin_pipe1, 0);
  uv_pipe_open(&stdout_pipe1, 1);
  uv_pipe_open(&stdin_pipe2, 0);
  uv_pipe_open(&stdout_pipe2, 1);

  for (j = 0; j < 2; j++) {
    /* Unref both stdio handles to make sure that all writes complete. */
    uv_unref((uv_handle_t*) &stdin_pipe1);
    uv_unref((uv_handle_t*) &stdout_pipe1);
    uv_unref((uv_handle_t*) &stdin_pipe2);
    uv_unref((uv_handle_t*) &stdout_pipe2);

    for (i = 0; i < ARRAY_SIZE(buffers); i++) {
      buf[i] = uv_buf_init((char*) buffers[i], strlen(buffers[i]));
    }

    for (i = 0; i < ARRAY_SIZE(buffers); i++) {
      r = uv_write(&write_req[i],
                   (uv_stream_t*) (j == 0 ? &stdout_pipe1 : &stdout_pipe2),
                   &buf[i],
                   1,
                   after_pipe_write);
      ASSERT_OK(r);
    }

    notify_parent_process();
    uv_run(loop, UV_RUN_DEFAULT);

    ASSERT_EQ(after_write_called, 7 * (j + 1));
    ASSERT_EQ(on_pipe_read_called, j);
    ASSERT_OK(close_cb_called);

    uv_ref((uv_handle_t*) &stdout_pipe1);
    uv_ref((uv_handle_t*) &stdin_pipe1);
    uv_ref((uv_handle_t*) &stdout_pipe2);
    uv_ref((uv_handle_t*) &stdin_pipe2);

    r = uv_read_start((uv_stream_t*) (j == 0 ? &stdin_pipe1 : &stdin_pipe2),
                      on_read_alloc,
                      on_pipe_read);
    ASSERT_OK(r);

    uv_run(loop, UV_RUN_DEFAULT);

    ASSERT_EQ(after_write_called, 7 * (j + 1));
    ASSERT_EQ(on_pipe_read_called, j + 1);
    ASSERT_OK(close_cb_called);
  }

  uv_close((uv_handle_t*)&stdin_pipe1, close_cb);
  uv_close((uv_handle_t*)&stdout_pipe1, close_cb);
  uv_close((uv_handle_t*)&stdin_pipe2, close_cb);
  uv_close((uv_handle_t*)&stdout_pipe2, close_cb);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(14, after_write_called);
  ASSERT_EQ(2, on_pipe_read_called);
  ASSERT_EQ(4, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
