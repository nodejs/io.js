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


#ifdef _WIN32
# define BAD_PIPENAME "bad-pipe"
#else
# define BAD_PIPENAME "/path/to/unix/socket/that/really/should/not/be/there"
#endif


static int close_cb_called = 0;
static int connect_cb_called = 0;


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void connect_cb(uv_connect_t* connect_req, int status) {
  ASSERT_EQ(status, UV_ENOENT);
  uv_close((uv_handle_t*) connect_req->handle, close_cb);
  connect_cb_called++;
}


static void connect_cb_file(uv_connect_t* connect_req, int status) {
  if (status != UV_ENOTSOCK)
    if (status != UV_EACCES)
      ASSERT_EQ(status, UV_ECONNREFUSED);
  uv_close((uv_handle_t*) connect_req->handle, close_cb);
  connect_cb_called++;
}


TEST_IMPL(pipe_connect_bad_name) {
  uv_pipe_t client;
  uv_connect_t req;
  int r;

  r = uv_pipe_init(uv_default_loop(), &client, 0);
  ASSERT_OK(r);
  uv_pipe_connect(&req, &client, BAD_PIPENAME, connect_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(1, connect_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_connect_to_file) {
  const char* path = "test/fixtures/empty_file";
  uv_pipe_t client;
  uv_connect_t req;
  int r;

  r = uv_pipe_init(uv_default_loop(), &client, 0);
  ASSERT_OK(r);
  uv_pipe_connect(&req, &client, path, connect_cb_file);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(1, connect_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
