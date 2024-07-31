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

static const int server_port = TEST_PORT;
/* Will be updated right after making the uv_connect_call */
static int connect_port = -1;

static int getsocknamecount_tcp = 0;
static int getpeernamecount = 0;
static int getsocknamecount_udp = 0;

static uv_loop_t* loop;
static uv_tcp_t tcp;
static uv_udp_t udp;
static uv_connect_t connect_req;
static uv_tcp_t tcpServer;
static uv_udp_t udpServer;
static uv_udp_send_t send_req;


static void alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}


static void on_close(uv_handle_t* peer) {
  free(peer);
  uv_close((uv_handle_t*)&tcpServer, NULL);
}


static void after_shutdown(uv_shutdown_t* req, int status) {
  uv_close((uv_handle_t*) req->handle, on_close);
  free(req);
}


static void after_read(uv_stream_t* handle,
                       ssize_t nread,
                       const uv_buf_t* buf) {
  uv_shutdown_t* req;
  int r;

  if (buf->base) {
    free(buf->base);
  }

  req = (uv_shutdown_t*) malloc(sizeof *req);
  r = uv_shutdown(req, handle, after_shutdown);
  ASSERT_OK(r);
}


static void check_sockname(struct sockaddr* addr, const char* compare_ip,
  int compare_port, const char* context) {
  struct sockaddr_in check_addr = *(struct sockaddr_in*) addr;
  struct sockaddr_in compare_addr;
  char check_ip[17];
  int r;

  ASSERT_OK(uv_ip4_addr(compare_ip, compare_port, &compare_addr));

  /* Both addresses should be ipv4 */
  ASSERT_EQ(check_addr.sin_family, AF_INET);
  ASSERT_EQ(compare_addr.sin_family, AF_INET);

  /* Check if the ip matches */
  ASSERT_OK(memcmp(&check_addr.sin_addr,
            &compare_addr.sin_addr,
            sizeof compare_addr.sin_addr));

  /* Check if the port matches. If port == 0 anything goes. */
  ASSERT(compare_port == 0 || check_addr.sin_port == compare_addr.sin_port);

  r = uv_ip4_name(&check_addr, (char*) check_ip, sizeof check_ip);
  ASSERT_OK(r);

  printf("%s: %s:%d\n", context, check_ip, ntohs(check_addr.sin_port));
}


static void on_connection(uv_stream_t* server, int status) {
  struct sockaddr sockname, peername;
  int namelen;
  uv_tcp_t* handle;
  int r;

  if (status != 0) {
    fprintf(stderr, "Connect error %s\n", uv_err_name(status));
  }
  ASSERT_OK(status);

  handle = malloc(sizeof(*handle));
  ASSERT_NOT_NULL(handle);

  r = uv_tcp_init(loop, handle);
  ASSERT_OK(r);

  /* associate server with stream */
  handle->data = server;

  r = uv_accept(server, (uv_stream_t*)handle);
  ASSERT_OK(r);

  namelen = sizeof sockname;
  r = uv_tcp_getsockname(handle, &sockname, &namelen);
  ASSERT_OK(r);
  check_sockname(&sockname, "127.0.0.1", server_port, "accepted socket");
  getsocknamecount_tcp++;

  namelen = sizeof peername;
  r = uv_tcp_getpeername(handle, &peername, &namelen);
  ASSERT_OK(r);
  check_sockname(&peername, "127.0.0.1", connect_port, "accepted socket peer");
  getpeernamecount++;

  r = uv_read_start((uv_stream_t*)handle, alloc, after_read);
  ASSERT_OK(r);
}


static void on_connect(uv_connect_t* req, int status) {
  struct sockaddr sockname, peername;
  int r, namelen;

  ASSERT_OK(status);

  namelen = sizeof sockname;
  r = uv_tcp_getsockname((uv_tcp_t*) req->handle, &sockname, &namelen);
  ASSERT_OK(r);
  check_sockname(&sockname, "127.0.0.1", 0, "connected socket");
  getsocknamecount_tcp++;

  namelen = sizeof peername;
  r = uv_tcp_getpeername((uv_tcp_t*) req->handle, &peername, &namelen);
  ASSERT_OK(r);
  check_sockname(&peername, "127.0.0.1", server_port, "connected socket peer");
  getpeernamecount++;

  uv_close((uv_handle_t*)&tcp, NULL);
}


