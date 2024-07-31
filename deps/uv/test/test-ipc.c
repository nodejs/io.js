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
#include <string.h>

static uv_pipe_t channel;
static uv_tcp_t tcp_server;
static uv_tcp_t tcp_server2;
static uv_tcp_t tcp_connection;

static int exit_cb_called;
static int read_cb_called;
static int tcp_write_cb_called;
static int tcp_read_cb_called;
static int on_pipe_read_called;
static int local_conn_accepted;
static int remote_conn_accepted;
static int tcp_server_listening;
static uv_write_t write_req;
static uv_write_t write_req2;
static uv_write_t conn_notify_req;
static int close_cb_called;
static int connection_accepted;
static int tcp_conn_read_cb_called;
static int tcp_conn_write_cb_called;
static int send_zero_write;

typedef struct {
  uv_connect_t conn_req;
  uv_write_t tcp_write_req;
  uv_tcp_t conn;
} tcp_conn;

#define CONN_COUNT 100
#define BACKLOG 128


static void close_server_conn_cb(uv_handle_t* handle) {
  free(handle);
}


static void on_connection(uv_stream_t* server, int status) {
  uv_tcp_t* conn;
  int r;

  if (!local_conn_accepted) {
    /* Accept the connection and close it.  Also and close the server. */
    ASSERT_OK(status);
    ASSERT_PTR_EQ(&tcp_server, server);

    conn = malloc(sizeof(*conn));
    ASSERT_NOT_NULL(conn);
    r = uv_tcp_init(server->loop, conn);
    ASSERT_OK(r);

    r = uv_accept(server, (uv_stream_t*)conn);
    ASSERT_OK(r);

    uv_close((uv_handle_t*)conn, close_server_conn_cb);
    uv_close((uv_handle_t*)server, NULL);
    local_conn_accepted = 1;
  }
}


static void exit_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  printf("exit_cb\n");
  exit_cb_called++;
  ASSERT_OK(exit_status);
  ASSERT_OK(term_signal);
  uv_close((uv_handle_t*)process, NULL);
}


static void on_alloc(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}


static void close_client_conn_cb(uv_handle_t* handle) {
  tcp_conn* p = (tcp_conn*)handle->data;
  free(p);
}


static void connect_cb(uv_connect_t* req, int status) {
  uv_close((uv_handle_t*)req->handle, close_client_conn_cb);
}


static void make_many_connections(void) {
  tcp_conn* conn;
  struct sockaddr_in addr;
  int r, i;

  for (i = 0; i < CONN_COUNT; i++) {
    conn = malloc(sizeof(*conn));
    ASSERT_NOT_NULL(conn);

    r = uv_tcp_init(uv_default_loop(), &conn->conn);
    ASSERT_OK(r);
    ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

    r = uv_tcp_connect(&conn->conn_req,
                       (uv_tcp_t*) &conn->conn,
                       (const struct sockaddr*) &addr,
                       connect_cb);
    ASSERT_OK(r);

    conn->conn.data = conn;
  }
}


static void on_read(uv_stream_t* handle,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  int r;
  uv_pipe_t* pipe;
  uv_handle_type pending;
  uv_buf_t outbuf;

  pipe = (uv_pipe_t*) handle;

  if (nread == 0) {
    /* Everything OK, but nothing read. */
    free(buf->base);
    return;
  }

  if (nread < 0) {
    if (nread == UV_EOF) {
      free(buf->base);
      return;
    }

    printf("error recving on channel: %s\n", uv_strerror(nread));
    abort();
  }

  fprintf(stderr, "got %d bytes\n", (int)nread);

  pending = uv_pipe_pending_type(pipe);
  if (!tcp_server_listening) {
    ASSERT_EQ(1, uv_pipe_pending_count(pipe));
    ASSERT_GT(nread, 0);
    ASSERT_NOT_NULL(buf->base);
    ASSERT_NE(pending, UV_UNKNOWN_HANDLE);
    read_cb_called++;

    /* Accept the pending TCP server, and start listening on it. */
    ASSERT_EQ(pending, UV_TCP);
    r = uv_tcp_init(uv_default_loop(), &tcp_server);
    ASSERT_OK(r);

    r = uv_accept((uv_stream_t*)pipe, (uv_stream_t*)&tcp_server);
    ASSERT_OK(r);

    r = uv_listen((uv_stream_t*)&tcp_server, BACKLOG, on_connection);
    ASSERT_OK(r);

    tcp_server_listening = 1;

    /* Make sure that the expected data is correctly multiplexed. */
    ASSERT_MEM_EQ("hello\n", buf->base, nread);

    outbuf = uv_buf_init("foobar\n", 7);
    r = uv_write(&write_req, (uv_stream_t*)pipe, &outbuf, 1, NULL);
    ASSERT_OK(r);

    /* Create a bunch of connections to get both servers to accept. */
    make_many_connections();
  } else if (memcmp("accepted_connection\n", buf->base, nread) == 0) {
    /* Remote server has accepted a connection.  Close the channel. */
    ASSERT_OK(uv_pipe_pending_count(pipe));
    ASSERT_EQ(pending, UV_UNKNOWN_HANDLE);
    remote_conn_accepted = 1;
    uv_close((uv_handle_t*)&channel, NULL);
  }

  free(buf->base);
}

