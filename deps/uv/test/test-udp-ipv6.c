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

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/sysctl.h>
#endif

#define CHECK_HANDLE(handle)                   \
  ASSERT_NE((uv_udp_t*)(handle) == &server     \
         || (uv_udp_t*)(handle) == &client     \
         || (uv_timer_t*)(handle) == &timeout, 0)

#define CHECK_REQ(req) \
  ASSERT_PTR_EQ((req), &req_);

static uv_udp_t client;
static uv_udp_t server;
static uv_udp_send_t req_;
static char data[10];
static uv_timer_t timeout;

static int send_cb_called;
static int recv_cb_called;
static int close_cb_called;
static uint16_t client_port;

#if defined(__FreeBSD__) || defined(__NetBSD__)
static int can_ipv6_ipv4_dual(void) {
  int v6only;
  size_t size = sizeof(int);

  if (sysctlbyname("net.inet6.ip6.v6only", &v6only, &size, NULL, 0))
    return 0;

  return v6only != 1;
}
#endif


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  CHECK_HANDLE(handle);
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  close_cb_called++;
}


static void send_cb(uv_udp_send_t* req, int status) {
  CHECK_REQ(req);
  CHECK_HANDLE(req->handle);
  ASSERT_OK(status);
  send_cb_called++;
}

static int is_from_client(const struct sockaddr* addr) {
  const struct sockaddr_in6* addr6;
  char dst[256];
  int r;

  /* Debugging output, and filter out unwanted network traffic */
  if (addr != NULL) {
    ASSERT_EQ(addr->sa_family, AF_INET6);
    addr6 = (struct sockaddr_in6*) addr;
    r = uv_inet_ntop(addr->sa_family, &addr6->sin6_addr, dst, sizeof(dst));
    if (r == 0)
      printf("from [%.*s]:%d\n", (int) sizeof(dst), dst, addr6->sin6_port);
    if (addr6->sin6_port != client_port)
      return 0;
    if (r != 0 || strcmp(dst, "::ffff:127.0.0.1"))
      return 0;
  }
  return 1;
}


static void ipv6_recv_fail(uv_udp_t* handle,
                           ssize_t nread,
                           const uv_buf_t* buf,
                           const struct sockaddr* addr,
                           unsigned flags) {
  printf("got %d %.*s\n", (int) nread, nread > 0 ? (int) nread : 0, buf->base);
  if (!is_from_client(addr) || (nread == 0 && addr == NULL))
    return;
  ASSERT(0 && "this function should not have been called");
}


static void ipv6_recv_ok(uv_udp_t* handle,
                         ssize_t nread,
                         const uv_buf_t* buf,
                         const struct sockaddr* addr,
                         unsigned flags) {
  CHECK_HANDLE(handle);

  printf("got %d %.*s\n", (int) nread, nread > 0 ? (int) nread : 0, buf->base);
  if (!is_from_client(addr) || (nread == 0 && addr == NULL))
    return;

  ASSERT_EQ(9, nread);
  ASSERT(!memcmp(buf->base, data, 9));
  recv_cb_called++;
}


static void timeout_cb(uv_timer_t* timer) {
  uv_close((uv_handle_t*)&server, close_cb);
  uv_close((uv_handle_t*)&client, close_cb);
  uv_close((uv_handle_t*)&timeout, close_cb);
}


static void do_test(uv_udp_recv_cb recv_cb, int bind_flags) {
  struct sockaddr_in6 addr6;
  struct sockaddr_in addr;
  int addr6_len;
  int addr_len;
  uv_buf_t buf;
  char dst[256];
  int r;

  ASSERT_OK(uv_ip6_addr("::0", TEST_PORT, &addr6));

  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  r = uv_udp_bind(&server, (const struct sockaddr*) &addr6, bind_flags);
  ASSERT_OK(r);

  addr6_len = sizeof(addr6);
  ASSERT_OK(uv_udp_getsockname(&server,
                               (struct sockaddr*) &addr6,
                               &addr6_len));
  ASSERT_OK(uv_inet_ntop(addr6.sin6_family,
                         &addr6.sin6_addr,
                         dst,
                         sizeof(dst)));
  printf("on [%.*s]:%d\n", (int) sizeof(dst), dst, addr6.sin6_port);

  r = uv_udp_recv_start(&server, alloc_cb, recv_cb);
  ASSERT_OK(r);

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_inet_ntop(addr.sin_family, &addr.sin_addr, dst, sizeof(dst)));
  printf("to [%.*s]:%d\n", (int) sizeof(dst), dst, addr.sin_port);

  /* Create some unique data to send */
  ASSERT_EQ(9, snprintf(data,
                        sizeof(data),
                        "PING%5u",
                        uv_os_getpid() & 0xFFFF));
  buf = uv_buf_init(data, 9);
  printf("sending %s\n", data);

  r = uv_udp_send(&req_,
                  &client,
                  &buf,
                  1,
                  (const struct sockaddr*) &addr,
                  send_cb);
  ASSERT_OK(r);

  addr_len = sizeof(addr);
  ASSERT_OK(uv_udp_getsockname(&client, (struct sockaddr*) &addr, &addr_len));
  ASSERT_OK(uv_inet_ntop(addr.sin_family, &addr.sin_addr, dst, sizeof(dst)));
  printf("from [%.*s]:%d\n", (int) sizeof(dst), dst, addr.sin_port);
  client_port = addr.sin_port;

  r = uv_timer_init(uv_default_loop(), &timeout);
  ASSERT_OK(r);

  r = uv_timer_start(&timeout, timeout_cb, 500, 0);
  ASSERT_OK(r);

  ASSERT_OK(close_cb_called);
  ASSERT_OK(send_cb_called);
  ASSERT_OK(recv_cb_called);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
}


TEST_IMPL(udp_dual_stack) {
#if defined(__CYGWIN__) || defined(__MSYS__)
  /* FIXME: Does Cygwin support this?  */
  RETURN_SKIP("FIXME: This test needs more investigation on Cygwin");
#endif

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

#if defined(__FreeBSD__) || defined(__NetBSD__)
  if (!can_ipv6_ipv4_dual())
    RETURN_SKIP("IPv6-IPv4 dual stack not supported");
#elif defined(__OpenBSD__)
  RETURN_SKIP("IPv6-IPv4 dual stack not supported");
#endif

  do_test(ipv6_recv_ok, 0);

  printf("recv_cb_called %d\n", recv_cb_called);
  printf("send_cb_called %d\n", send_cb_called);
  ASSERT_EQ(1, recv_cb_called);
  ASSERT_EQ(1, send_cb_called);

  return 0;
}


TEST_IMPL(udp_ipv6_only) {
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  do_test(ipv6_recv_fail, UV_UDP_IPV6ONLY);

  ASSERT_OK(recv_cb_called);
  ASSERT_EQ(1, send_cb_called);

  return 0;
}
