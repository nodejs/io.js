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
#include <string.h>

#ifndef _WIN32
# include <unistd.h>  /* close */
#else
# include <fcntl.h>
#endif

static uv_pipe_t pipe_client;
static uv_pipe_t pipe_server;
static uv_connect_t connect_req;

static int pipe_close_cb_called = 0;
static int pipe_client_connect_cb_called = 0;


static void pipe_close_cb(uv_handle_t* handle) {
  ASSERT_NE(handle == (uv_handle_t*) &pipe_client ||
            handle == (uv_handle_t*) &pipe_server, 0);
  pipe_close_cb_called++;
}


static void pipe_client_connect_cb(uv_connect_t* req, int status) {
  char buf[1024];
  size_t len;
  int r;

  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);

  len = sizeof buf;
  r = uv_pipe_getpeername(&pipe_client, buf, &len);
  ASSERT_OK(r);

  if (*buf == '\0') {  /* Linux abstract socket. */
    const char expected[] = "\0" TEST_PIPENAME;
    ASSERT_EQ(len, sizeof(expected) - 1);
    ASSERT_MEM_EQ(buf, expected, len);
  } else {
    ASSERT_NE(0, buf[len - 1]);
    ASSERT_MEM_EQ(buf, TEST_PIPENAME, len);
  }

  len = sizeof buf;
  r = uv_pipe_getsockname(&pipe_client, buf, &len);
  ASSERT(r == 0 && len == 0);

  pipe_client_connect_cb_called++;

  uv_close((uv_handle_t*) &pipe_client, pipe_close_cb);
  uv_close((uv_handle_t*) &pipe_server, pipe_close_cb);
}


#if defined(__linux__)
/* Socket name looks like \0[0-9a-f]{5}, e.g. "\0bad42" */
static void check_is_autobind_abstract_socket_name(const char *p, size_t len) {
  ASSERT_EQ(len, 6);
  ASSERT_EQ(*p, '\0');

  while (*p != '\0') {
    ASSERT((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'));
    p++;
  }
}


static void pipe_client_autobind_connect_cb(uv_connect_t* req, int status) {
  char buf[16];
  size_t len;

  ASSERT_OK(status);
  len = 5;
  ASSERT_EQ(UV_ENOBUFS, uv_pipe_getpeername(&pipe_client, buf, &len));
  len = 6;
  ASSERT_OK(uv_pipe_getpeername(&pipe_client, buf, &len));
  check_is_autobind_abstract_socket_name(buf, len);
  pipe_client_connect_cb_called++;
  uv_close((uv_handle_t*) &pipe_client, pipe_close_cb);
  uv_close((uv_handle_t*) &pipe_server, pipe_close_cb);
}
#endif  /* defined(__linux__) */


static void pipe_server_connection_cb(uv_stream_t* handle, int status) {
  /* This function *may* be called, depending on whether accept or the
   * connection callback is called first.
   */
  ASSERT_OK(status);
}


TEST_IMPL(pipe_getsockname) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#endif
  uv_loop_t* loop;
  char namebuf[256];
  char buf[1024];
  size_t namelen;
  size_t len;
  int r;

  snprintf(namebuf, sizeof(namebuf), "%s-oob", TEST_PIPENAME);
  namelen = sizeof(TEST_PIPENAME) - 1;

  loop = uv_default_loop();
  ASSERT_NOT_NULL(loop);

  r = uv_pipe_init(loop, &pipe_server, 0);
  ASSERT_OK(r);

  r = uv_pipe_bind2(&pipe_server, "bad\0path", 8, 0);
  ASSERT_EQ(r, UV_EINVAL);

  len = sizeof buf;
  r = uv_pipe_getsockname(&pipe_server, buf, &len);
  ASSERT_EQ(r, UV_EBADF);

  len = sizeof buf;
  r = uv_pipe_getpeername(&pipe_server, buf, &len);
  ASSERT_EQ(r, UV_EBADF);

  r = uv_pipe_bind2(&pipe_server, namebuf, namelen, 0);
  ASSERT_OK(r);

#ifndef _WIN32
  ASSERT_STR_EQ(pipe_server.pipe_fname, TEST_PIPENAME);
#endif

  r = uv_pipe_getsockname(&pipe_server, NULL, &len);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_pipe_getsockname(&pipe_server, buf, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_pipe_getsockname(&pipe_server, NULL, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  len = sizeof(TEST_PIPENAME) - 1;
  ASSERT_EQ(UV_ENOBUFS, uv_pipe_getsockname(&pipe_server, buf, &len));

  len = sizeof(TEST_PIPENAME);
  ASSERT_OK(uv_pipe_getsockname(&pipe_server, buf, &len));

  ASSERT_NE(0, buf[len - 1]);
  ASSERT_EQ(buf[len], '\0');
  ASSERT_OK(memcmp(buf, TEST_PIPENAME, len));

  len = sizeof buf;
  r = uv_pipe_getpeername(&pipe_server, buf, &len);
  ASSERT_EQ(r, UV_ENOTCONN);

  r = uv_listen((uv_stream_t*) &pipe_server, 0, pipe_server_connection_cb);
  ASSERT_OK(r);

  r = uv_pipe_init(loop, &pipe_client, 0);
  ASSERT_OK(r);

  len = sizeof buf;
  r = uv_pipe_getsockname(&pipe_client, buf, &len);
  ASSERT_EQ(r, UV_EBADF);

  len = sizeof buf;
  r = uv_pipe_getpeername(&pipe_client, buf, &len);
  ASSERT_EQ(r, UV_EBADF);

  r = uv_pipe_connect2(&connect_req,
                       &pipe_client,
                       namebuf,
                       namelen,
                       0,
                       pipe_client_connect_cb);
  ASSERT_OK(r);

  len = sizeof buf;
  r = uv_pipe_getsockname(&pipe_client, buf, &len);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(len, 0);

  len = sizeof buf;
  r = uv_pipe_getpeername(&pipe_client, buf, &len);
  ASSERT_OK(r);

  ASSERT_NE(0, buf[len - 1]);
  ASSERT_OK(memcmp(buf, TEST_PIPENAME, len));

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);
  ASSERT_EQ(1, pipe_client_connect_cb_called);
  ASSERT_EQ(2, pipe_close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(pipe_getsockname_abstract) {
  /* TODO(bnoordhuis) Use unique name, susceptible to concurrent test runs. */
  static const char name[] = "\0" TEST_PIPENAME;
#if defined(__linux__)
  char buf[256];
  size_t buflen;

  buflen = sizeof(buf);
  memset(buf, 0, sizeof(buf));
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_server, 0));
  ASSERT_OK(uv_pipe_bind2(&pipe_server, name, sizeof(name) - 1, 0));
  ASSERT_OK(uv_pipe_getsockname(&pipe_server, buf, &buflen));
  ASSERT_UINT64_EQ(sizeof(name) - 1, buflen);
  ASSERT_MEM_EQ(name, buf, buflen);
  ASSERT_OK(uv_listen((uv_stream_t*) &pipe_server,
                      0,
                      pipe_server_connection_cb));
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_client, 0));
  ASSERT_OK(uv_pipe_connect2(&connect_req,
                             &pipe_client,
                             name,
                             sizeof(name) - 1,
                             0,
                             pipe_client_connect_cb));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, pipe_client_connect_cb_called);
  ASSERT_EQ(2, pipe_close_cb_called);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#else
  /* On other platforms it should simply fail with UV_EINVAL. */
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_server, 0));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind2(&pipe_server, name, sizeof(name), 0));
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_client, 0));
  uv_close((uv_handle_t*) &pipe_server, pipe_close_cb);
  ASSERT_EQ(UV_EINVAL, uv_pipe_connect2(&connect_req,
                                        &pipe_client,
                                        name,
                                        sizeof(name),
                                        0,
                                        (uv_connect_cb) abort));
  uv_close((uv_handle_t*) &pipe_client, pipe_close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(2, pipe_close_cb_called);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}


