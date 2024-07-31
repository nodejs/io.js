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


#ifndef _WIN32

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


/* NOTE: size should be divisible by 2 */
static uv_pipe_t incoming[4];
static unsigned int incoming_count;
static unsigned int close_called;


static void set_nonblocking(uv_os_sock_t sock) {
  int r;
#ifdef _WIN32
  unsigned long on = 1;
  r = ioctlsocket(sock, FIONBIO, &on);
  ASSERT_OK(r);
#else
  int flags = fcntl(sock, F_GETFL, 0);
  ASSERT_GE(flags, 0);
  r = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  ASSERT_GE(r, 0);
#endif
}




static void close_cb(uv_handle_t* handle) {
  close_called++;
}


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char base[1];

  buf->base = base;
  buf->len = sizeof(base);
}


static void read_cb(uv_stream_t* handle,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  uv_pipe_t* p;
  uv_pipe_t* inc;
  uv_handle_type pending;
  unsigned int i;

  p = (uv_pipe_t*) handle;
  ASSERT_GE(nread, 0);

  while (uv_pipe_pending_count(p) != 0) {
    pending = uv_pipe_pending_type(p);
    ASSERT_EQ(pending, UV_NAMED_PIPE);

    ASSERT_LT(incoming_count, ARRAY_SIZE(incoming));
    inc = &incoming[incoming_count++];
    ASSERT_OK(uv_pipe_init(p->loop, inc, 0));
    ASSERT_OK(uv_accept(handle, (uv_stream_t*) inc));
  }

  if (incoming_count != ARRAY_SIZE(incoming))
    return;

  ASSERT_OK(uv_read_stop((uv_stream_t*) p));
  uv_close((uv_handle_t*) p, close_cb);
  for (i = 0; i < ARRAY_SIZE(incoming); i++)
    uv_close((uv_handle_t*) &incoming[i], close_cb);
}


TEST_IMPL(pipe_sendmsg) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  uv_pipe_t p;
  int r;
  int fds[2];
  int send_fds[ARRAY_SIZE(incoming)];
  struct msghdr msg;
  char scratch[64];
  struct cmsghdr *cmsg;
  unsigned int i;
  uv_buf_t buf;

  ASSERT_OK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  for (i = 0; i < ARRAY_SIZE(send_fds); i += 2)
    ASSERT_OK(socketpair(AF_UNIX, SOCK_STREAM, 0, send_fds + i));
  ASSERT_EQ(i, ARRAY_SIZE(send_fds));
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &p, 1));
  ASSERT_OK(uv_pipe_open(&p, fds[1]));

  buf = uv_buf_init("X", 1);
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = (struct iovec*) &buf;
  msg.msg_iovlen = 1;
  msg.msg_flags = 0;

  msg.msg_control = (void*) scratch;
  msg.msg_controllen = CMSG_LEN(sizeof(send_fds));
  ASSERT_GE(sizeof(scratch), msg.msg_controllen);

  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = msg.msg_controllen;

  /* silence aliasing warning */
  {
    void* pv = CMSG_DATA(cmsg);
    int* pi = pv;
    for (i = 0; i < ARRAY_SIZE(send_fds); i++)
      pi[i] = send_fds[i];
  }

  set_nonblocking(fds[1]);
  ASSERT_OK(uv_read_start((uv_stream_t*) &p, alloc_cb, read_cb));

  do
    r = sendmsg(fds[0], &msg, 0);
  while (r == -1 && errno == EINTR);
  ASSERT_EQ(1, r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(ARRAY_SIZE(incoming), incoming_count);
  ASSERT_EQ(ARRAY_SIZE(incoming) + 1, close_called);
  close(fds[0]);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#else  /* !_WIN32 */

TEST_IMPL(pipe_sendmsg) {
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#endif  /* _WIN32 */
