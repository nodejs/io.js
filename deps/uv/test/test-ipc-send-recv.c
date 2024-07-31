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

/* See test-ipc.c */
void spawn_helper(uv_pipe_t* channel,
                  uv_process_t* process,
                  const char* helper);

void ipc_send_recv_helper_threadproc(void* arg);

union handles {
  uv_handle_t handle;
  uv_stream_t stream;
  uv_pipe_t pipe;
  uv_tcp_t tcp;
  uv_tty_t tty;
};

struct test_ctx {
  uv_pipe_t channel;
  uv_connect_t connect_req;
  uv_write_t write_req;
  uv_write_t write_req2;
  uv_handle_type expected_type;
  union handles send;
  union handles send2;
  union handles recv;
  union handles recv2;
};

struct echo_ctx {
  uv_pipe_t listen;
  uv_pipe_t channel;
  uv_write_t write_req;
  uv_write_t write_req2;
  uv_handle_type expected_type;
  union handles recv;
  union handles recv2;
};

static struct test_ctx ctx;
static struct echo_ctx ctx2;

/* Used in write2_cb to decide if we need to cleanup or not */
static int is_child_process;
static int is_in_process;
static int read_cb_count;
static int recv_cb_count;
static int write2_cb_called;


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  /* We're not actually reading anything so a small buffer is okay
   * but it needs to be heap-allocated to appease TSan.
   */
  buf->len = 8;
  buf->base = malloc(buf->len);
  ASSERT_NOT_NULL(buf->base);
}


static void recv_cb(uv_stream_t* handle,
                    ssize_t nread,
                    const uv_buf_t* buf) {
  uv_handle_type pending;
  uv_pipe_t* pipe;
  int r;
  union handles* recv;

  free(buf->base);

  pipe = (uv_pipe_t*) handle;
  ASSERT_PTR_EQ(pipe, &ctx.channel);

  do {
    if (++recv_cb_count == 1) {
      recv = &ctx.recv;
    } else {
      recv = &ctx.recv2;
    }

    /* Depending on the OS, the final recv_cb can be called after
     * the child process has terminated which can result in nread
     * being UV_EOF instead of the number of bytes read.  Since
     * the other end of the pipe has closed this UV_EOF is an
     * acceptable value. */
    if (nread == UV_EOF) {
      /* UV_EOF is only acceptable for the final recv_cb call */
      ASSERT_EQ(2, recv_cb_count);
    } else {
      ASSERT_GE(nread, 0);
      ASSERT_GT(uv_pipe_pending_count(pipe), 0);

      pending = uv_pipe_pending_type(pipe);
      ASSERT_EQ(pending, ctx.expected_type);

      if (pending == UV_NAMED_PIPE)
        r = uv_pipe_init(ctx.channel.loop, &recv->pipe, 0);
      else if (pending == UV_TCP)
        r = uv_tcp_init(ctx.channel.loop, &recv->tcp);
      else
        abort();
      ASSERT_OK(r);

      r = uv_accept(handle, &recv->stream);
      ASSERT_OK(r);
    }
  } while (uv_pipe_pending_count(pipe) > 0);

  /* Close after two writes received */
  if (recv_cb_count == 2) {
    uv_close((uv_handle_t*)&ctx.channel, NULL);
  }
}

static void connect_cb(uv_connect_t* req, int status) {
  int r;
  uv_buf_t buf;

  ASSERT_PTR_EQ(req, &ctx.connect_req);
  ASSERT_OK(status);

  buf = uv_buf_init(".", 1);
  r = uv_write2(&ctx.write_req,
                (uv_stream_t*)&ctx.channel,
                &buf, 1,
                &ctx.send.stream,
                NULL);
  ASSERT_OK(r);

  /* Perform two writes to the same pipe to make sure that on Windows we are
   * not running into issue 505:
   *   https://github.com/libuv/libuv/issues/505 */
  buf = uv_buf_init(".", 1);
  r = uv_write2(&ctx.write_req2,
                (uv_stream_t*)&ctx.channel,
                &buf, 1,
                &ctx.send2.stream,
                NULL);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*)&ctx.channel, alloc_cb, recv_cb);
  ASSERT_OK(r);
}

static int run_test(int inprocess) {
  uv_process_t process;
  uv_thread_t tid;
  int r;

  if (inprocess) {
    r = uv_thread_create(&tid, ipc_send_recv_helper_threadproc, (void *) 42);
    ASSERT_OK(r);

    uv_sleep(1000);

    r = uv_pipe_init(uv_default_loop(), &ctx.channel, 1);
    ASSERT_OK(r);

    uv_pipe_connect(&ctx.connect_req, &ctx.channel, TEST_PIPENAME_3, connect_cb);
  } else {
    spawn_helper(&ctx.channel, &process, "ipc_send_recv_helper");

    connect_cb(&ctx.connect_req, 0);
  }

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(2, recv_cb_count);

  if (inprocess) {
    r = uv_thread_join(&tid);
    ASSERT_OK(r);
  }

  return 0;
}

static int run_ipc_send_recv_pipe(int inprocess) {
  int r;

  ctx.expected_type = UV_NAMED_PIPE;

  r = uv_pipe_init(uv_default_loop(), &ctx.send.pipe, 1);
  ASSERT_OK(r);

  r = uv_pipe_bind(&ctx.send.pipe, TEST_PIPENAME);
  ASSERT_OK(r);

  r = uv_pipe_init(uv_default_loop(), &ctx.send2.pipe, 1);
  ASSERT_OK(r);

  r = uv_pipe_bind(&ctx.send2.pipe, TEST_PIPENAME_2);
  ASSERT_OK(r);

  r = run_test(inprocess);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(ipc_send_recv_pipe) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  return run_ipc_send_recv_pipe(0);
}

