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

#include <math.h>
#include <stdio.h>


static int TARGET_CONNECTIONS;
#define WRITE_BUFFER_SIZE           8192
#define MAX_SIMULTANEOUS_CONNECTS   100

#define PRINT_STATS                 0
#define STATS_INTERVAL              1000 /* msec */
#define STATS_COUNT                 5


static void do_write(uv_stream_t*);
static void maybe_connect_some(void);

static uv_req_t* req_alloc(void);
static void req_free(uv_req_t* uv_req);

static void buf_alloc(uv_handle_t* handle, size_t size, uv_buf_t* buf);
static void buf_free(const uv_buf_t* buf);

static uv_loop_t* loop;

static uv_tcp_t tcpServer;
static uv_pipe_t pipeServer;
static uv_stream_t* server;
static struct sockaddr_in listen_addr;
static struct sockaddr_in connect_addr;

static int64_t start_time;

static int max_connect_socket = 0;
static int max_read_sockets = 0;
static int read_sockets = 0;
static int write_sockets = 0;

static int64_t nrecv = 0;
static int64_t nrecv_total = 0;
static int64_t nsent = 0;
static int64_t nsent_total = 0;

static int stats_left = 0;

static char write_buffer[WRITE_BUFFER_SIZE];

/* Make this as large as you need. */
#define MAX_WRITE_HANDLES 1000

static stream_type type;

static uv_tcp_t tcp_write_handles[MAX_WRITE_HANDLES];
static uv_pipe_t pipe_write_handles[MAX_WRITE_HANDLES];

static uv_timer_t timer_handle;


static double gbit(int64_t bytes, int64_t passed_ms) {
  double gbits = ((double)bytes / (1024 * 1024 * 1024)) * 8;
  return gbits / ((double)passed_ms / 1000);
}


static void show_stats(uv_timer_t* handle) {
  int64_t diff;
  int i;

#if PRINT_STATS
  fprintf(stderr, "connections: %d, write: %.1f gbit/s\n",
          write_sockets,
          gbit(nsent, STATS_INTERVAL));
  fflush(stderr);
#endif

  /* Exit if the show is over */
  if (!--stats_left) {

    uv_update_time(loop);
    diff = uv_now(loop) - start_time;

    fprintf(stderr, "%s_pump%d_client: %.1f gbit/s\n",
            type == TCP ? "tcp" : "pipe",
            write_sockets,
            gbit(nsent_total, diff));
    fflush(stderr);

    for (i = 0; i < write_sockets; i++) {
      if (type == TCP)
        uv_close((uv_handle_t*) &tcp_write_handles[i], NULL);
      else
        uv_close((uv_handle_t*) &pipe_write_handles[i], NULL);
    }

    exit(0);
  }

  /* Reset read and write counters */
  nrecv = 0;
  nsent = 0;
}


static void read_show_stats(void) {
  int64_t diff;

  uv_update_time(loop);
  diff = uv_now(loop) - start_time;

  fprintf(stderr, "%s_pump%d_server: %.1f gbit/s\n",
          type == TCP ? "tcp" : "pipe",
          max_read_sockets,
          gbit(nrecv_total, diff));
  fflush(stderr);
}



static void read_sockets_close_cb(uv_handle_t* handle) {
  free(handle);
  read_sockets--;

  /* If it's past the first second and everyone has closed their connection
   * Then print stats.
   */
  if (uv_now(loop) - start_time > 1000 && read_sockets == 0) {
    read_show_stats();
    uv_close((uv_handle_t*)server, NULL);
  }
}


static void start_stats_collection(void) {
  int r;

  /* Show-stats timer */
  stats_left = STATS_COUNT;
  r = uv_timer_init(loop, &timer_handle);
  ASSERT_OK(r);
  r = uv_timer_start(&timer_handle, show_stats, STATS_INTERVAL, STATS_INTERVAL);
  ASSERT_OK(r);

  uv_update_time(loop);
  start_time = uv_now(loop);
}


static void read_cb(uv_stream_t* stream, ssize_t bytes, const uv_buf_t* buf) {
  if (nrecv_total == 0) {
    ASSERT_OK(start_time);
    uv_update_time(loop);
    start_time = uv_now(loop);
  }

  if (bytes < 0) {
    uv_close((uv_handle_t*)stream, read_sockets_close_cb);
    return;
  }

  buf_free(buf);

  nrecv += bytes;
  nrecv_total += bytes;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);

  req_free((uv_req_t*) req);

  nsent += sizeof write_buffer;
  nsent_total += sizeof write_buffer;

  do_write((uv_stream_t*) req->handle);
}


static void do_write(uv_stream_t* stream) {
  uv_write_t* req;
  uv_buf_t buf;
  int r;

  buf.base = (char*) &write_buffer;
  buf.len = sizeof write_buffer;

  req = (uv_write_t*) req_alloc();
  r = uv_write(req, stream, &buf, 1, write_cb);
  ASSERT_OK(r);
}


static void connect_cb(uv_connect_t* req, int status) {
  int i;

  if (status) {
    fprintf(stderr, "%s", uv_strerror(status));
    fflush(stderr);
  }
  ASSERT_OK(status);

  write_sockets++;
  req_free((uv_req_t*) req);

  maybe_connect_some();

  if (write_sockets == TARGET_CONNECTIONS) {
    start_stats_collection();

    /* Yay! start writing */
    for (i = 0; i < write_sockets; i++) {
      if (type == TCP)
        do_write((uv_stream_t*) &tcp_write_handles[i]);
      else
        do_write((uv_stream_t*) &pipe_write_handles[i]);
    }
  }
}


