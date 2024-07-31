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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED "RANG TANG DING DONG I AM THE JAPANESE SANDMAN"

#define TEST_DURATION 5000 /* ms */

#define BASE_PORT 12345

struct sender_state {
  struct sockaddr_in addr;
  uv_udp_send_t send_req;
  uv_udp_t udp_handle;
};

struct receiver_state {
  struct sockaddr_in addr;
  uv_udp_t udp_handle;
};

/* not used in timed mode */
static unsigned int packet_counter = (unsigned int) 1e6;

static int n_senders_;
static int n_receivers_;
static uv_buf_t bufs[5];
static struct sender_state senders[1024];
static struct receiver_state receivers[1024];

static unsigned int send_cb_called;
static unsigned int recv_cb_called;
static unsigned int close_cb_called;
static int timed;
static int exiting;


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  static char slab[65536];
  ASSERT_LE(suggested_size, sizeof(slab));
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void send_cb(uv_udp_send_t* req, int status) {
  struct sender_state* s;

  ASSERT_NOT_NULL(req);

  if (status != 0) {
    ASSERT_EQ(status, UV_ECANCELED);
    return;
  }

  if (exiting)
    return;

  s = container_of(req, struct sender_state, send_req);
  ASSERT_PTR_EQ(req->handle, &s->udp_handle);

  if (timed)
    goto send;

  if (packet_counter == 0) {
    uv_close((uv_handle_t*)&s->udp_handle, NULL);
    return;
  }

  packet_counter--;

send:
  ASSERT_OK(uv_udp_send(&s->send_req,
                        &s->udp_handle,
                        bufs,
                        ARRAY_SIZE(bufs),
                        (const struct sockaddr*) &s->addr,
                        send_cb));
  send_cb_called++;
}


static void recv_cb(uv_udp_t* handle,
                    ssize_t nread,
                    const uv_buf_t* buf,
                    const struct sockaddr* addr,
                    unsigned flags) {
  if (nread == 0)
    return;

  if (nread < 0) {
    ASSERT_EQ(nread, UV_ECANCELED);
    return;
  }

  ASSERT_EQ(addr->sa_family, AF_INET);
  ASSERT(!memcmp(buf->base, EXPECTED, nread));

  recv_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


static void timeout_cb(uv_timer_t* timer) {
  int i;

  exiting = 1;

  for (i = 0; i < n_senders_; i++)
    uv_close((uv_handle_t*)&senders[i].udp_handle, close_cb);

  for (i = 0; i < n_receivers_; i++)
    uv_close((uv_handle_t*)&receivers[i].udp_handle, close_cb);
}


static int pummel(unsigned int n_senders,
                  unsigned int n_receivers,
                  unsigned long timeout) {
  uv_timer_t timer_handle;
  uint64_t duration;
  uv_loop_t* loop;
  unsigned int i;

  ASSERT_LE(n_senders, ARRAY_SIZE(senders));
  ASSERT_LE(n_receivers, ARRAY_SIZE(receivers));

  loop = uv_default_loop();

  n_senders_ = n_senders;
  n_receivers_ = n_receivers;

  if (timeout) {
    ASSERT_OK(uv_timer_init(loop, &timer_handle));
    ASSERT_OK(uv_timer_start(&timer_handle, timeout_cb, timeout, 0));
    /* Timer should not keep loop alive. */
    uv_unref((uv_handle_t*)&timer_handle);
    timed = 1;
  }

  for (i = 0; i < n_receivers; i++) {
    struct receiver_state* s = receivers + i;
    struct sockaddr_in addr;
    ASSERT_OK(uv_ip4_addr("0.0.0.0", BASE_PORT + i, &addr));
    ASSERT_OK(uv_udp_init(loop, &s->udp_handle));
    ASSERT_OK(uv_udp_bind(&s->udp_handle, (const struct sockaddr*) &addr, 0));
    ASSERT_OK(uv_udp_recv_start(&s->udp_handle, alloc_cb, recv_cb));
    uv_unref((uv_handle_t*)&s->udp_handle);
  }

  bufs[0] = uv_buf_init(&EXPECTED[0],  10);
  bufs[1] = uv_buf_init(&EXPECTED[10], 10);
  bufs[2] = uv_buf_init(&EXPECTED[20], 10);
  bufs[3] = uv_buf_init(&EXPECTED[30], 10);
  bufs[4] = uv_buf_init(&EXPECTED[40], 5);

  for (i = 0; i < n_senders; i++) {
    struct sender_state* s = senders + i;
    ASSERT_OK(uv_ip4_addr("127.0.0.1",
                          BASE_PORT + (i % n_receivers),
                          &s->addr));
    ASSERT_OK(uv_udp_init(loop, &s->udp_handle));
    ASSERT_OK(uv_udp_send(&s->send_req,
                          &s->udp_handle,
                          bufs,
                          ARRAY_SIZE(bufs),
                          (const struct sockaddr*) &s->addr,
                          send_cb));
  }

  duration = uv_hrtime();
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  duration = uv_hrtime() - duration;
  /* convert from nanoseconds to milliseconds */
  duration = duration / (uint64_t) 1e6;

  printf("udp_pummel_%dv%d: %.0f/s received, %.0f/s sent. "
         "%u received, %u sent in %.1f seconds.\n",
         n_receivers,
         n_senders,
         recv_cb_called / (duration / 1000.0),
         send_cb_called / (duration / 1000.0),
         recv_cb_called,
         send_cb_called,
         duration / 1000.0);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


#define X(a, b)                                                               \
  BENCHMARK_IMPL(udp_pummel_##a##v##b) {                                      \
    return pummel(a, b, 0);                                                   \
  }                                                                           \
  BENCHMARK_IMPL(udp_timed_pummel_##a##v##b) {                                \
    return pummel(a, b, TEST_DURATION);                                       \
  }

X(1, 1)
X(1, 10)
X(1, 100)
X(1, 1000)
X(10, 10)
X(10, 100)
X(10, 1000)
X(100, 10)
X(100, 100)
X(100, 1000)
X(1000, 1000)

#undef X
