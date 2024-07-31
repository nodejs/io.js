/* Copyright (c) 2015 Saúl Ibarra Corretgé <saghul@gmail.com>.
 * All rights reserved.
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


static void connection_cb(uv_stream_t* server, int status) {
  ASSERT(0 && "this will never be called");
}


TEST_IMPL(pipe_pending_instances) {
  int r;
  uv_pipe_t pipe_handle;
  uv_loop_t* loop;

  loop = uv_default_loop();

  r = uv_pipe_init(loop, &pipe_handle, 0);
  ASSERT_OK(r);

  uv_pipe_pending_instances(&pipe_handle, 8);

  r = uv_pipe_bind(&pipe_handle, TEST_PIPENAME);
  ASSERT_OK(r);

  uv_pipe_pending_instances(&pipe_handle, 16);

  r = uv_listen((uv_stream_t*)&pipe_handle, 128, connection_cb);
  ASSERT_OK(r);

  uv_close((uv_handle_t*)&pipe_handle, NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