#ifdef _WIN32
static void on_read_listen_after_bound_twice(uv_stream_t* handle,
                                             ssize_t nread,
                                             const uv_buf_t* buf) {
  int r;
  uv_pipe_t* pipe;
  uv_handle_type pending;

  pipe = (uv_pipe_t*) handle;

  if (nread == 0) {
    /* Everything OK, but nothing read. */
    free(buf->base);
    return;
  }

  if (nread < 0) {
    if (nread == UV_EOF) {
      free(buf->base);
      return;
    }

    printf("error recving on channel: %s\n", uv_strerror(nread));
    abort();
  }

  fprintf(stderr, "got %d bytes\n", (int)nread);

  ASSERT_GT(uv_pipe_pending_count(pipe), 0);
  pending = uv_pipe_pending_type(pipe);
  ASSERT_GT(nread, 0);
  ASSERT_NOT_NULL(buf->base);
  ASSERT_NE(pending, UV_UNKNOWN_HANDLE);
  read_cb_called++;

  if (read_cb_called == 1) {
    /* Accept the first TCP server, and start listening on it. */
    ASSERT_EQ(pending, UV_TCP);
    r = uv_tcp_init(uv_default_loop(), &tcp_server);
    ASSERT_OK(r);

    r = uv_accept((uv_stream_t*)pipe, (uv_stream_t*)&tcp_server);
    ASSERT_OK(r);

    r = uv_listen((uv_stream_t*)&tcp_server, BACKLOG, on_connection);
    ASSERT_OK(r);
  } else if (read_cb_called == 2) {
    /* Accept the second TCP server, and start listening on it. */
    ASSERT_EQ(pending, UV_TCP);
    r = uv_tcp_init(uv_default_loop(), &tcp_server2);
    ASSERT_OK(r);

    r = uv_accept((uv_stream_t*)pipe, (uv_stream_t*)&tcp_server2);
    ASSERT_OK(r);

    r = uv_listen((uv_stream_t*)&tcp_server2, BACKLOG, on_connection);
    ASSERT_EQ(r, UV_EADDRINUSE);

    uv_close((uv_handle_t*)&tcp_server, NULL);
    uv_close((uv_handle_t*)&tcp_server2, NULL);
    ASSERT_OK(uv_pipe_pending_count(pipe));
    uv_close((uv_handle_t*)&channel, NULL);
  }

  free(buf->base);
}
#endif

void spawn_helper(uv_pipe_t* channel,
                  uv_process_t* process,
                  const char* helper) {
  uv_process_options_t options;
  size_t exepath_size;
  char exepath[1024];
  char* args[3];
  int r;
  uv_stdio_container_t stdio[3];

  r = uv_pipe_init(uv_default_loop(), channel, 1);
  ASSERT_OK(r);
  ASSERT_NE(0, channel->ipc);

  exepath_size = sizeof(exepath);
  r = uv_exepath(exepath, &exepath_size);
  ASSERT_OK(r);

  exepath[exepath_size] = '\0';
  args[0] = exepath;
  args[1] = (char*)helper;
  args[2] = NULL;

  memset(&options, 0, sizeof(options));
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;
  options.stdio = stdio;
  options.stdio_count = ARRAY_SIZE(stdio);

  stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE;
  stdio[0].data.stream = (uv_stream_t*) channel;
  stdio[1].flags = UV_INHERIT_FD;
  stdio[1].data.fd = 1;
  stdio[2].flags = UV_INHERIT_FD;
  stdio[2].data.fd = 2;

  r = uv_spawn(uv_default_loop(), process, &options);
  ASSERT_OK(r);
}