static void maybe_connect_some(void) {
  uv_connect_t* req;
  uv_tcp_t* tcp;
  uv_pipe_t* pipe;
  int r;

  while (max_connect_socket < TARGET_CONNECTIONS &&
         max_connect_socket < write_sockets + MAX_SIMULTANEOUS_CONNECTS) {
    if (type == TCP) {
      tcp = &tcp_write_handles[max_connect_socket++];

      r = uv_tcp_init(loop, tcp);
      ASSERT_OK(r);

      req = (uv_connect_t*) req_alloc();
      r = uv_tcp_connect(req,
                         tcp,
                         (const struct sockaddr*) &connect_addr,
                         connect_cb);
      ASSERT_OK(r);
    } else {
      pipe = &pipe_write_handles[max_connect_socket++];

      r = uv_pipe_init(loop, pipe, 0);
      ASSERT_OK(r);

      req = (uv_connect_t*) req_alloc();
      uv_pipe_connect(req, pipe, TEST_PIPENAME, connect_cb);
    }
  }
}


static void connection_cb(uv_stream_t* s, int status) {
  uv_stream_t* stream;
  int r;

  ASSERT_PTR_EQ(server, s);
  ASSERT_OK(status);

  if (type == TCP) {
    stream = (uv_stream_t*)malloc(sizeof(uv_tcp_t));
    r = uv_tcp_init(loop, (uv_tcp_t*)stream);
    ASSERT_OK(r);
  } else {
    stream = (uv_stream_t*)malloc(sizeof(uv_pipe_t));
    r = uv_pipe_init(loop, (uv_pipe_t*)stream, 0);
    ASSERT_OK(r);
  }

  r = uv_accept(s, stream);
  ASSERT_OK(r);

  r = uv_read_start(stream, buf_alloc, read_cb);
  ASSERT_OK(r);

  read_sockets++;
  max_read_sockets++;
}


/*
 * Request allocator
 */

typedef struct req_list_s {
  union uv_any_req uv_req;
  struct req_list_s* next;
} req_list_t;


static req_list_t* req_freelist = NULL;


static uv_req_t* req_alloc(void) {
  req_list_t* req;

  req = req_freelist;
  if (req != NULL) {
    req_freelist = req->next;
    return (uv_req_t*) req;
  }

  req = (req_list_t*) malloc(sizeof *req);
  return (uv_req_t*) req;
}


static void req_free(uv_req_t* uv_req) {
  req_list_t* req = (req_list_t*) uv_req;

  req->next = req_freelist;
  req_freelist = req;
}


/*
 * Buffer allocator
 */

typedef struct buf_list_s {
  uv_buf_t uv_buf_t;
  struct buf_list_s* next;
} buf_list_t;


static buf_list_t* buf_freelist = NULL;


static void buf_alloc(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf_list_t* ab;

  ab = buf_freelist;
  if (ab != NULL)
    buf_freelist = ab->next;
  else {
    ab = malloc(size + sizeof(*ab));
    ab->uv_buf_t.len = size;
    ab->uv_buf_t.base = (char*) (ab + 1);
  }

  *buf = ab->uv_buf_t;
}


static void buf_free(const uv_buf_t* buf) {
  buf_list_t* ab = (buf_list_t*) buf->base - 1;
  ab->next = buf_freelist;
  buf_freelist = ab;
}


HELPER_IMPL(tcp_pump_server) {
  int r;

  type = TCP;
  loop = uv_default_loop();

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &listen_addr));

  /* Server */
  server = (uv_stream_t*)&tcpServer;
  r = uv_tcp_init(loop, &tcpServer);
  ASSERT_OK(r);
  r = uv_tcp_bind(&tcpServer, (const struct sockaddr*) &listen_addr, 0);
  ASSERT_OK(r);
  r = uv_listen((uv_stream_t*)&tcpServer, MAX_WRITE_HANDLES, connection_cb);
  ASSERT_OK(r);

  notify_parent_process();
  uv_run(loop, UV_RUN_DEFAULT);

  return 0;
}


HELPER_IMPL(pipe_pump_server) {
  int r;
  type = PIPE;

  loop = uv_default_loop();

  /* Server */
  server = (uv_stream_t*)&pipeServer;
  r = uv_pipe_init(loop, &pipeServer, 0);
  ASSERT_OK(r);
  r = uv_pipe_bind(&pipeServer, TEST_PIPENAME);
  ASSERT_OK(r);
  r = uv_listen((uv_stream_t*)&pipeServer, MAX_WRITE_HANDLES, connection_cb);
  ASSERT_OK(r);

  notify_parent_process();
  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


static void tcp_pump(int n) {
  ASSERT_LE(n, MAX_WRITE_HANDLES);
  TARGET_CONNECTIONS = n;
  type = TCP;

  loop = uv_default_loop();

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &connect_addr));

  /* Start making connections */
  maybe_connect_some();

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
}


static void pipe_pump(int n) {
  ASSERT_LE(n, MAX_WRITE_HANDLES);
  TARGET_CONNECTIONS = n;
  type = PIPE;

  loop = uv_default_loop();

  /* Start making connections */
  maybe_connect_some();

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
}


BENCHMARK_IMPL(tcp_pump100_client) {
  tcp_pump(100);
  return 0;
}


BENCHMARK_IMPL(tcp_pump1_client) {
  tcp_pump(1);
  return 0;
}


BENCHMARK_IMPL(pipe_pump100_client) {
  pipe_pump(100);
  return 0;
}


BENCHMARK_IMPL(pipe_pump1_client) {
  pipe_pump(1);
  return 0;
}
