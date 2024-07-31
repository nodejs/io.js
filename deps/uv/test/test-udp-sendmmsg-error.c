/* Copyright libuv project and contributors. All rights reserved.
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

#define DATAGRAMS 6

static uv_udp_t client;
static uv_udp_send_t req[DATAGRAMS];

static int send_cb_called;
static int close_cb_called;


static void close_cb(uv_handle_t* handle) {
  ASSERT_PTR_EQ(handle, &client);
  ASSERT(uv_is_closing(handle));
  close_cb_called++;
}


static void send_cb(uv_udp_send_t* req, int status) {
  if (status != 0)
    ASSERT_EQ(status, UV_ECONNREFUSED);

  if (++send_cb_called == DATAGRAMS)
    uv_close((uv_handle_t*)&client, close_cb);
}


TEST_IMPL(udp_sendmmsg_error) {
  struct sockaddr_in addr;
  uv_buf_t buf;
  int i;

  ASSERT_OK(uv_udp_init(uv_default_loop(), &client));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_udp_connect(&client, (const struct sockaddr*)&addr));

  buf = uv_buf_init("TEST", 4);
  for (i = 0; i < DATAGRAMS; ++i)
    ASSERT_OK(uv_udp_send(&req[i], &client, &buf, 1, NULL, send_cb));

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(DATAGRAMS, send_cb_called);

  ASSERT_OK(client.send_queue_size);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
