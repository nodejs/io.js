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

#define CHECK_HANDLE(handle) \
  ASSERT_NE((uv_udp_t*)(handle) == &server || (uv_udp_t*)(handle) == &client, 0)

static uv_udp_t server;
static uv_udp_t client;

static int sv_recv_cb_called;

static int close_cb_called;


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  CHECK_HANDLE(handle);
  ASSERT_LE(suggested_size, sizeof(slab));
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  ASSERT(uv_is_closing(handle));
  close_cb_called++;
}


static void sv_recv_cb(uv_udp_t* handle,
                       ssize_t nread,
                       const uv_buf_t* rcvbuf,
                       const struct sockaddr* addr,
                       unsigned flags) {
  if (nread == 0) {
    ASSERT_NULL(addr);
    return;
  }

  ASSERT_EQ(4, nread);
  ASSERT_NOT_NULL(addr);

  if (!memcmp("EXIT", rcvbuf->base, nread)) {
    uv_close((uv_handle_t*) handle, close_cb);
    uv_close((uv_handle_t*) &client, close_cb);
  } else {
    ASSERT_MEM_EQ(rcvbuf->base, "HELO", 4);
  }

  sv_recv_cb_called++;

  if (sv_recv_cb_called == 2)
    uv_udp_recv_stop(handle);
}


TEST_IMPL(udp_try_send) {
  struct sockaddr_in addr;
  static char buffer[64 * 1024];
  uv_buf_t buf;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  r = uv_udp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_udp_recv_start(&server, alloc_cb, sv_recv_cb);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  buf = uv_buf_init(buffer, sizeof(buffer));

  r = uv_udp_try_send(&client, &buf, 0, (const struct sockaddr*) &addr);
  ASSERT_EQ(r, UV_EINVAL);

  r = uv_udp_try_send(&client, &buf, 1, (const struct sockaddr*) &addr);
  ASSERT_EQ(r, UV_EMSGSIZE);

  uv_buf_t* bufs[] = {&buf, &buf};
  unsigned int nbufs[] = {1, 1};
  struct sockaddr* addrs[] = {
    (struct sockaddr*) &addr,
    (struct sockaddr*) &addr,
  };

  ASSERT_EQ(0, sv_recv_cb_called);

  buf = uv_buf_init("HELO", 4);
  r = uv_udp_try_send2(&client, 2, bufs, nbufs, addrs, /*flags*/0);
  ASSERT_EQ(r, 2);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, sv_recv_cb_called);

  r = uv_udp_recv_start(&server, alloc_cb, sv_recv_cb);
  ASSERT_OK(r);

  buf = uv_buf_init("EXIT", 4);
  r = uv_udp_try_send(&client, &buf, 1, (const struct sockaddr*) &addr);
  ASSERT_EQ(4, r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, close_cb_called);
  ASSERT_EQ(3, sv_recv_cb_called);

  ASSERT_OK(client.send_queue_size);
  ASSERT_OK(server.send_queue_size);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