static void on_tcp_write(uv_write_t* req, int status) {
  ASSERT_OK(status);
  ASSERT_PTR_EQ(req->handle, &tcp_connection);
  tcp_write_cb_called++;
}


static void on_read_alloc(uv_handle_t* handle,
                          size_t suggested_size,
                          uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}


static void on_tcp_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  ASSERT_GT(nread, 0);
  ASSERT_MEM_EQ("hello again\n", buf->base, nread);
  ASSERT_PTR_EQ(tcp, &tcp_connection);
  free(buf->base);

  tcp_read_cb_called++;

  uv_close((uv_handle_t*)tcp, NULL);
  uv_close((uv_handle_t*)&channel, NULL);
}


static void on_read_connection(uv_stream_t* handle,
                               ssize_t nread,
                               const uv_buf_t* buf) {
  int r;
  uv_buf_t outbuf;
  uv_pipe_t* pipe;
  uv_handle_type pending;

  pipe = (uv_pipe_t*) handle;
  if (nread == 0) {
    /* Everything OK, but nothing read. */
    free(buf->base);
    return;
  }

  if (nread < 0) {
    if (nread == UV_EOF) {
      free(buf->base);
      return;
    }

    printf("error recving on channel: %s\n", uv_strerror(nread));
    abort();
  }

  fprintf(stderr, "got %d bytes\n", (int)nread);

  ASSERT_EQ(1, uv_pipe_pending_count(pipe));
  pending = uv_pipe_pending_type(pipe);

  ASSERT_GT(nread, 0);
  ASSERT_NOT_NULL(buf->base);
  ASSERT_NE(pending, UV_UNKNOWN_HANDLE);
  read_cb_called++;

  /* Accept the pending TCP connection */
  ASSERT_EQ(pending, UV_TCP);
  r = uv_tcp_init(uv_default_loop(), &tcp_connection);
  ASSERT_OK(r);

  r = uv_accept(handle, (uv_stream_t*)&tcp_connection);
  ASSERT_OK(r);

  /* Make sure that the expected data is correctly multiplexed. */
  ASSERT_MEM_EQ("hello\n", buf->base, nread);

  /* Write/read to/from the connection */
  outbuf = uv_buf_init("world\n", 6);
  r = uv_write(&write_req, (uv_stream_t*)&tcp_connection, &outbuf, 1,
    on_tcp_write);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*)&tcp_connection, on_read_alloc, on_tcp_read);
  ASSERT_OK(r);

  free(buf->base);
}


static void on_read_send_zero(uv_stream_t* handle,
                              ssize_t nread,
                              const uv_buf_t* buf) {
  ASSERT(nread == 0 || nread == UV_EOF);
  free(buf->base);
}


static int run_ipc_test(const char* helper, uv_read_cb read_cb) {
  uv_process_t process;
  int r;

  spawn_helper(&channel, &process, helper);
  uv_read_start((uv_stream_t*)&channel, on_alloc, read_cb);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(ipc_listen_before_write) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  int r = run_ipc_test("ipc_helper_listen_before_write", on_read);
  ASSERT_EQ(1, local_conn_accepted);
  ASSERT_EQ(1, remote_conn_accepted);
  ASSERT_EQ(1, read_cb_called);
  ASSERT_EQ(1, exit_cb_called);
  return r;
}


TEST_IMPL(ipc_listen_after_write) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  int r = run_ipc_test("ipc_helper_listen_after_write", on_read);
  ASSERT_EQ(1, local_conn_accepted);
  ASSERT_EQ(1, remote_conn_accepted);
  ASSERT_EQ(1, read_cb_called);
  ASSERT_EQ(1, exit_cb_called);
  return r;
}


TEST_IMPL(ipc_tcp_connection) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  int r = run_ipc_test("ipc_helper_tcp_connection", on_read_connection);
  ASSERT_EQ(1, read_cb_called);
  ASSERT_EQ(1, tcp_write_cb_called);
  ASSERT_EQ(1, tcp_read_cb_called);
  ASSERT_EQ(1, exit_cb_called);
  return r;
}


