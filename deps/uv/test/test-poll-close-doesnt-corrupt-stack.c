/* Copyright Bert Belder, and other libuv contributors. All rights reserved.
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
#include <stdio.h>

#include "uv.h"
#include "task.h"

#ifdef _MSC_VER  /* msvc */
# define NO_INLINE __declspec(noinline)
#else  /* gcc */
# define NO_INLINE __attribute__ ((noinline))
#endif

#ifdef _WIN32
static uv_os_sock_t sock;
static uv_poll_t handle;
static int close_cb_called = 0;


static void close_cb(uv_handle_t* h) {
  close_cb_called++;
}


static void poll_cb(uv_poll_t* h, int status, int events) {
  ASSERT(0 && "should never get here");
}


static void NO_INLINE close_socket_and_verify_stack(void) {
  const uint32_t MARKER = 0xDEADBEEF;
  const int VERIFY_AFTER = 10; /* ms */
  int r;

  volatile uint32_t data[65536];
  size_t i;

  for (i = 0; i < ARRAY_SIZE(data); i++)
    data[i] = MARKER;

  r = closesocket(sock);
  ASSERT_OK(r);

  uv_sleep(VERIFY_AFTER);

  for (i = 0; i < ARRAY_SIZE(data); i++)
    ASSERT_EQ(data[i], MARKER);
}
#endif


TEST_IMPL(poll_close_doesnt_corrupt_stack) {
#ifndef _WIN32
  RETURN_SKIP("Test only relevant on Windows");
#else
  struct WSAData wsa_data;
  int r;
  unsigned long on;
  struct sockaddr_in addr;

  r = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  ASSERT_OK(r);

  sock = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_NE(sock, INVALID_SOCKET);
  on = 1;
  r = ioctlsocket(sock, FIONBIO, &on);
  ASSERT_OK(r);

  r = uv_ip4_addr("127.0.0.1", TEST_PORT, &addr);
  ASSERT_OK(r);

  r = connect(sock, (const struct sockaddr*) &addr, sizeof addr);
  ASSERT(r);
  ASSERT_EQ(WSAGetLastError(), WSAEWOULDBLOCK);

  r = uv_poll_init_socket(uv_default_loop(), &handle, sock);
  ASSERT_OK(r);
  r = uv_poll_start(&handle, UV_READABLE | UV_WRITABLE, poll_cb);
  ASSERT_OK(r);

  uv_close((uv_handle_t*) &handle, close_cb);

  close_socket_and_verify_stack();

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}
