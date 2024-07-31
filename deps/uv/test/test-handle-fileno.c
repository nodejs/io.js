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


static int get_tty_fd(void) {
  /* Make sure we have an FD that refers to a tty */
#ifdef _WIN32
  HANDLE handle;
  handle = CreateFileA("conin$",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
  if (handle == INVALID_HANDLE_VALUE)
    return -1;
  return _open_osfhandle((intptr_t) handle, 0);
#else /* unix */
  return open("/dev/tty", O_RDONLY, 0);
#endif
}


TEST_IMPL(handle_fileno) {
  int r;
  int tty_fd;
  struct sockaddr_in addr;
  uv_os_fd_t fd;
  uv_tcp_t tcp;
  uv_udp_t udp;
  uv_pipe_t pipe;
  uv_tty_t tty;
  uv_idle_t idle;
  uv_loop_t* loop;

  loop = uv_default_loop();
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_idle_init(loop, &idle);
  ASSERT_OK(r);
  r = uv_fileno((uv_handle_t*) &idle, &fd);
  ASSERT_EQ(r, UV_EINVAL);
  uv_close((uv_handle_t*) &idle, NULL);

  r = uv_tcp_init(loop, &tcp);
  ASSERT_OK(r);
  r = uv_fileno((uv_handle_t*) &tcp, &fd);
  ASSERT_EQ(r, UV_EBADF);
  r = uv_tcp_bind(&tcp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);
  r = uv_fileno((uv_handle_t*) &tcp, &fd);
  ASSERT_OK(r);
  uv_close((uv_handle_t*) &tcp, NULL);
  r = uv_fileno((uv_handle_t*) &tcp, &fd);
  ASSERT_EQ(r, UV_EBADF);

  r = uv_udp_init(loop, &udp);
  ASSERT_OK(r);
  r = uv_fileno((uv_handle_t*) &udp, &fd);
  ASSERT_EQ(r, UV_EBADF);
  r = uv_udp_bind(&udp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);
  r = uv_fileno((uv_handle_t*) &udp, &fd);
  ASSERT_OK(r);
  uv_close((uv_handle_t*) &udp, NULL);
  r = uv_fileno((uv_handle_t*) &udp, &fd);
  ASSERT_EQ(r, UV_EBADF);

  r = uv_pipe_init(loop, &pipe, 0);
  ASSERT_OK(r);
  r = uv_fileno((uv_handle_t*) &pipe, &fd);
  ASSERT_EQ(r, UV_EBADF);
  r = uv_pipe_bind(&pipe, TEST_PIPENAME);
  ASSERT_OK(r);
  r = uv_fileno((uv_handle_t*) &pipe, &fd);
  ASSERT_OK(r);
  uv_close((uv_handle_t*) &pipe, NULL);
  r = uv_fileno((uv_handle_t*) &pipe, &fd);
  ASSERT_EQ(r, UV_EBADF);

  tty_fd = get_tty_fd();
  if (tty_fd < 0) {
    fprintf(stderr, "Cannot open a TTY fd");
    fflush(stderr);
  } else {
    r = uv_tty_init(loop, &tty, tty_fd, 0);
    ASSERT_OK(r);
    ASSERT(uv_is_readable((uv_stream_t*) &tty));
    ASSERT(!uv_is_writable((uv_stream_t*) &tty));
    r = uv_fileno((uv_handle_t*) &tty, &fd);
    ASSERT_OK(r);
    uv_close((uv_handle_t*) &tty, NULL);
    r = uv_fileno((uv_handle_t*) &tty, &fd);
    ASSERT_EQ(r, UV_EBADF);
    ASSERT(!uv_is_readable((uv_stream_t*) &tty));
    ASSERT(!uv_is_writable((uv_stream_t*) &tty));
  }

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
