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


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_EQ(status, UV_EADDRINUSE);
  uv_close((uv_handle_t*) req->handle, close_cb);
  connect_cb_called++;
}


TEST_IMPL(tcp_bind_error_addrinuse_connect) {
  struct sockaddr_in addr;
  int addrlen;
  uv_connect_t req;
  uv_tcp_t conn;

  /* 127.0.0.1:<TEST_PORT> is already taken by tcp4_echo_server running in
   * another process. uv_tcp_bind() and uv_tcp_connect() should still succeed
   * (greatest common denominator across platforms) but the connect callback
   * should receive an UV_EADDRINUSE error.
   */
  ASSERT_OK(uv_tcp_init(uv_default_loop(), &conn));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_bind(&conn, (const struct sockaddr*) &addr, 0));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT + 1, &addr));
  ASSERT_OK(uv_tcp_connect(&req,
                           &conn,
                           (const struct sockaddr*) &addr,
                           connect_cb));

  addrlen = sizeof(addr);
  ASSERT_EQ(UV_EADDRINUSE, uv_tcp_getsockname(&conn,
                                              (struct sockaddr*) &addr,
                                              &addrlen));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind_error_addrinuse_listen) {
  struct sockaddr_in addr;
  uv_tcp_t server1, server2;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  r = uv_tcp_init(uv_default_loop(), &server1);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server1, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_tcp_init(uv_default_loop(), &server2);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server2, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server1, 128, NULL);
  ASSERT_OK(r);
  r = uv_listen((uv_stream_t*)&server2, 128, NULL);
  ASSERT_EQ(r, UV_EADDRINUSE);

  uv_close((uv_handle_t*)&server1, close_cb);
  uv_close((uv_handle_t*)&server2, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind_error_addrnotavail_1) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  int r;

  ASSERT_OK(uv_ip4_addr("127.255.255.255", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  /* It seems that Linux is broken here - bind succeeds. */
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT(r == 0 || r == UV_EADDRNOTAVAIL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind_error_addrnotavail_2) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  int r;

  ASSERT_OK(uv_ip4_addr("4.4.4.4", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_EQ(r, UV_EADDRNOTAVAIL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind_error_fault) {
  char garbage[] =
      "blah blah blah blah blah blah blah blah blah blah blah blah";
  struct sockaddr_in* garbage_addr;
  uv_tcp_t server;
  int r;

  garbage_addr = (struct sockaddr_in*) &garbage;

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) garbage_addr, 0);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

/* Notes: On Linux uv_bind(server, NULL) will segfault the program.  */

TEST_IMPL(tcp_bind_error_inval) {
  struct sockaddr_in addr1;
  struct sockaddr_in addr2;
  uv_tcp_t server;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr1));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT_2, &addr2));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr1, 0);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr2, 0);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind_localhost_ok) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind_invalid_flags) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, UV_TCP_IPV6ONLY);
  ASSERT_EQ(r, UV_EINVAL);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_listen_without_bind) {
  int r;
  uv_tcp_t server;

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_listen((uv_stream_t*)&server, 128, NULL);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind_writable_flags) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  uv_buf_t buf;
  uv_write_t write_req;
  uv_shutdown_t shutdown_req;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);
  r = uv_listen((uv_stream_t*)&server, 128, NULL);
  ASSERT_OK(r);

  ASSERT_OK(uv_is_writable((uv_stream_t*) &server));
  ASSERT_OK(uv_is_readable((uv_stream_t*) &server));

  buf = uv_buf_init("PING", 4);
  r = uv_write(&write_req, (uv_stream_t*) &server, &buf, 1, NULL);
  ASSERT_EQ(r, UV_EPIPE);
  r = uv_shutdown(&shutdown_req, (uv_stream_t*) &server, NULL);
  ASSERT_EQ(r, UV_ENOTCONN);
  r = uv_read_start((uv_stream_t*) &server,
                    (uv_alloc_cb) abort,
                    (uv_read_cb) abort);
  ASSERT_EQ(r, UV_ENOTCONN);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(tcp_bind_or_listen_error_after_close) {
  uv_tcp_t tcp;
  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(9999);
  addr.sin_family = AF_INET;

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &tcp));
  uv_close((uv_handle_t*) &tcp, NULL);
  ASSERT_EQ(uv_tcp_bind(&tcp, (struct sockaddr*) &addr, 0), UV_EINVAL);
  ASSERT_EQ(uv_listen((uv_stream_t*) &tcp, 5, NULL), UV_EINVAL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
