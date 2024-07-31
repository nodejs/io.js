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
# include <unistd.h>
# include <sys/socket.h>
# include <sys/un.h>
#endif

static int send_cb_called = 0;
static int close_cb_called = 0;

static uv_udp_send_t send_req;


static void startup(void) {
#ifdef _WIN32
    struct WSAData wsa_data;
    int r = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    ASSERT_OK(r);
#endif
}


static uv_os_sock_t create_udp_socket(void) {
  uv_os_sock_t sock;

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
#ifdef _WIN32
  ASSERT_NE(sock, INVALID_SOCKET);
#else
  ASSERT_GE(sock, 0);
#endif

#ifndef _WIN32
  {
    /* Allow reuse of the port. */
    int yes = 1;
    int r = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    ASSERT_OK(r);
  }
#endif

  return sock;
}


static void close_socket(uv_os_sock_t sock) {
  int r;
#ifdef _WIN32
  r = closesocket(sock);
#else
  r = close(sock);
#endif
  ASSERT_OK(r);
}


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  ASSERT_LE(suggested_size, sizeof(slab));
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void recv_cb(uv_udp_t* handle,
                       ssize_t nread,
                       const uv_buf_t* buf,
                       const struct sockaddr* addr,
                       unsigned flags) {
  int r;

  if (nread < 0) {
    ASSERT(0 && "unexpected error");
  }

  if (nread == 0) {
    /* Returning unused buffer. Don't count towards sv_recv_cb_called */
    ASSERT_NULL(addr);
    return;
  }

  ASSERT_OK(flags);

  ASSERT_NOT_NULL(addr);
  ASSERT_EQ(4, nread);
  ASSERT_OK(memcmp("PING", buf->base, nread));

  r = uv_udp_recv_stop(handle);
  ASSERT_OK(r);

  uv_close((uv_handle_t*) handle, close_cb);
}


static void send_cb(uv_udp_send_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT_OK(status);

  send_cb_called++;
  uv_close((uv_handle_t*)req->handle, close_cb);
}


TEST_IMPL(udp_open) {
  struct sockaddr_in addr;
  uv_buf_t buf = uv_buf_init("PING", 4);
  uv_udp_t client, client2;
  uv_os_sock_t sock;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  startup();
  sock = create_udp_socket();

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_udp_open(&client, sock);
  ASSERT_OK(r);

  r = uv_udp_bind(&client, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_udp_recv_start(&client, alloc_cb, recv_cb);
  ASSERT_OK(r);

  r = uv_udp_send(&send_req,
                  &client,
                  &buf,
                  1,
                  (const struct sockaddr*) &addr,
                  send_cb);
  ASSERT_OK(r);

#ifndef _WIN32
  {
    r = uv_udp_init(uv_default_loop(), &client2);
    ASSERT_OK(r);

    r = uv_udp_open(&client2, sock);
    ASSERT_EQ(r, UV_EEXIST);

    uv_close((uv_handle_t*) &client2, NULL);
  }
#else  /* _WIN32 */
  (void)client2;
#endif

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, send_cb_called);
  ASSERT_EQ(1, close_cb_called);

  ASSERT_OK(client.send_queue_size);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(udp_open_twice) {
  uv_udp_t client;
  uv_os_sock_t sock1, sock2;
  int r;

  startup();
  sock1 = create_udp_socket();
  sock2 = create_udp_socket();

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_udp_open(&client, sock1);
  ASSERT_OK(r);

  r = uv_udp_open(&client, sock2);
  ASSERT_EQ(r, UV_EBUSY);
  close_socket(sock2);

  uv_close((uv_handle_t*) &client, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(udp_open_bound) {
  struct sockaddr_in addr;
  uv_udp_t client;
  uv_os_sock_t sock;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  startup();
  sock = create_udp_socket();

  r = bind(sock, (struct sockaddr*) &addr, sizeof(addr));
  ASSERT_OK(r);

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_udp_open(&client, sock);
  ASSERT_OK(r);

  r = uv_udp_recv_start(&client, alloc_cb, recv_cb);
  ASSERT_OK(r);

  uv_close((uv_handle_t*) &client, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(udp_open_connect) {
  struct sockaddr_in addr;
  uv_buf_t buf = uv_buf_init("PING", 4);
  uv_udp_t client;
  uv_udp_t server;
  uv_os_sock_t sock;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  startup();
  sock = create_udp_socket();

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = connect(sock, (const struct sockaddr*) &addr, sizeof(addr));
  ASSERT_OK(r);

  r = uv_udp_open(&client, sock);
  ASSERT_OK(r);

  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  r = uv_udp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_udp_recv_start(&server, alloc_cb, recv_cb);
  ASSERT_OK(r);

  r = uv_udp_send(&send_req,
                  &client,
                  &buf,
                  1,
                  NULL,
                  send_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, send_cb_called);
  ASSERT_EQ(2, close_cb_called);

  ASSERT_OK(client.send_queue_size);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#ifndef _WIN32
TEST_IMPL(udp_send_unix) {
  /* Test that "uv_udp_send()" supports sending over
     a "sockaddr_un" address. */
  struct sockaddr_un addr;
  uv_udp_t handle;
  uv_udp_send_t req;
  uv_loop_t* loop;
  uv_buf_t buf = uv_buf_init("PING", 4);
  int fd;
  int r;

  loop = uv_default_loop();

  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  ASSERT_LT(strlen(TEST_PIPENAME), sizeof(addr.sun_path));
  memcpy(addr.sun_path, TEST_PIPENAME, strlen(TEST_PIPENAME));

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);

  unlink(TEST_PIPENAME);
  ASSERT_OK(bind(fd, (const struct sockaddr*)&addr, sizeof addr));
  ASSERT_OK(listen(fd, 1));

  r = uv_udp_init(loop, &handle);
  ASSERT_OK(r);
  r = uv_udp_open(&handle, fd);
  ASSERT_OK(r);
  uv_run(loop, UV_RUN_DEFAULT);

  r = uv_udp_send(&req,
                  &handle,
                  &buf,
                  1,
                  (const struct sockaddr*) &addr,
                  NULL);
  ASSERT_OK(r);

  uv_close((uv_handle_t*)&handle, NULL);
  uv_run(loop, UV_RUN_DEFAULT);
  close(fd);
  unlink(TEST_PIPENAME);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
#endif
