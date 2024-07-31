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
#include <string.h> /* strlen */

static int completed_pingers = 0;

#if defined(__CYGWIN__) || defined(__MSYS__) || defined(__MVS__)
#define NUM_PINGS 100 /* fewer pings to avoid timeout */
#else
#define NUM_PINGS 1000
#endif

static char PING[] = "PING\n";
static char PONG[] = "PONG\n";
static int pinger_on_connect_count;


typedef struct {
  int vectored_writes;
  unsigned pongs;
  unsigned state;
  union {
    uv_tcp_t tcp;
    uv_pipe_t pipe;
  } stream;
  uv_connect_t connect_req;
  char* pong;
} pinger_t;


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  buf->len = size;
}


static void ponger_on_close(uv_handle_t* handle) {
  if (handle->data)
    free(handle->data);
  else
    free(handle);
}


static void pinger_on_close(uv_handle_t* handle) {
  pinger_t* pinger = (pinger_t*) handle->data;

  ASSERT_EQ(NUM_PINGS, pinger->pongs);

  if (handle == (uv_handle_t*) &pinger->stream.tcp) {
    free(pinger); /* also frees handle */
  } else {
    uv_close((uv_handle_t*) &pinger->stream.tcp, ponger_on_close);
    free(handle);
  }

  completed_pingers++;
}


static void pinger_after_write(uv_write_t* req, int status) {
  ASSERT_OK(status);
  free(req->data);
  free(req);
}


static void pinger_write_ping(pinger_t* pinger) {
  uv_stream_t* stream;
  uv_write_t* req;
  uv_buf_t bufs[sizeof PING - 1];
  int i, nbufs;

  stream = (uv_stream_t*) &pinger->stream.tcp;

  if (!pinger->vectored_writes) {
    /* Write a single buffer. */
    nbufs = 1;
    bufs[0] = uv_buf_init(PING, sizeof PING - 1);
  } else {
    /* Write multiple buffers, each with one byte in them. */
    nbufs = sizeof PING - 1;
    for (i = 0; i < nbufs; i++) {
      bufs[i] = uv_buf_init(&PING[i], 1);
    }
  }

  req = malloc(sizeof(*req));
  ASSERT_NOT_NULL(req);
  req->data = NULL;
  ASSERT_OK(uv_write(req, stream, bufs, nbufs, pinger_after_write));

  puts("PING");
}


static void pinger_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  ssize_t i;
  pinger_t* pinger;

  pinger = (pinger_t*) stream->data;

  if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);

    puts("got EOF");
    free(buf->base);

    uv_close((uv_handle_t*) stream, pinger_on_close);

    return;
  }

  /* Now we count the pongs */
  for (i = 0; i < nread; i++) {
    ASSERT_EQ(buf->base[i], pinger->pong[pinger->state]);
    pinger->state = (pinger->state + 1) % strlen(pinger->pong);

    if (pinger->state != 0)
      continue;

    printf("PONG %d\n", pinger->pongs);
    pinger->pongs++;

    if (pinger->pongs < NUM_PINGS) {
      pinger_write_ping(pinger);
    } else {
      uv_close((uv_handle_t*) stream, pinger_on_close);
      break;
    }
  }

  free(buf->base);
}


static void ponger_read_cb(uv_stream_t* stream,
                           ssize_t nread,
                           const uv_buf_t* buf) {
  uv_buf_t writebuf;
  uv_write_t* req;
  int i;

  if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);

    puts("got EOF");
    free(buf->base);

    uv_close((uv_handle_t*) stream, ponger_on_close);

    return;
  }

  /* Echo back */
  for (i = 0; i < nread; i++) {
    if (buf->base[i] == 'I')
      buf->base[i] = 'O';
  }

  writebuf = uv_buf_init(buf->base, nread);
  req = malloc(sizeof(*req));
  ASSERT_NOT_NULL(req);
  req->data = buf->base;
  ASSERT_OK(uv_write(req, stream, &writebuf, 1, pinger_after_write));
}


static void pinger_on_connect(uv_connect_t* req, int status) {
  pinger_t* pinger = (pinger_t*) req->handle->data;

  pinger_on_connect_count++;

  ASSERT_OK(status);

  ASSERT_EQ(1, uv_is_readable(req->handle));
  ASSERT_EQ(1, uv_is_writable(req->handle));
  ASSERT_OK(uv_is_closing((uv_handle_t *) req->handle));

  pinger_write_ping(pinger);

  ASSERT_OK(uv_read_start((uv_stream_t*) req->handle,
                          alloc_cb,
                          pinger_read_cb));
}


/* same ping-pong test, but using IPv6 connection */
static void tcp_pinger_v6_new(int vectored_writes) {
  int r;
  struct sockaddr_in6 server_addr;
  pinger_t* pinger;


  ASSERT_OK(uv_ip6_addr("::1", TEST_PORT, &server_addr));
  pinger = malloc(sizeof(*pinger));
  ASSERT_NOT_NULL(pinger);
  pinger->vectored_writes = vectored_writes;
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->pong = PING;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(uv_default_loop(), &pinger->stream.tcp);
  pinger->stream.tcp.data = pinger;
  ASSERT_OK(r);

  /* We are never doing multiple reads/connects at a time anyway, so these
   * handles can be pre-initialized. */
  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->stream.tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_on_connect);
  ASSERT_OK(r);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT_OK(pinger_on_connect_count);
}


