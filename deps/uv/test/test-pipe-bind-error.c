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


TEST_IMPL(pipe_bind_error_addrinuse) {
  uv_pipe_t server1, server2;
  int r;

  r = uv_pipe_init(uv_default_loop(), &server1, 0);
  ASSERT_OK(r);
  r = uv_pipe_bind(&server1, TEST_PIPENAME);
  ASSERT_OK(r);

  r = uv_pipe_init(uv_default_loop(), &server2, 0);
  ASSERT_OK(r);
  r = uv_pipe_bind(&server2, TEST_PIPENAME);
  ASSERT_EQ(r, UV_EADDRINUSE);

  r = uv_listen((uv_stream_t*)&server1, SOMAXCONN, NULL);
  ASSERT_OK(r);
  r = uv_listen((uv_stream_t*)&server2, SOMAXCONN, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server1, close_cb);
  uv_close((uv_handle_t*)&server2, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_bind_error_addrnotavail) {
  uv_pipe_t server;
  int r;

  r = uv_pipe_init(uv_default_loop(), &server, 0);
  ASSERT_OK(r);

  r = uv_pipe_bind(&server, BAD_PIPENAME);
  ASSERT_EQ(r, UV_EACCES);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_bind_error_inval) {
  uv_pipe_t server;
  int r;

  r = uv_pipe_init(uv_default_loop(), &server, 0);
  ASSERT_OK(r);
  r = uv_pipe_bind(&server, TEST_PIPENAME);
  ASSERT_OK(r);
  r = uv_pipe_bind(&server, TEST_PIPENAME_2);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_listen_without_bind) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#endif
  uv_pipe_t server;
  int r;

  r = uv_pipe_init(uv_default_loop(), &server, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server, SOMAXCONN, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(pipe_bind_or_listen_error_after_close) {
  uv_pipe_t server;

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &server, 0));
  uv_close((uv_handle_t*) &server, NULL);

  ASSERT_EQ(uv_pipe_bind(&server, TEST_PIPENAME), UV_EINVAL);

  ASSERT_EQ(uv_listen((uv_stream_t*) &server, SOMAXCONN, NULL), UV_EINVAL);

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void connect_overlong_cb(uv_connect_t* connect_req, int status) {
  ASSERT_EQ(status, UV_EINVAL);
  connect_cb_called++;
  uv_close((uv_handle_t*) connect_req->handle, close_cb);
}


TEST_IMPL(pipe_overlong_path) {
  uv_pipe_t pipe;
  uv_connect_t req;

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe, 0));

#ifndef _WIN32
  char path[512];
  memset(path, '@', sizeof(path));
  ASSERT_EQ(UV_EINVAL,
            uv_pipe_bind2(&pipe, path, sizeof(path), UV_PIPE_NO_TRUNCATE));
  ASSERT_EQ(UV_EINVAL,
            uv_pipe_connect2(&req,
                             &pipe,
                             path,
                             sizeof(path),
                             UV_PIPE_NO_TRUNCATE,
                             (uv_connect_cb) abort));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
#endif

  ASSERT_EQ(UV_EINVAL, uv_pipe_bind(&pipe, ""));
  uv_pipe_connect(&req,
                  &pipe,
                  "",
                  (uv_connect_cb) connect_overlong_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;

}
