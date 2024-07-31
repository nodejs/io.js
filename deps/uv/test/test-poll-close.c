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

#include <errno.h>

#ifndef _WIN32
# include <fcntl.h>
# include <sys/socket.h>
# include <unistd.h>
#endif

#include "uv.h"
#include "task.h"

#define NUM_SOCKETS 64


static int close_cb_called = 0;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


TEST_IMPL(poll_close) {
  uv_os_sock_t sockets[NUM_SOCKETS];
  uv_poll_t poll_handles[NUM_SOCKETS];
  int i;

#ifdef _WIN32
  {
    struct WSAData wsa_data;
    int r = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    ASSERT_OK(r);
  }
#endif

  for (i = 0; i < NUM_SOCKETS; i++) {
    sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
    uv_poll_init_socket(uv_default_loop(), &poll_handles[i], sockets[i]);
    uv_poll_start(&poll_handles[i], UV_READABLE | UV_WRITABLE, NULL);
  }

  for (i = 0; i < NUM_SOCKETS; i++) {
    uv_close((uv_handle_t*) &poll_handles[i], close_cb);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(close_cb_called, NUM_SOCKETS);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
