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

#include "task.h"
#include "uv.h"

/* Update this is you're going to run > 1000 concurrent requests. */
#define MAX_CONNS 1000

#undef NANOSEC
#define NANOSEC ((uint64_t) 1e9)

#undef DEBUG
#define DEBUG 0

struct conn_rec_s;

typedef void (*setup_fn)(int num, void* arg);
typedef void (*make_connect_fn)(struct conn_rec_s* conn);
typedef int (*connect_fn)(int num, make_connect_fn make_connect, void* arg);

/* Base class for tcp_conn_rec and pipe_conn_rec.
 * The ordering of fields matters!
 */
typedef struct conn_rec_s {
  int i;
  uv_connect_t conn_req;
  uv_write_t write_req;
  make_connect_fn make_connect;
  uv_stream_t stream;
} conn_rec;

typedef struct {
  int i;
  uv_connect_t conn_req;
  uv_write_t write_req;
  make_connect_fn make_connect;
  uv_tcp_t stream;
} tcp_conn_rec;

typedef struct {
  int i;
  uv_connect_t conn_req;
  uv_write_t write_req;
  make_connect_fn make_connect;
  uv_pipe_t stream;
} pipe_conn_rec;

static char buffer[] = "QS";

static uv_loop_t* loop;

static tcp_conn_rec tcp_conns[MAX_CONNS];
static pipe_conn_rec pipe_conns[MAX_CONNS];

static uint64_t start; /* in ms  */
static int closed_streams;
static int conns_failed;

static void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void connect_cb(uv_connect_t* conn_req, int status);
static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void close_cb(uv_handle_t* handle);


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void after_write(uv_write_t* req, int status) {
  if (status != 0) {
    fprintf(stderr, "write error %s\n", uv_err_name(status));
    uv_close((uv_handle_t*)req->handle, close_cb);
    conns_failed++;
    return;
  }
}


static void connect_cb(uv_connect_t* req, int status) {
  conn_rec* conn;
  uv_buf_t buf;
  int r;

  if (status != 0) {
#if DEBUG
    fprintf(stderr, "connect error %s\n", uv_err_name(status));
#endif
    uv_close((uv_handle_t*)req->handle, close_cb);
    conns_failed++;
    return;
  }

  ASSERT_NOT_NULL(req);
  ASSERT_OK(status);

  conn = (conn_rec*)req->data;
  ASSERT_NOT_NULL(conn);

#if DEBUG
  printf("connect_cb %d\n", conn->i);
#endif

  r = uv_read_start(&conn->stream, alloc_cb, read_cb);
  ASSERT_OK(r);

  buf.base = buffer;
  buf.len = sizeof(buffer) - 1;

  r = uv_write(&conn->write_req, &conn->stream, &buf, 1, after_write);
  ASSERT_OK(r);
}


static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {

  ASSERT_NOT_NULL(stream);

#if DEBUG
  printf("read_cb %d\n", p->i);
#endif

  uv_close((uv_handle_t*)stream, close_cb);

  if (nread < 0) {
    if (nread == UV_EOF) {
      ;
    } else if (nread == UV_ECONNRESET) {
      conns_failed++;
    } else {
      fprintf(stderr, "read error %s\n", uv_err_name(nread));
      ASSERT(0);
    }
  }
}


static void close_cb(uv_handle_t* handle) {
  conn_rec* p = (conn_rec*)handle->data;

  ASSERT_NOT_NULL(handle);
  closed_streams++;

#if DEBUG
  printf("close_cb %d\n", p->i);
#endif

  if (uv_now(loop) - start < 10000) {
    p->make_connect(p);
  }
}


static void tcp_do_setup(int num, void* arg) {
  int i;

  for (i = 0; i < num; i++) {
    tcp_conns[i].i = i;
  }
}


static void pipe_do_setup(int num, void* arg) {
  int i;

  for (i = 0; i < num; i++) {
    pipe_conns[i].i = i;
  }
}


static void tcp_make_connect(conn_rec* p) {
  struct sockaddr_in addr;
  tcp_conn_rec* tp;
  int r;

  tp = (tcp_conn_rec*) p;

  r = uv_tcp_init(loop, (uv_tcp_t*)&p->stream);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_connect(&tp->conn_req,
                     (uv_tcp_t*) &p->stream,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  if (r) {
    fprintf(stderr, "uv_tcp_connect error %s\n", uv_err_name(r));
    ASSERT(0);
  }

#if DEBUG
  printf("make connect %d\n", p->i);
#endif

  p->conn_req.data = p;
  p->write_req.data = p;
  p->stream.data = p;
}


static void pipe_make_connect(conn_rec* p) {
  int r;

  r = uv_pipe_init(loop, (uv_pipe_t*)&p->stream, 0);
  ASSERT_OK(r);

  uv_pipe_connect(&((pipe_conn_rec*) p)->conn_req,
                  (uv_pipe_t*) &p->stream,
                  TEST_PIPENAME,
                  connect_cb);

#if DEBUG
  printf("make connect %d\n", p->i);
#endif

  p->conn_req.data = p;
  p->write_req.data = p;
  p->stream.data = p;
}


static int tcp_do_connect(int num, make_connect_fn make_connect, void* arg) {
  int i;

  for (i = 0; i < num; i++) {
    tcp_make_connect((conn_rec*)&tcp_conns[i]);
    tcp_conns[i].make_connect = make_connect;
  }

  return 0;
}


static int pipe_do_connect(int num, make_connect_fn make_connect, void* arg) {
  int i;

  for (i = 0; i < num; i++) {
    pipe_make_connect((conn_rec*)&pipe_conns[i]);
    pipe_conns[i].make_connect = make_connect;
  }

  return 0;
}


static int pound_it(int concurrency,
                    const char* type,
                    setup_fn do_setup,
                    connect_fn do_connect,
                    make_connect_fn make_connect,
                    void* arg) {
  double secs;
  int r;
  uint64_t start_time; /* in ns */
  uint64_t end_time;

  loop = uv_default_loop();

  uv_update_time(loop);
  start = uv_now(loop);

  /* Run benchmark for at least five seconds. */
  start_time = uv_hrtime();

  do_setup(concurrency, arg);

  r = do_connect(concurrency, make_connect, arg);
  ASSERT(!r);

  uv_run(loop, UV_RUN_DEFAULT);

  end_time = uv_hrtime();

  /* Number of fractional seconds it took to run the benchmark. */
  secs = (double)(end_time - start_time) / NANOSEC;

  fprintf(stderr, "%s-conn-pound-%d: %.0f accepts/s (%d failed)\n",
          type,
          concurrency,
          closed_streams / secs,
          conns_failed);
  fflush(stderr);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


BENCHMARK_IMPL(tcp4_pound_100) {
  return pound_it(100,
                  "tcp",
                  tcp_do_setup,
                  tcp_do_connect,
                  tcp_make_connect,
                  NULL);
}


BENCHMARK_IMPL(tcp4_pound_1000) {
  return pound_it(1000,
                  "tcp",
                  tcp_do_setup,
                  tcp_do_connect,
                  tcp_make_connect,
                  NULL);
}


BENCHMARK_IMPL(pipe_pound_100) {
  return pound_it(100,
                  "pipe",
                  pipe_do_setup,
                  pipe_do_connect,
                  pipe_make_connect,
                  NULL);
}


BENCHMARK_IMPL(pipe_pound_1000) {
  return pound_it(1000,
                  "pipe",
                  pipe_do_setup,
                  pipe_do_connect,
                  pipe_make_connect,
                  NULL);
}