#ifdef _WIN32
TEST_IMPL(listen_with_simultaneous_accepts) {
  uv_tcp_t server;
  int r;
  struct sockaddr_in addr;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_tcp_simultaneous_accepts(&server, 1);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server, SOMAXCONN, NULL);
  ASSERT_OK(r);
  ASSERT_EQ(32, server.reqs_pending);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(listen_no_simultaneous_accepts) {
  uv_tcp_t server;
  int r;
  struct sockaddr_in addr;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);

  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_tcp_simultaneous_accepts(&server, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server, SOMAXCONN, NULL);
  ASSERT_OK(r);
  ASSERT_EQ(1, server.reqs_pending);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(ipc_listen_after_bind_twice) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  int r = run_ipc_test("ipc_helper_bind_twice", on_read_listen_after_bound_twice);
  ASSERT_EQ(2, read_cb_called);
  ASSERT_EQ(1, exit_cb_called);
  return r;
}
#endif

TEST_IMPL(ipc_send_zero) {
  int r;
  r = run_ipc_test("ipc_helper_send_zero", on_read_send_zero);
  ASSERT_OK(r);
  return 0;
}


/* Everything here runs in a child process. */

static tcp_conn conn;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void conn_notify_write_cb(uv_write_t* req, int status) {
  uv_close((uv_handle_t*)&tcp_server, close_cb);
  uv_close((uv_handle_t*)&channel, close_cb);
}


static void tcp_connection_write_cb(uv_write_t* req, int status) {
  ASSERT_PTR_EQ(&conn.conn, req->handle);
  uv_close((uv_handle_t*)req->handle, close_cb);
  uv_close((uv_handle_t*)&channel, close_cb);
  uv_close((uv_handle_t*)&tcp_server, close_cb);
  tcp_conn_write_cb_called++;
}


static void send_zero_write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  send_zero_write++;
}

static void on_tcp_child_process_read(uv_stream_t* tcp,
                                      ssize_t nread,
                                      const uv_buf_t* buf) {
  uv_buf_t outbuf;
  int r;

  if (nread < 0) {
    if (nread == UV_EOF) {
      free(buf->base);
      return;
    }

    printf("error recving on tcp connection: %s\n", uv_strerror(nread));
    abort();
  }

  ASSERT_GT(nread, 0);
  ASSERT_MEM_EQ("world\n", buf->base, nread);
  on_pipe_read_called++;
  free(buf->base);

  /* Write to the socket */
  outbuf = uv_buf_init("hello again\n", 12);
  r = uv_write(&conn.tcp_write_req, tcp, &outbuf, 1, tcp_connection_write_cb);
  ASSERT_OK(r);

  tcp_conn_read_cb_called++;
}


static void connect_child_process_cb(uv_connect_t* req, int status) {
  int r;

  ASSERT_OK(status);
  r = uv_read_start(req->handle, on_read_alloc, on_tcp_child_process_read);
  ASSERT_OK(r);
}


static void ipc_on_connection(uv_stream_t* server, int status) {
  int r;
  uv_buf_t buf;

  if (!connection_accepted) {
    /*
     * Accept the connection and close it.  Also let the other
     * side know.
     */
    ASSERT_OK(status);
    ASSERT_PTR_EQ(&tcp_server, server);

    r = uv_tcp_init(server->loop, &conn.conn);
    ASSERT_OK(r);

    r = uv_accept(server, (uv_stream_t*)&conn.conn);
    ASSERT_OK(r);

    uv_close((uv_handle_t*)&conn.conn, close_cb);

    buf = uv_buf_init("accepted_connection\n", 20);
    r = uv_write2(&conn_notify_req, (uv_stream_t*)&channel, &buf, 1,
      NULL, conn_notify_write_cb);
    ASSERT_OK(r);

    connection_accepted = 1;
  }
}


static void close_and_free_cb(uv_handle_t* handle) {
  close_cb_called++;
  free(handle);
}

static void ipc_on_connection_tcp_conn(uv_stream_t* server, int status) {
  int r;
  uv_buf_t buf;
  uv_tcp_t* conn;

  ASSERT_OK(status);
  ASSERT_PTR_EQ(&tcp_server, server);

  conn = malloc(sizeof(*conn));
  ASSERT_NOT_NULL(conn);

  r = uv_tcp_init(server->loop, conn);
  ASSERT_OK(r);

  r = uv_accept(server, (uv_stream_t*)conn);
  ASSERT_OK(r);

  /* Send the accepted connection to the other process */
  buf = uv_buf_init("hello\n", 6);
  r = uv_write2(&conn_notify_req, (uv_stream_t*)&channel, &buf, 1,
    (uv_stream_t*)conn, NULL);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) conn,
                    on_read_alloc,
                    on_tcp_child_process_read);
  ASSERT_OK(r);

  uv_close((uv_handle_t*)conn, close_and_free_cb);
}


