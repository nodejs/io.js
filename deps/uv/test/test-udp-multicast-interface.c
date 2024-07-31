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

static int sv_send_cb_called;
static int close_cb_called;


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  close_cb_called++;
}


static void sv_send_cb(uv_udp_send_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT(status == 0 || status == UV_ENETUNREACH || status == UV_EPERM);
  CHECK_HANDLE(req->handle);

  sv_send_cb_called++;

  uv_close((uv_handle_t*) req->handle, close_cb);
}


TEST_IMPL(udp_multicast_interface) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  int r;
  uv_udp_send_t req;
  uv_buf_t buf;
  struct sockaddr_in addr;
  struct sockaddr_in baddr;

  ASSERT_OK(uv_ip4_addr("239.255.0.1", TEST_PORT, &addr));

  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &baddr));
  r = uv_udp_bind(&server, (const struct sockaddr*)&baddr, 0);
  ASSERT_OK(r);

  r = uv_udp_set_multicast_interface(&server, "0.0.0.0");
  ASSERT_OK(r);

  /* server sends "PING" */
  buf = uv_buf_init("PING", 4);
  r = uv_udp_send(&req,
                  &server,
                  &buf,
                  1,
                  (const struct sockaddr*)&addr,
                  sv_send_cb);
  ASSERT_OK(r);

  ASSERT_OK(close_cb_called);
  ASSERT_OK(sv_send_cb_called);

  /* run the loop till all events are processed */
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, sv_send_cb_called);
  ASSERT_EQ(1, close_cb_called);

  ASSERT_OK(client.send_queue_size);
  ASSERT_OK(server.send_queue_size);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
