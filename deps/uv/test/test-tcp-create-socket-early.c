/* Copyright (c) 2015 Saúl Ibarra Corretgé <saghul@gmail.com>.
 * All rights reserved.
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
#include <string.h>

#ifdef _WIN32
# define INVALID_FD (INVALID_HANDLE_VALUE)
#else
# define INVALID_FD (-1)
#endif


static void on_connect(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  uv_close((uv_handle_t*) req->handle, NULL);
}


static void on_connection(uv_stream_t* server, int status) {
  uv_tcp_t* handle;
  int r;

  ASSERT_OK(status);

  handle = malloc(sizeof(*handle));
  ASSERT_NOT_NULL(handle);

  r = uv_tcp_init_ex(server->loop, handle, AF_INET);
  ASSERT_OK(r);

  r = uv_accept(server, (uv_stream_t*)handle);
  ASSERT_EQ(r, UV_EBUSY);

  uv_close((uv_handle_t*) server, NULL);
  uv_close((uv_handle_t*) handle, (uv_close_cb)free);
}


static void tcp_listener(uv_loop_t* loop, uv_tcp_t* server) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_tcp_init(loop, server);
  ASSERT_OK(r);

  r = uv_tcp_bind(server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*) server, 128, on_connection);
  ASSERT_OK(r);
}


static void tcp_connector(uv_loop_t* loop, uv_tcp_t* client, uv_connect_t* req) {
  struct sockaddr_in server_addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));

  r = uv_tcp_init(loop, client);
  ASSERT_OK(r);

  r = uv_tcp_connect(req,
                     client,
                     (const struct sockaddr*) &server_addr,
                     on_connect);
  ASSERT_OK(r);
}


TEST_IMPL(tcp_create_early) {
  struct sockaddr_in addr;
  struct sockaddr_in sockname;
  uv_tcp_t client;
  uv_os_fd_t fd;
  int r, namelen;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init_ex(uv_default_loop(), &client, AF_INET);
  ASSERT_OK(r);

  r = uv_fileno((const uv_handle_t*) &client, &fd);
  ASSERT_OK(r);

  /* Windows returns WSAEINVAL if the socket is not bound */
#ifndef _WIN32
  ASSERT_NE(fd, INVALID_FD);
  namelen = sizeof sockname;
  r = uv_tcp_getsockname(&client, (struct sockaddr*) &sockname, &namelen);
  ASSERT_OK(r);
  ASSERT_EQ(sockname.sin_family, AF_INET);
#else
  ASSERT_PTR_NE(fd, INVALID_FD);
#endif

  r = uv_tcp_bind(&client, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  namelen = sizeof sockname;
  r = uv_tcp_getsockname(&client, (struct sockaddr*) &sockname, &namelen);
  ASSERT_OK(r);
  ASSERT_OK(memcmp(&addr.sin_addr,
                   &sockname.sin_addr,
                   sizeof(addr.sin_addr)));

  uv_close((uv_handle_t*) &client, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_create_early_bad_bind) {
  struct sockaddr_in addr;
  uv_tcp_t client;
  uv_os_fd_t fd;
  int r;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init_ex(uv_default_loop(), &client, AF_INET6);
  ASSERT_OK(r);

  r = uv_fileno((const uv_handle_t*) &client, &fd);
  ASSERT_OK(r);

  /* Windows returns WSAEINVAL if the socket is not bound */
#ifndef _WIN32
  ASSERT_NE(fd, INVALID_FD);
  {
    int namelen;
    struct sockaddr_in6 sockname;
    namelen = sizeof sockname;
    r = uv_tcp_getsockname(&client, (struct sockaddr*) &sockname, &namelen);
    ASSERT_OK(r);
    ASSERT_EQ(sockname.sin6_family, AF_INET6);
  }
#else 
  ASSERT_PTR_NE(fd, INVALID_FD);
#endif

  r = uv_tcp_bind(&client, (const struct sockaddr*) &addr, 0);
#if !defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
  ASSERT_EQ(r, UV_EINVAL);
#else
  ASSERT_EQ(r, UV_EFAULT);
#endif

  uv_close((uv_handle_t*) &client, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_create_early_bad_domain) {
  uv_tcp_t client;
  int r;

  r = uv_tcp_init_ex(uv_default_loop(), &client, 47);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_tcp_init_ex(uv_default_loop(), &client, 1024);
  ASSERT_EQ(r, UV_EINVAL);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_create_early_accept) {
  uv_tcp_t client, server;
  uv_connect_t connect_req;

  tcp_listener(uv_default_loop(), &server);
  tcp_connector(uv_default_loop(), &client, &connect_req);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