int ipc_helper(int listen_after_write) {
  /*
   * This is launched from test-ipc.c. stdin is a duplex channel that we
   * over which a handle will be transmitted.
   */
  struct sockaddr_in addr;
  int r;
  uv_buf_t buf;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_pipe_init(uv_default_loop(), &channel, 1);
  ASSERT_OK(r);

  uv_pipe_open(&channel, 0);

  ASSERT_EQ(1, uv_is_readable((uv_stream_t*) &channel));
  ASSERT_EQ(1, uv_is_writable((uv_stream_t*) &channel));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &channel));

  r = uv_tcp_init(uv_default_loop(), &tcp_server);
  ASSERT_OK(r);

  r = uv_tcp_bind(&tcp_server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  if (!listen_after_write) {
    r = uv_listen((uv_stream_t*)&tcp_server, BACKLOG, ipc_on_connection);
    ASSERT_OK(r);
  }

  buf = uv_buf_init("hello\n", 6);
  r = uv_write2(&write_req, (uv_stream_t*)&channel, &buf, 1,
      (uv_stream_t*)&tcp_server, NULL);
  ASSERT_OK(r);

  if (listen_after_write) {
    r = uv_listen((uv_stream_t*)&tcp_server, BACKLOG, ipc_on_connection);
    ASSERT_OK(r);
  }

  notify_parent_process();
  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, connection_accepted);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


int ipc_helper_tcp_connection(void) {
  /*
   * This is launched from test-ipc.c. stdin is a duplex channel
   * over which a handle will be transmitted.
   */

  int r;
  struct sockaddr_in addr;

  r = uv_pipe_init(uv_default_loop(), &channel, 1);
  ASSERT_OK(r);

  uv_pipe_open(&channel, 0);

  ASSERT_EQ(1, uv_is_readable((uv_stream_t*) &channel));
  ASSERT_EQ(1, uv_is_writable((uv_stream_t*) &channel));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &channel));

  r = uv_tcp_init(uv_default_loop(), &tcp_server);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_tcp_bind(&tcp_server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&tcp_server, BACKLOG, ipc_on_connection_tcp_conn);
  ASSERT_OK(r);

  /* Make a connection to the server */
  r = uv_tcp_init(uv_default_loop(), &conn.conn);
  ASSERT_OK(r);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_connect(&conn.conn_req,
                     (uv_tcp_t*) &conn.conn,
                     (const struct sockaddr*) &addr,
                     connect_child_process_cb);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, tcp_conn_read_cb_called);
  ASSERT_EQ(1, tcp_conn_write_cb_called);
  ASSERT_EQ(4, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

int ipc_helper_bind_twice(void) {
  /*
   * This is launched from test-ipc.c. stdin is a duplex channel
   * over which two handles will be transmitted.
   */
  struct sockaddr_in addr;
  int r;
  uv_buf_t buf;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  r = uv_pipe_init(uv_default_loop(), &channel, 1);
  ASSERT_OK(r);

  uv_pipe_open(&channel, 0);

  ASSERT_EQ(1, uv_is_readable((uv_stream_t*) &channel));
  ASSERT_EQ(1, uv_is_writable((uv_stream_t*) &channel));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &channel));

  buf = uv_buf_init("hello\n", 6);

  r = uv_tcp_init(uv_default_loop(), &tcp_server);
  ASSERT_OK(r);
  r = uv_tcp_init(uv_default_loop(), &tcp_server2);
  ASSERT_OK(r);

  r = uv_tcp_bind(&tcp_server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);
  r = uv_tcp_bind(&tcp_server2, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_write2(&write_req, (uv_stream_t*)&channel, &buf, 1,
                (uv_stream_t*)&tcp_server, NULL);
  ASSERT_OK(r);
  r = uv_write2(&write_req2, (uv_stream_t*)&channel, &buf, 1,
                (uv_stream_t*)&tcp_server2, NULL);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

int ipc_helper_send_zero(void) {
  int r;
  uv_buf_t zero_buf;

  zero_buf = uv_buf_init(0, 0);

  r = uv_pipe_init(uv_default_loop(), &channel, 0);
  ASSERT_OK(r);

  uv_pipe_open(&channel, 0);

  ASSERT_EQ(1, uv_is_readable((uv_stream_t*) &channel));
  ASSERT_EQ(1, uv_is_writable((uv_stream_t*) &channel));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &channel));

  r = uv_write(&write_req,
               (uv_stream_t*)&channel,
               &zero_buf,
               1,
               send_zero_write_cb);

  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, send_zero_write);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
