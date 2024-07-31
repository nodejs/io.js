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


#define WRITES            3
#if defined(__arm__) /* Decrease the chunks so the test passes on arm CI bots */
#define CHUNKS_PER_WRITE  2048
#else
#define CHUNKS_PER_WRITE  4096
#endif
#define CHUNK_SIZE        10024 /* 10 kb */

#define TOTAL_BYTES       (WRITES * CHUNKS_PER_WRITE * CHUNK_SIZE)

static char* send_buffer;

static int shutdown_cb_called = 0;
static int connect_cb_called = 0;
static int write_cb_called = 0;
static int close_cb_called = 0;
static size_t bytes_sent = 0;
static size_t bytes_sent_done = 0;
static size_t bytes_received_done = 0;

static uv_connect_t connect_req;
static uv_shutdown_t shutdown_req;
static uv_write_t write_reqs[WRITES];


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  buf->len = size;
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void shutdown_cb(uv_shutdown_t* req, int status) {
  uv_tcp_t* tcp;

  ASSERT_PTR_EQ(req, &shutdown_req);
  ASSERT_OK(status);

  tcp = (uv_tcp_t*)(req->handle);

  /* The write buffer should be empty by now. */
  ASSERT_OK(tcp->write_queue_size);

  /* Now we wait for the EOF */
  shutdown_cb_called++;

  /* We should have had all the writes called already. */
  ASSERT_EQ(write_cb_called, WRITES);
}


static void read_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_NOT_NULL(tcp);

  if (nread >= 0) {
    bytes_received_done += nread;
  }
  else {
    ASSERT_EQ(nread, UV_EOF);
    printf("GOT EOF\n");
    uv_close((uv_handle_t*)tcp, close_cb);
  }

  free(buf->base);
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_NOT_NULL(req);

  if (status) {
    fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));
    ASSERT(0);
  }

  bytes_sent_done += CHUNKS_PER_WRITE * CHUNK_SIZE;
  write_cb_called++;
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_buf_t send_bufs[CHUNKS_PER_WRITE];
  uv_stream_t* stream;
  int i, j, r;

  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);

  stream = req->handle;
  connect_cb_called++;

  /* Write a lot of data */
  for (i = 0; i < WRITES; i++) {
    uv_write_t* write_req = write_reqs + i;

    for (j = 0; j < CHUNKS_PER_WRITE; j++) {
      send_bufs[j] = uv_buf_init(send_buffer + bytes_sent, CHUNK_SIZE);
      bytes_sent += CHUNK_SIZE;
    }

    r = uv_write(write_req, stream, send_bufs, CHUNKS_PER_WRITE, write_cb);
    ASSERT_OK(r);
  }

  /* Shutdown on drain. */
  r = uv_shutdown(&shutdown_req, stream, shutdown_cb);
  ASSERT_OK(r);

  /* Start reading */
  r = uv_read_start(stream, alloc_cb, read_cb);
  ASSERT_OK(r);
}


TEST_IMPL(tcp_writealot) {
  struct sockaddr_in addr;
  uv_tcp_t client;
  int r;

#if defined(__MSAN__) || defined(__TSAN__)
  RETURN_SKIP("Test is too slow to run under "
              "MemorySanitizer or ThreadSanitizer");
#endif

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  send_buffer = calloc(1, TOTAL_BYTES);
  ASSERT_NOT_NULL(send_buffer);

  r = uv_tcp_init(uv_default_loop(), &client);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &client,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, shutdown_cb_called);
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(write_cb_called, WRITES);
  ASSERT_EQ(1, close_cb_called);
  ASSERT_EQ(bytes_sent, TOTAL_BYTES);
  ASSERT_EQ(bytes_sent_done, TOTAL_BYTES);
  ASSERT_EQ(bytes_received_done, TOTAL_BYTES);

  free(send_buffer);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