TEST_IMPL(pipe_getsockname_autobind) {
#if defined(__linux__)
  char buf[256];
  size_t buflen;

  buflen = sizeof(buf);
  memset(buf, 0, sizeof(buf));
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_server, 0));
  ASSERT_OK(uv_pipe_bind2(&pipe_server, "", 0, 0));
  ASSERT_OK(uv_pipe_getsockname(&pipe_server, buf, &buflen));
  check_is_autobind_abstract_socket_name(buf, buflen);
  ASSERT_OK(uv_listen((uv_stream_t*) &pipe_server, 0,
                      pipe_server_connection_cb));
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_client, 0));
  ASSERT_OK(uv_pipe_connect2(&connect_req, &pipe_client,
                             buf,
                             1 + strlen(&buf[1]),
                             0,
                             pipe_client_autobind_connect_cb));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, pipe_client_connect_cb_called);
  ASSERT_EQ(2, pipe_close_cb_called);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#else
  /* On other platforms it should simply fail with UV_EINVAL. */
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_server, 0));
  ASSERT_EQ(UV_EINVAL, uv_pipe_bind2(&pipe_server, "", 0, 0));
  uv_close((uv_handle_t*) &pipe_server, pipe_close_cb);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, pipe_close_cb_called);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}


TEST_IMPL(pipe_getsockname_blocking) {
#ifdef _WIN32
  HANDLE readh, writeh;
  int readfd;
  char buf1[1024], buf2[1024];
  size_t len1, len2;
  int r;

  r = CreatePipe(&readh, &writeh, NULL, 65536);
  ASSERT(r);

  r = uv_pipe_init(uv_default_loop(), &pipe_client, 0);
  ASSERT_OK(r);
  readfd = _open_osfhandle((intptr_t)readh, _O_RDONLY);
  ASSERT_NE(r, -1);
  r = uv_pipe_open(&pipe_client, readfd);
  ASSERT_OK(r);
  r = uv_read_start((uv_stream_t*) &pipe_client,
                    (uv_alloc_cb) abort,
                    (uv_read_cb) abort);
  ASSERT_OK(r);
  Sleep(100);
  r = uv_read_stop((uv_stream_t*)&pipe_client);
  ASSERT_OK(r);

  len1 = sizeof buf1;
  r = uv_pipe_getsockname(&pipe_client, buf1, &len1);
  ASSERT_OK(r);
  ASSERT_OK(len1);  /* It's an annonymous pipe. */

  r = uv_read_start((uv_stream_t*)&pipe_client,
                    (uv_alloc_cb) abort,
                    (uv_read_cb) abort);
  ASSERT_OK(r);
  Sleep(100);

  len2 = sizeof buf2;
  r = uv_pipe_getsockname(&pipe_client, buf2, &len2);
  ASSERT_OK(r);
  ASSERT_OK(len2);  /* It's an annonymous pipe. */

  r = uv_read_stop((uv_stream_t*)&pipe_client);
  ASSERT_OK(r);

  ASSERT_EQ(len1, len2);
  ASSERT_OK(memcmp(buf1, buf2, len1));

  pipe_close_cb_called = 0;
  uv_close((uv_handle_t*)&pipe_client, pipe_close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, pipe_close_cb_called);

  CloseHandle(writeh);
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