static void tcp_pinger_new(int vectored_writes) {
  int r;
  struct sockaddr_in server_addr;
  pinger_t* pinger;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  pinger = malloc(sizeof(*pinger));
  ASSERT_NOT_NULL(pinger);
  pinger->vectored_writes = vectored_writes;
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->pong = PING;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(uv_default_loop(), &pinger->stream.tcp);
  pinger->stream.tcp.data = pinger;
  ASSERT_OK(r);

  /* We are never doing multiple reads/connects at a time anyway, so these
   * handles can be pre-initialized. */
  r = uv_tcp_connect(&pinger->connect_req,
                     &pinger->stream.tcp,
                     (const struct sockaddr*) &server_addr,
                     pinger_on_connect);
  ASSERT_OK(r);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT_OK(pinger_on_connect_count);
}


static void pipe_pinger_new(int vectored_writes) {
  int r;
  pinger_t* pinger;

  pinger = malloc(sizeof(*pinger));
  ASSERT_NOT_NULL(pinger);
  pinger->vectored_writes = vectored_writes;
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->pong = PING;

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_pipe_init(uv_default_loop(), &pinger->stream.pipe, 0);
  pinger->stream.pipe.data = pinger;
  ASSERT_OK(r);

  /* We are never doing multiple reads/connects at a time anyway, so these
   * handles can be pre-initialized. */
  uv_pipe_connect(&pinger->connect_req, &pinger->stream.pipe, TEST_PIPENAME,
      pinger_on_connect);

  /* Synchronous connect callbacks are not allowed. */
  ASSERT_OK(pinger_on_connect_count);
}


static void socketpair_pinger_new(int vectored_writes) {
  pinger_t* pinger;
  uv_os_sock_t fds[2];
  uv_tcp_t* ponger;

  pinger = malloc(sizeof(*pinger));
  ASSERT_NOT_NULL(pinger);
  pinger->vectored_writes = vectored_writes;
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->pong = PONG;

  /* Try to make a socketpair and do NUM_PINGS ping-pongs. */
  (void)uv_default_loop(); /* ensure WSAStartup has been performed */
  ASSERT_OK(uv_socketpair(SOCK_STREAM, 0, fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE));
#ifndef _WIN32
  /* On Windows, this is actually a UV_TCP, but libuv doesn't detect that. */
  ASSERT_EQ(uv_guess_handle((uv_file) fds[0]), UV_NAMED_PIPE);
  ASSERT_EQ(uv_guess_handle((uv_file) fds[1]), UV_NAMED_PIPE);
#endif

  ASSERT_OK(uv_tcp_init(uv_default_loop(), &pinger->stream.tcp));
  pinger->stream.pipe.data = pinger;
  ASSERT_OK(uv_tcp_open(&pinger->stream.tcp, fds[1]));

  ponger = malloc(sizeof(*ponger));
  ASSERT_NOT_NULL(ponger);
  ponger->data = NULL;
  ASSERT_OK(uv_tcp_init(uv_default_loop(), ponger));
  ASSERT_OK(uv_tcp_open(ponger, fds[0]));

  pinger_write_ping(pinger);

  ASSERT_OK(uv_read_start((uv_stream_t*) &pinger->stream.tcp,
                          alloc_cb,
                          pinger_read_cb));
  ASSERT_OK(uv_read_start((uv_stream_t*) ponger,
                          alloc_cb,
                          ponger_read_cb));
}


static void pipe2_pinger_new(int vectored_writes) {
  uv_file fds[2];
  pinger_t* pinger;
  uv_pipe_t* ponger;

  /* Try to make a pipe and do NUM_PINGS pings. */
  ASSERT_OK(uv_pipe(fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE));
  ASSERT_EQ(uv_guess_handle(fds[0]), UV_NAMED_PIPE);
  ASSERT_EQ(uv_guess_handle(fds[1]), UV_NAMED_PIPE);

  ponger = malloc(sizeof(*ponger));
  ASSERT_NOT_NULL(ponger);
  ASSERT_OK(uv_pipe_init(uv_default_loop(), ponger, 0));
  ASSERT_OK(uv_pipe_open(ponger, fds[0]));

  pinger = malloc(sizeof(*pinger));
  ASSERT_NOT_NULL(pinger);
  pinger->vectored_writes = vectored_writes;
  pinger->state = 0;
  pinger->pongs = 0;
  pinger->pong = PING;
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pinger->stream.pipe, 0));
  ASSERT_OK(uv_pipe_open(&pinger->stream.pipe, fds[1]));
  pinger->stream.pipe.data = pinger; /* record for close_cb */
  ponger->data = pinger; /* record for read_cb */

  pinger_write_ping(pinger);

  ASSERT_OK(uv_read_start((uv_stream_t*) ponger, alloc_cb, pinger_read_cb));
}

static int run_ping_pong_test(void) {
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, completed_pingers);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_ping_pong) {
  tcp_pinger_new(0);
  run_ping_pong_test();

  completed_pingers = 0;
  socketpair_pinger_new(0);
  return run_ping_pong_test();
}


TEST_IMPL(tcp_ping_pong_vec) {
  tcp_pinger_new(1);
  run_ping_pong_test();

  completed_pingers = 0;
  socketpair_pinger_new(1);
  return run_ping_pong_test();
}


TEST_IMPL(tcp6_ping_pong) {
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");
  tcp_pinger_v6_new(0);
  return run_ping_pong_test();
}


TEST_IMPL(tcp6_ping_pong_vec) {
  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");
  tcp_pinger_v6_new(1);
  return run_ping_pong_test();
}


TEST_IMPL(pipe_ping_pong) {
  pipe_pinger_new(0);
  run_ping_pong_test();

  completed_pingers = 0;
  pipe2_pinger_new(0);
  return run_ping_pong_test();
}


TEST_IMPL(pipe_ping_pong_vec) {
  pipe_pinger_new(1);
  run_ping_pong_test();

  completed_pingers = 0;
  pipe2_pinger_new(1);
  return run_ping_pong_test();
}