static int tcp_listener(void) {
  struct sockaddr_in addr;
  struct sockaddr sockname, peername;
  int namelen;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", server_port, &addr));

  r = uv_tcp_init(loop, &tcpServer);
  if (r) {
    fprintf(stderr, "Socket creation error\n");
    return 1;
  }

  r = uv_tcp_bind(&tcpServer, (const struct sockaddr*) &addr, 0);
  if (r) {
    fprintf(stderr, "Bind error\n");
    return 1;
  }

  r = uv_listen((uv_stream_t*)&tcpServer, 128, on_connection);
  if (r) {
    fprintf(stderr, "Listen error\n");
    return 1;
  }

  memset(&sockname, -1, sizeof sockname);
  namelen = sizeof sockname;
  r = uv_tcp_getsockname(&tcpServer, &sockname, &namelen);
  ASSERT_OK(r);
  check_sockname(&sockname, "0.0.0.0", server_port, "server socket");
  getsocknamecount_tcp++;

  namelen = sizeof sockname;
  r = uv_tcp_getpeername(&tcpServer, &peername, &namelen);
  ASSERT_EQ(r, UV_ENOTCONN);
  getpeernamecount++;

  return 0;
}


static void tcp_connector(void) {
  struct sockaddr_in server_addr;
  struct sockaddr sockname;
  int r, namelen;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", server_port, &server_addr));

  r = uv_tcp_init(loop, &tcp);
  tcp.data = &connect_req;
  ASSERT(!r);

  r = uv_tcp_connect(&connect_req,
                     &tcp,
                     (const struct sockaddr*) &server_addr,
                     on_connect);
  ASSERT(!r);

  /* Fetch the actual port used by the connecting socket. */
  namelen = sizeof sockname;
  r = uv_tcp_getsockname(&tcp, &sockname, &namelen);
  ASSERT(!r);
  ASSERT_EQ(sockname.sa_family, AF_INET);
  connect_port = ntohs(((struct sockaddr_in*) &sockname)->sin_port);
  ASSERT_GT(connect_port, 0);
}


static void udp_recv(uv_udp_t* handle,
                     ssize_t nread,
                     const uv_buf_t* buf,
                     const struct sockaddr* addr,
                     unsigned flags) {
  struct sockaddr sockname;
  int namelen;
  int r;

  ASSERT_GE(nread, 0);
  free(buf->base);

  if (nread == 0) {
    return;
  }

  memset(&sockname, -1, sizeof sockname);
  namelen = sizeof(sockname);
  r = uv_udp_getsockname(&udp, &sockname, &namelen);
  ASSERT_OK(r);
  check_sockname(&sockname, "0.0.0.0", 0, "udp receiving socket");
  getsocknamecount_udp++;

  uv_close((uv_handle_t*) &udp, NULL);
  uv_close((uv_handle_t*) handle, NULL);
}


static void udp_send(uv_udp_send_t* req, int status) {

}


static int udp_listener(void) {
  struct sockaddr_in addr;
  struct sockaddr sockname;
  int namelen;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", server_port, &addr));

  r = uv_udp_init(loop, &udpServer);
  if (r) {
    fprintf(stderr, "Socket creation error\n");
    return 1;
  }

  r = uv_udp_bind(&udpServer, (const struct sockaddr*) &addr, 0);
  if (r) {
    fprintf(stderr, "Bind error\n");
    return 1;
  }

  memset(&sockname, -1, sizeof sockname);
  namelen = sizeof sockname;
  r = uv_udp_getsockname(&udpServer, &sockname, &namelen);
  ASSERT_OK(r);
  check_sockname(&sockname, "0.0.0.0", server_port, "udp listener socket");
  getsocknamecount_udp++;

  r = uv_udp_recv_start(&udpServer, alloc, udp_recv);
  ASSERT_OK(r);

  return 0;
}


static void udp_sender(void) {
  struct sockaddr_in server_addr;
  uv_buf_t buf;
  int r;

  r = uv_udp_init(loop, &udp);
  ASSERT(!r);

  buf = uv_buf_init("PING", 4);
  ASSERT_OK(uv_ip4_addr("127.0.0.1", server_port, &server_addr));

  r = uv_udp_send(&send_req,
                  &udp,
                  &buf,
                  1,
                  (const struct sockaddr*) &server_addr,
                  udp_send);
  ASSERT(!r);
}


TEST_IMPL(getsockname_tcp) {
  loop = uv_default_loop();

  if (tcp_listener())
    return 1;

  tcp_connector();

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(3, getsocknamecount_tcp);
  ASSERT_EQ(3, getpeernamecount);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(getsockname_udp) {
  loop = uv_default_loop();

  if (udp_listener())
    return 1;

  udp_sender();

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(2, getsocknamecount_udp);

  ASSERT_OK(udp.send_queue_size);
  ASSERT_OK(udpServer.send_queue_size);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
