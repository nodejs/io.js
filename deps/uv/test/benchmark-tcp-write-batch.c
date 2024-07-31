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

#define WRITE_REQ_DATA  "Hello, world."
#define NUM_WRITE_REQS  (1000 * 1000)

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req;


static write_req* write_reqs;
static uv_tcp_t tcp_client;
static uv_connect_t connect_req;
static uv_shutdown_t shutdown_req;

static int shutdown_cb_called = 0;
static int connect_cb_called = 0;
static int write_cb_called = 0;
static int close_cb_called = 0;

static void connect_cb(uv_connect_t* req, int status);
static void write_cb(uv_write_t* req, int status);
static void shutdown_cb(uv_shutdown_t* req, int status);
static void close_cb(uv_handle_t* handle);


static void connect_cb(uv_connect_t* req, int status) {
  write_req* w;
  int i;
  int r;

  ASSERT_PTR_EQ(req->handle, (uv_stream_t*)&tcp_client);

  for (i = 0; i < NUM_WRITE_REQS; i++) {
    w = &write_reqs[i];
    r = uv_write(&w->req, req->handle, &w->buf, 1, write_cb);
    ASSERT_OK(r);
  }

  r = uv_shutdown(&shutdown_req, req->handle, shutdown_cb);
  ASSERT_OK(r);

  connect_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);
  ASSERT_OK(status);
  write_cb_called++;
}


static void shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT_PTR_EQ(req->handle, (uv_stream_t*)&tcp_client);
  ASSERT_OK(req->handle->write_queue_size);

  uv_close((uv_handle_t*)req->handle, close_cb);
  free(write_reqs);

  shutdown_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_PTR_EQ(handle, (uv_handle_t*)&tcp_client);
  close_cb_called++;
}


BENCHMARK_IMPL(tcp_write_batch) {
  struct sockaddr_in addr;
  uv_loop_t* loop;
  uint64_t start;
  uint64_t stop;
  int i;
  int r;

  write_reqs = malloc(sizeof(*write_reqs) * NUM_WRITE_REQS);
  ASSERT_NOT_NULL(write_reqs);

  /* Prepare the data to write out. */
  for (i = 0; i < NUM_WRITE_REQS; i++) {
    write_reqs[i].buf = uv_buf_init(WRITE_REQ_DATA,
                                    sizeof(WRITE_REQ_DATA) - 1);
  }

  loop = uv_default_loop();
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(loop, &tcp_client);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &tcp_client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

  start = uv_hrtime();

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  stop = uv_hrtime();

  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(write_cb_called, NUM_WRITE_REQS);
  ASSERT_EQ(1, shutdown_cb_called);
  ASSERT_EQ(1, close_cb_called);

  printf("%ld write requests in %.2fs.\n",
         (long)NUM_WRITE_REQS,
         (stop - start) / 1e9);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
