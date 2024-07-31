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
#ifndef _WIN32

#include "uv.h"
#include "task.h"

#include <string.h>
#include <unistd.h>

static uv_loop_t loop;
static uv_signal_t signal_hdl;
static uv_pipe_t pipe_hdl;
static uv_write_t write_req;
static char* buf;
static int close_cb_called;


static void stop_loop_cb(uv_signal_t* signal, int signum) {
  ASSERT_EQ(signum, SIGPIPE);
  uv_stop(signal->loop);
}

static void signal_cb(uv_signal_t* signal, int signum) {
  ASSERT(0);
}

static void close_cb(uv_handle_t *handle) {
  close_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT_EQ(status, UV_EPIPE);
  free(buf);
  uv_close((uv_handle_t *) &pipe_hdl, close_cb);
  uv_close((uv_handle_t *) &signal_hdl, close_cb);
}


TEST_IMPL(signal_pending_on_close) {
  int pipefds[2];
  uv_buf_t buffer;
  int r;

  ASSERT_OK(uv_loop_init(&loop));

  ASSERT_OK(uv_signal_init(&loop, &signal_hdl));

  ASSERT_OK(uv_signal_start(&signal_hdl, signal_cb, SIGPIPE));

  ASSERT_OK(pipe(pipefds));

  ASSERT_OK(uv_pipe_init(&loop, &pipe_hdl, 0));

  ASSERT_OK(uv_pipe_open(&pipe_hdl, pipefds[1]));

  /* Write data large enough so it needs loop iteration */
  buf = malloc(1<<24);
  ASSERT_NOT_NULL(buf);
  memset(buf, '.', 1<<24);
  buffer = uv_buf_init(buf, 1<<24);

  r = uv_write(&write_req, (uv_stream_t *) &pipe_hdl, &buffer, 1, write_cb);
  ASSERT_OK(r);

  /* cause a SIGPIPE on write in next iteration */
  close(pipefds[0]);

  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));

  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(&loop);
  return 0;
}


TEST_IMPL(signal_close_loop_alive) {
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_signal_init(&loop, &signal_hdl));
  ASSERT_OK(uv_signal_start(&signal_hdl, stop_loop_cb, SIGPIPE));
  uv_unref((uv_handle_t*) &signal_hdl);

  ASSERT_OK(uv_kill(uv_os_getpid(), SIGPIPE));
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  uv_close((uv_handle_t*) &signal_hdl, close_cb);
  ASSERT_EQ(1, uv_loop_alive(&loop));

  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(&loop);
  return 0;
}

#endif
