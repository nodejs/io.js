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
#include <stdio.h>
#include <stdlib.h>


#ifdef _WIN32
# define BAD_PIPENAME "bad-pipe"
#else
# define BAD_PIPENAME "/path/to/unix/socket/that/really/should/not/be/there"
#endif


static int close_cb_called = 0;
static int connect_cb_called = 0;

static uv_pipe_t pipe_handle;
static uv_prepare_t prepare_handle;
static uv_connect_t conn_req;


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void connect_cb(uv_connect_t* connect_req, int status) {
  ASSERT_EQ(status, UV_ENOENT);
  connect_cb_called++;
  uv_close((uv_handle_t*)&prepare_handle, close_cb);
  uv_close((uv_handle_t*)&pipe_handle, close_cb);
}


static void prepare_cb(uv_prepare_t* handle) {
  ASSERT_PTR_EQ(handle, &prepare_handle);
  uv_pipe_connect(&conn_req, &pipe_handle, BAD_PIPENAME, connect_cb);
}


TEST_IMPL(pipe_connect_on_prepare) {
  int r;

  r = uv_pipe_init(uv_default_loop(), &pipe_handle, 0);
  ASSERT_OK(r);

  r = uv_prepare_init(uv_default_loop(), &prepare_handle);
  ASSERT_OK(r);
  r = uv_prepare_start(&prepare_handle, prepare_cb);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(2, close_cb_called);
  ASSERT_EQ(1, connect_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
