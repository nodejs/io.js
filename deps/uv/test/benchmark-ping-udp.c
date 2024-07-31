/* Copyright libuv project contributors. All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>

/* Run the benchmark for this many ms */
#define TIME 5000

typedef struct {
  int pongs;
  int state;
  uv_udp_t udp;
  struct sockaddr_in server_addr;
} pinger_t;

typedef struct buf_s {
  uv_buf_t uv_buf_t;
  struct buf_s* next;
} buf_t;

static char PING[] = "PING\n";

static uv_loop_t* loop;

static int completed_pingers;
static unsigned long completed_pings;
static int64_t start_time;


static void buf_alloc(uv_handle_t* tcp, size_t size, uv_buf_t* buf) {
  static char slab[64 * 1024];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void buf_free(const uv_buf_t* buf) {
}


static void pinger_close_cb(uv_handle_t* handle) {
  pinger_t* pinger;

  pinger = (pinger_t*)handle->data;
#if DEBUG
  fprintf(stderr, "ping_pongs: %d roundtrips/s\n",
	  pinger->pongs / (TIME / 1000));
#endif

  completed_pings += pinger->pongs;
  completed_pingers++;
  free(pinger);
}

static void pinger_write_ping(pinger_t* pinger) {
  uv_buf_t buf;
  int r;

  buf = uv_buf_init(PING, sizeof(PING) - 1);
  r = uv_udp_try_send(&pinger->udp, &buf, 1,
                      (const struct sockaddr*) &pinger->server_addr);
  if (r < 0)
    FATAL("uv_udp_send failed");
}

static void pinger_read_cb(uv_udp_t* udp,
                           ssize_t nread,
                           const uv_buf_t* buf,
                           const struct sockaddr* addr,
                           unsigned flags) {
  ssize_t i;
  pinger_t* pinger;
  pinger = (pinger_t*)udp->data;

  /* No data here means something went wrong */
  ASSERT_GT(nread, 0);

  /* Now we count the pings */
  for (i = 0; i < nread; i++) {
    ASSERT_EQ(buf->base[i], PING[pinger->state]);
    pinger->state = (pinger->state + 1) % (sizeof(PING) - 1);
    if (pinger->state == 0) {
      pinger->pongs++;
      if (uv_now(loop) - start_time > TIME) {
        uv_close((uv_handle_t*)udp, pinger_close_cb);
        break;
      }
      pinger_write_ping(pinger);
    }
  }

  if (buf && !(flags & UV_UDP_MMSG_CHUNK))
    buf_free(buf);
}

static void udp_pinger_new(void) {
  pinger_t* pinger = malloc(sizeof(*pinger));
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &pinger->server_addr));
  pinger->state = 0;
  pinger->pongs = 0;

  /* Try to do NUM_PINGS ping-pongs (connection-less). */
  r = uv_udp_init(loop, &pinger->udp);
  ASSERT_OK(r);
  r = uv_udp_bind(&pinger->udp, (const struct sockaddr*) &pinger->server_addr, 0);
  ASSERT_OK(r);

  pinger->udp.data = pinger;

  /* Start pinging */
  if (0 != uv_udp_recv_start(&pinger->udp, buf_alloc, pinger_read_cb)) {
    FATAL("uv_udp_read_start failed");
  }
  pinger_write_ping(pinger);
}

static int ping_udp(unsigned pingers) {
  unsigned i;

  loop = uv_default_loop();
  start_time = uv_now(loop);

  for (i = 0; i < pingers; ++i) {
    udp_pinger_new();
  }
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_GE(completed_pingers, 1);

  fprintf(stderr, "ping_pongs: %d pingers, ~ %lu roundtrips/s\n",
          completed_pingers, completed_pings / (TIME/1000));

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

#define X(PINGERS) \
BENCHMARK_IMPL(ping_udp##PINGERS) {\
  return ping_udp(PINGERS); \
}

X(1)
X(10)
X(100)

#undef X