TEST_IMPL(ipc_send_recv_pipe_inprocess) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  return run_ipc_send_recv_pipe(1);
}

static int run_ipc_send_recv_tcp(int inprocess) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ctx.expected_type = UV_TCP;

  r = uv_tcp_init(uv_default_loop(), &ctx.send.tcp);
  ASSERT_OK(r);

  r = uv_tcp_init(uv_default_loop(), &ctx.send2.tcp);
  ASSERT_OK(r);

  r = uv_tcp_bind(&ctx.send.tcp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_tcp_bind(&ctx.send2.tcp, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = run_test(inprocess);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(ipc_send_recv_tcp) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  return run_ipc_send_recv_tcp(0);
}

TEST_IMPL(ipc_send_recv_tcp_inprocess) {
#if defined(NO_SEND_HANDLE_ON_PIPE)
  RETURN_SKIP(NO_SEND_HANDLE_ON_PIPE);
#endif
  return run_ipc_send_recv_tcp(1);
}


/* Everything here runs in a child process or second thread. */

static void write2_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);

  /* After two successful writes in the child process, allow the child
   * process to be closed. */
  if (++write2_cb_called == 2 && (is_child_process || is_in_process)) {
    uv_close(&ctx2.recv.handle, NULL);
    uv_close(&ctx2.recv2.handle, NULL);
    uv_close((uv_handle_t*)&ctx2.channel, NULL);
    uv_close((uv_handle_t*)&ctx2.listen, NULL);
  }
}

static void read_cb(uv_stream_t* handle,
                    ssize_t nread,
                    const uv_buf_t* rdbuf) {
  uv_buf_t wrbuf;
  uv_pipe_t* pipe;
  uv_handle_type pending;
  int r;
  union handles* recv;
  uv_write_t* write_req;

  free(rdbuf->base);

  if (nread == UV_EOF || nread == UV_ECONNABORTED) {
    return;
  }

  ASSERT_GE(nread, 0);

  pipe = (uv_pipe_t*) handle;
  ASSERT_PTR_EQ(pipe, &ctx2.channel);

  while (uv_pipe_pending_count(pipe) > 0) {
    if (++read_cb_count == 2) {
      recv = &ctx2.recv;
      write_req = &ctx2.write_req;
    } else {
      recv = &ctx2.recv2;
      write_req = &ctx2.write_req2;
    }

    pending = uv_pipe_pending_type(pipe);
    ASSERT(pending == UV_NAMED_PIPE || pending == UV_TCP);

    if (pending == UV_NAMED_PIPE)
      r = uv_pipe_init(ctx2.channel.loop, &recv->pipe, 0);
    else if (pending == UV_TCP)
      r = uv_tcp_init(ctx2.channel.loop, &recv->tcp);
    else
      abort();
    ASSERT_OK(r);

    r = uv_accept(handle, &recv->stream);
    ASSERT_OK(r);

    wrbuf = uv_buf_init(".", 1);
    r = uv_write2(write_req,
                  (uv_stream_t*)&ctx2.channel,
                  &wrbuf,
                  1,
                  &recv->stream,
                  write2_cb);
    ASSERT_OK(r);
  }
}

static void send_recv_start(void) {
  int r;
  ASSERT_EQ(1, uv_is_readable((uv_stream_t*)&ctx2.channel));
  ASSERT_EQ(1, uv_is_writable((uv_stream_t*)&ctx2.channel));
  ASSERT_OK(uv_is_closing((uv_handle_t*)&ctx2.channel));

  r = uv_read_start((uv_stream_t*)&ctx2.channel, alloc_cb, read_cb);
  ASSERT_OK(r);
}

static void listen_cb(uv_stream_t* handle, int status) {
  int r;
  ASSERT_PTR_EQ(handle, (uv_stream_t*)&ctx2.listen);
  ASSERT_OK(status);

  r = uv_accept((uv_stream_t*)&ctx2.listen, (uv_stream_t*)&ctx2.channel);
  ASSERT_OK(r);

  send_recv_start();
}

int run_ipc_send_recv_helper(uv_loop_t* loop, int inprocess) {
  int r;

  is_in_process = inprocess;

  memset(&ctx2, 0, sizeof(ctx2));

  r = uv_pipe_init(loop, &ctx2.listen, 0);
  ASSERT_OK(r);

  r = uv_pipe_init(loop, &ctx2.channel, 1);
  ASSERT_OK(r);

  if (inprocess) {
    r = uv_pipe_bind(&ctx2.listen, TEST_PIPENAME_3);
    ASSERT_OK(r);

    r = uv_listen((uv_stream_t*)&ctx2.listen, SOMAXCONN, listen_cb);
    ASSERT_OK(r);
  } else {
    r = uv_pipe_open(&ctx2.channel, 0);
    ASSERT_OK(r);

    send_recv_start();
  }

  notify_parent_process();
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  return 0;
}

/* stdin is a duplex channel over which a handle is sent.
 * We receive it and send it back where it came from.
 */
int ipc_send_recv_helper(void) {
  int r;

  r = run_ipc_send_recv_helper(uv_default_loop(), 0);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

void ipc_send_recv_helper_threadproc(void* arg) {
  int r;
  uv_loop_t loop;

  r = uv_loop_init(&loop);
  ASSERT_OK(r);

  r = run_ipc_send_recv_helper(&loop, 1);
  ASSERT_OK(r);

  r = uv_loop_close(&loop);
  ASSERT_OK(r);
}
