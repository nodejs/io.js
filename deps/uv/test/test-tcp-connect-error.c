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


static int connect_cb_called = 0;
static int close_cb_called = 0;



static void connect_cb(uv_connect_t* handle, int status) {
  ASSERT_NOT_NULL(handle);
  connect_cb_called++;
}



static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


TEST_IMPL(tcp_connect_error_fault) {
  const char garbage[] =
      "blah blah blah blah blah blah blah blah blah blah blah blah";
  const struct sockaddr_in* garbage_addr;
  uv_tcp_t server;
  int r;
  uv_connect_t req;

  garbage_addr = (const struct sockaddr_in*) &garbage;

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_connect(&req,
                     &server,
                     (const struct sockaddr*) garbage_addr,
                     connect_cb);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_OK(connect_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
