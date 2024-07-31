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

#ifndef _WIN32

#include "uv.h"
#include "task.h"

uv_loop_t loop;
uv_tcp_t tcp_client;
uv_connect_t connection_request;
uv_write_t write_request;
uv_buf_t buf = { "HELLO", 4 };


static void write_cb(uv_write_t *req, int status) {
  ASSERT_EQ(status, UV_ECANCELED);
  uv_close((uv_handle_t*) req->handle, NULL);
}


static void connect_cb(uv_connect_t *req, int status) {
  ASSERT_EQ(status, UV_ECONNREFUSED);
}


TEST_IMPL(tcp_write_after_connect) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  struct sockaddr_in sa;
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &sa));
  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_tcp_init(&loop, &tcp_client));

  ASSERT_OK(uv_tcp_connect(&connection_request,
                           &tcp_client,
                           (const struct sockaddr *)
                           &sa,
                           connect_cb));

  ASSERT_OK(uv_write(&write_request,
                     (uv_stream_t *)&tcp_client,
                     &buf, 1,
                     write_cb));

  uv_run(&loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(&loop);
  return 0;
}

#else

typedef int file_has_no_tests; /* ISO C forbids an empty translation unit. */

#endif /* !_WIN32 */
