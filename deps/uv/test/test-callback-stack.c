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

/*
 * TODO: Add explanation of why we want on_close to be called from fresh
 * stack.
 */

#include "uv.h"
#include "task.h"


static const char MESSAGE[] = "Failure is for the weak. Everyone dies alone.";

static uv_tcp_t client;
static uv_timer_t timer;
static uv_connect_t connect_req;
static uv_write_t write_req;
static uv_shutdown_t shutdown_req;

static int nested = 0;
static int close_cb_called = 0;
static int connect_cb_called = 0;
static int write_cb_called = 0;
static int timer_cb_called = 0;
static int bytes_received = 0;
static int shutdown_cb_called = 0;


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->len = size;
  buf->base = malloc(size);
  ASSERT_NOT_NULL(buf->base);
}


static void close_cb(uv_handle_t* handle) {
  ASSERT(nested == 0 && "close_cb must be called from a fresh stack");

  close_cb_called++;
}


static void shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT_OK(status);
  ASSERT(nested == 0 && "shutdown_cb must be called from a fresh stack");

  shutdown_cb_called++;
}


static void read_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  ASSERT(nested == 0 && "read_cb must be called from a fresh stack");

  printf("Read. nread == %d\n", (int)nread);
  free(buf->base);

  if (nread == 0) {
    return;

  } else if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);

    nested++;
    uv_close((uv_handle_t*)tcp, close_cb);
    nested--;

    return;
  }

  bytes_received += nread;

  /* We call shutdown here because when bytes_received == sizeof MESSAGE there
   * will be no more data sent nor received, so here it would be possible for a
   * backend to call shutdown_cb immediately and *not* from a fresh stack. */
  if (bytes_received == sizeof MESSAGE) {
    nested++;

    puts("Shutdown");

    if (uv_shutdown(&shutdown_req, (uv_stream_t*)tcp, shutdown_cb)) {
      FATAL("uv_shutdown failed");
    }
    nested--;
  }
}


static void timer_cb(uv_timer_t* handle) {
  ASSERT_PTR_EQ(handle, &timer);
  ASSERT(nested == 0 && "timer_cb must be called from a fresh stack");

  puts("Timeout complete. Now read data...");

  nested++;
  if (uv_read_start((uv_stream_t*)&client, alloc_cb, read_cb)) {
    FATAL("uv_read_start failed");
  }
  nested--;

  timer_cb_called++;

  uv_close((uv_handle_t*)handle, close_cb);
}


static void write_cb(uv_write_t* req, int status) {
  int r;

  ASSERT_OK(status);
  ASSERT(nested == 0 && "write_cb must be called from a fresh stack");

  puts("Data written. 500ms timeout...");

  /* After the data has been sent, we're going to wait for a while, then start
   * reading. This makes us certain that the message has been echoed back to
   * our receive buffer when we start reading. This maximizes the temptation
   * for the backend to use dirty stack for calling read_cb. */
  nested++;
  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);
  r = uv_timer_start(&timer, timer_cb, 500, 0);
  ASSERT_OK(r);
  nested--;

  write_cb_called++;
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;

  puts("Connected. Write some data to echo server...");

  ASSERT_OK(status);
  ASSERT(nested == 0 && "connect_cb must be called from a fresh stack");

  nested++;

  buf.base = (char*) &MESSAGE;
  buf.len = sizeof MESSAGE;

  if (uv_write(&write_req, (uv_stream_t*)req->handle, &buf, 1, write_cb)) {
    FATAL("uv_write failed");
  }

  nested--;

  connect_cb_called++;
}


TEST_IMPL(callback_stack) {
  struct sockaddr_in addr;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  if (uv_tcp_init(uv_default_loop(), &client)) {
    FATAL("uv_tcp_init failed");
  }

  puts("Connecting...");

  nested++;

  if (uv_tcp_connect(&connect_req,
                     &client,
                     (const struct sockaddr*) &addr,
                     connect_cb)) {
    FATAL("uv_tcp_connect failed");
  }
  nested--;

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_OK(nested);
  ASSERT_NE(connect_cb_called == 1 && \
            "connect_cb must be called exactly once", 0);
  ASSERT_NE(write_cb_called == 1 && "write_cb must be called exactly once",
            0);
  ASSERT_NE(timer_cb_called == 1 && "timer_cb must be called exactly once",
            0);
  ASSERT_EQ(bytes_received, sizeof MESSAGE);
  ASSERT_NE(shutdown_cb_called == 1 && \
            "shutdown_cb must be called exactly once", 0);
  ASSERT_NE(close_cb_called == 2 && "close_cb must be called exactly twice",
            0);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
