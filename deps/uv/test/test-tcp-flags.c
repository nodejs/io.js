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


TEST_IMPL(tcp_flags) {
  uv_loop_t* loop;
  uv_tcp_t handle;
  int r;

  loop = uv_default_loop();

  /* Use _ex to make sure the socket is created. */
  r = uv_tcp_init_ex(loop, &handle, AF_INET);
  ASSERT_OK(r);

  r = uv_tcp_nodelay(&handle, 1);
  ASSERT_OK(r);

  r = uv_tcp_keepalive(&handle, 1, 60);
  ASSERT_OK(r);

  r = uv_tcp_keepalive(&handle, 0, 0);
  ASSERT_OK(r);

  r = uv_tcp_keepalive(&handle, 1, 0);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&handle, NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
