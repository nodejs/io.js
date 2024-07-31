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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uv.h"
#include "task.h"

#define REQ_COUNT 10000

static uv_timer_t timer;
static uv_tcp_t server;
static uv_tcp_t client;
static uv_tcp_t incoming;
static int connect_cb_called;
static int close_cb_called;
static int connection_cb_called;
static int write_callbacks;
static int write_cancelled_callbacks;
static int write_error_callbacks;

static uv_write_t write_requests[REQ_COUNT];


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

static void timer_cb(uv_timer_t* handle) {
  uv_close((uv_handle_t*) &client, close_cb);
  uv_close((uv_handle_t*) &server, close_cb);
  uv_close((uv_handle_t*) &incoming, close_cb);
}

static void write_cb(uv_write_t* req, int status) {
  if (status == 0)
    write_callbacks++;
  else if (status == UV_ECANCELED)
    write_cancelled_callbacks++;
  else
    write_error_callbacks++;
}

static void connect_cb(uv_connect_t* req, int status) {
  static char base[1024];
  int r;
  int i;
  uv_buf_t buf;

  ASSERT_OK(status);
  connect_cb_called++;

  buf = uv_buf_init(base, sizeof(base));

  for (i = 0; i < REQ_COUNT; i++) {
    r = uv_write(&write_requests[i],
                 req->handle,
                 &buf,
                 1,
                 write_cb);
    ASSERT_OK(r);
  }
}


static void connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);

  ASSERT_OK(uv_tcp_init(tcp->loop, &incoming));
  ASSERT_OK(uv_accept(tcp, (uv_stream_t*) &incoming));

  ASSERT_OK(uv_timer_init(uv_default_loop(), &timer));
  ASSERT_OK(uv_timer_start(&timer, timer_cb, 1000, 0));

  connection_cb_called++;
}


static void start_server(void) {
  struct sockaddr_in addr;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &server));
  ASSERT_OK(uv_tcp_bind(&server, (struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 128, connection_cb));
}


TEST_IMPL(tcp_write_queue_order) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  int buffer_size = 16 * 1024;

  start_server();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client,
                           (struct sockaddr*) &addr,
                           connect_cb));
  ASSERT_OK(uv_send_buffer_size((uv_handle_t*) &client, &buffer_size));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, connection_cb_called);
  ASSERT_GT(write_callbacks, 0);
  ASSERT_GT(write_cancelled_callbacks, 0);
  ASSERT_EQ(write_callbacks +
            write_error_callbacks +
            write_cancelled_callbacks, REQ_COUNT);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
