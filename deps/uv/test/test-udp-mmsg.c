/* Copyright libuv contributors. All rights reserved.
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
#include <string.h>

#define CHECK_HANDLE(handle) \
  ASSERT_NE((uv_udp_t*)(handle) == &recver || (uv_udp_t*)(handle) == &sender, 0)

#define BUFFER_MULTIPLIER 20
#define MAX_DGRAM_SIZE (64 * 1024)
#define NUM_SENDS 40

static uv_udp_t recver;
static uv_udp_t sender;
static int recv_cb_called;
static int received_datagrams;
static int read_bytes;
static int close_cb_called;
static int alloc_cb_called;


static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  size_t buffer_size;
  CHECK_HANDLE(handle);

  /* Only allocate enough room for multiple dgrams if we can actually recv them */
  buffer_size = MAX_DGRAM_SIZE;
  if (uv_udp_using_recvmmsg((uv_udp_t*)handle))
    buffer_size *= BUFFER_MULTIPLIER;

  /* Actually malloc to exercise free'ing the buffer later */
  buf->base = malloc(buffer_size);
  ASSERT_NOT_NULL(buf->base);
  buf->len = buffer_size;
  alloc_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  ASSERT(uv_is_closing(handle));
  close_cb_called++;
}


static void recv_cb(uv_udp_t* handle,
                    ssize_t nread,
                    const uv_buf_t* rcvbuf,
                    const struct sockaddr* addr,
                    unsigned flags) {
  ASSERT_GE(nread, 0);
  read_bytes += nread;

  /* free and return if this is a mmsg free-only callback invocation */
  if (flags & UV_UDP_MMSG_FREE) {
    ASSERT_OK(nread);
    ASSERT_NULL(addr);
    free(rcvbuf->base);
    return;
  }

  if (nread == 0) {
    /* There can be no more available data for the time being. */
    ASSERT_NULL(addr);
  } else {
    ASSERT_EQ(4, nread);
    ASSERT_NOT_NULL(addr);
    ASSERT_MEM_EQ("PING", rcvbuf->base, nread);
    received_datagrams++;
  }

  recv_cb_called++;
  if (received_datagrams == NUM_SENDS) {
    uv_close((uv_handle_t*) handle, close_cb);
    uv_close((uv_handle_t*) &sender, close_cb);
  }

  /* Don't free if the buffer could be reused via mmsg */
  if (rcvbuf && !(flags & UV_UDP_MMSG_CHUNK))
    free(rcvbuf->base);
}


TEST_IMPL(udp_mmsg) {
  struct sockaddr_in addr;
  uv_buf_t buf;
  int i;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));

  ASSERT_OK(uv_udp_init_ex(uv_default_loop(), &recver,
                           AF_UNSPEC | UV_UDP_RECVMMSG));

  ASSERT_OK(uv_udp_bind(&recver, (const struct sockaddr*) &addr, 0));

  ASSERT_OK(uv_udp_recv_start(&recver, alloc_cb, recv_cb));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  ASSERT_OK(uv_udp_init(uv_default_loop(), &sender));

  buf = uv_buf_init("PING", 4);
  for (i = 0; i < NUM_SENDS; i++) {
    ASSERT_EQ(4, uv_udp_try_send(&sender, &buf, 1, (const struct sockaddr*) &addr));
  }

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(2, close_cb_called);
  ASSERT_EQ(received_datagrams, NUM_SENDS);

  ASSERT_OK(sender.send_queue_size);
  ASSERT_OK(recver.send_queue_size);

  printf("%d allocs for %d recvs\n", alloc_cb_called, recv_cb_called);

  /* On platforms that don't support mmsg, each recv gets its own alloc */
  if (uv_udp_using_recvmmsg(&recver))
    ASSERT_EQ(read_bytes, NUM_SENDS * 4); /* we're sending 4 bytes per datagram */
  else
    ASSERT_EQ(alloc_cb_called, recv_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
