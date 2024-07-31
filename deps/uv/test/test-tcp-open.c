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
#endif

static int shutdown_cb_called = 0;
static int shutdown_requested = 0;
static int connect_cb_called = 0;
static int write_cb_called = 0;
static int close_cb_called = 0;

static uv_connect_t connect_req;
static uv_shutdown_t shutdown_req;
static uv_write_t write_req;
static uv_timer_t tm;
static uv_tcp_t client;


static void startup(void) {
#ifdef _WIN32
    struct WSAData wsa_data;
    int r = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    ASSERT_OK(r);
#endif
}


static uv_os_sock_t create_tcp_socket(void) {
  uv_os_sock_t sock;

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
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


static void shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT_PTR_EQ(req, &shutdown_req);
  ASSERT_OK(status);

  /* Now we wait for the EOF */
  shutdown_cb_called++;
}


static void read_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_NOT_NULL(tcp);

  if (nread >= 0) {
    ASSERT_EQ(4, nread);
    ASSERT_OK(memcmp("PING", buf->base, nread));
  }
  else {
    ASSERT_EQ(nread, UV_EOF);
    uv_close((uv_handle_t*)tcp, close_cb);
  }
}


static void read1_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  int i;
  ASSERT_NOT_NULL(tcp);

  if (nread >= 0) {
    for (i = 0; i < nread; ++i)
      ASSERT_EQ(buf->base[i], 'P');
  } else {
    ASSERT_EQ(nread, UV_EOF);
    printf("GOT EOF\n");
    uv_close((uv_handle_t*)tcp, close_cb);
  }
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);

  if (status) {
    fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
    ASSERT(0);
  }

  write_cb_called++;
}


static void write1_cb(uv_write_t* req, int status) {
  uv_buf_t buf;
  int r;

  ASSERT_NOT_NULL(req);
  if (status) {
    ASSERT(shutdown_cb_called);
    return;
  }

  if (shutdown_requested)
    return;

  buf = uv_buf_init("P", 1);
  r = uv_write(&write_req, req->handle, &buf, 1, write1_cb);
  ASSERT_OK(r);

  write_cb_called++;
}


static void timer_cb(uv_timer_t* handle) {
  int r;

  /* Shutdown on drain. */
  r = uv_shutdown(&shutdown_req, (uv_stream_t*) &client, shutdown_cb);
  ASSERT_OK(r);
  shutdown_requested++;
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf = uv_buf_init("PING", 4);
  uv_stream_t* stream;
  int r;

  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);

  stream = req->handle;
  connect_cb_called++;

  r = uv_write(&write_req, stream, &buf, 1, write_cb);
  ASSERT_OK(r);

  /* Shutdown on drain. */
  r = uv_shutdown(&shutdown_req, stream, shutdown_cb);
  ASSERT_OK(r);

  /* Start reading */
  r = uv_read_start(stream, alloc_cb, read_cb);
  ASSERT_OK(r);
}


static void connect1_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  uv_stream_t* stream;
  int r;

  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);

  stream = req->handle;
  connect_cb_called++;

  r = uv_timer_init(uv_default_loop(), &tm);
  ASSERT_OK(r);

  r = uv_timer_start(&tm, timer_cb, 2000, 0);
  ASSERT_OK(r);

  buf = uv_buf_init("P", 1);
  r = uv_write(&write_req, stream, &buf, 1, write1_cb);
  ASSERT_OK(r);

  /* Start reading */
  r = uv_read_start(stream, alloc_cb, read1_cb);
  ASSERT_OK(r);
}


TEST_IMPL(tcp_open) {
  struct sockaddr_in addr;
  uv_os_sock_t sock;
  int r;
  uv_tcp_t client2;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  startup();
  sock = create_tcp_socket();

  r = uv_tcp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_tcp_open(&client, sock);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

#ifndef _WIN32
  {
    r = uv_tcp_init(uv_default_loop(), &client2);
    ASSERT_OK(r);

    r = uv_tcp_open(&client2, sock);
    ASSERT_EQ(r, UV_EEXIST);

    uv_close((uv_handle_t*) &client2, NULL);
  }
#else  /* _WIN32 */
  (void)client2;
#endif

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, shutdown_cb_called);
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_open_twice) {
  uv_tcp_t client;
  uv_os_sock_t sock1, sock2;
  int r;

  startup();
  sock1 = create_tcp_socket();
  sock2 = create_tcp_socket();

  r = uv_tcp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_tcp_open(&client, sock1);
  ASSERT_OK(r);

  r = uv_tcp_open(&client, sock2);
  ASSERT_EQ(r, UV_EBUSY);
  close_socket(sock2);

  uv_close((uv_handle_t*) &client, NULL);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_open_bound) {
  struct sockaddr_in addr;
  uv_tcp_t server;
  uv_os_sock_t sock;

  startup();
  sock = create_tcp_socket();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &server));

  ASSERT_OK(bind(sock, (struct sockaddr*) &addr, sizeof(addr)));

  ASSERT_OK(uv_tcp_open(&server, sock));

  ASSERT_OK(uv_listen((uv_stream_t*) &server, 128, NULL));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_open_connected) {
  struct sockaddr_in addr;
  uv_tcp_t client;
  uv_os_sock_t sock;
  uv_buf_t buf = uv_buf_init("PING", 4);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  startup();
  sock = create_tcp_socket();

  ASSERT_OK(connect(sock, (struct sockaddr*) &addr,  sizeof(addr)));

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &client));

  ASSERT_OK(uv_tcp_open(&client, sock));

  ASSERT_OK(uv_write(&write_req,
                     (uv_stream_t*) &client,
                     &buf,
                     1,
                     write_cb));

  ASSERT_OK(uv_shutdown(&shutdown_req,
                        (uv_stream_t*) &client,
                        shutdown_cb));

  ASSERT_OK(uv_read_start((uv_stream_t*) &client, alloc_cb, read_cb));

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, shutdown_cb_called);
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_write_ready) {
  struct sockaddr_in addr;
  uv_os_sock_t sock;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  startup();
  sock = create_tcp_socket();

  r = uv_tcp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_tcp_open(&client, sock);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &client,
                     (const struct sockaddr*) &addr,
                     connect1_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, shutdown_cb_called);
  ASSERT_EQ(1, shutdown_requested);
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_GT(write_cb_called, 0);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
