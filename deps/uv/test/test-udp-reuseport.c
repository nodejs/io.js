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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__linux__) && !defined(__FreeBSD__) && \
    !defined(__DragonFly__) && !defined(__sun) && !defined(_AIX73)

TEST_IMPL(udp_reuseport) {
  struct sockaddr_in addr1, addr2, addr3;
  uv_loop_t* loop;
  uv_udp_t handle1, handle2, handle3;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr1));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT_2, &addr2));
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT_3, &addr3));

  loop = uv_default_loop();
  ASSERT_NOT_NULL(loop);

  r = uv_udp_init(loop, &handle1);
  ASSERT_OK(r);

  r = uv_udp_bind(&handle1, (const struct sockaddr*) &addr1, UV_UDP_REUSEADDR);
  ASSERT_OK(r);

  r = uv_udp_init(loop, &handle2);
  ASSERT_OK(r);

  r = uv_udp_bind(&handle2, (const struct sockaddr*) &addr2, UV_UDP_REUSEPORT);
  ASSERT_EQ(r, UV_ENOTSUP);

  r = uv_udp_init(loop, &handle3);
  ASSERT_OK(r);

  /* For platforms where SO_REUSEPORTs don't have the capability of
   * load balancing, specifying both UV_UDP_REUSEADDR and UV_UDP_REUSEPORT
   * in flags will fail, returning an UV_ENOTSUP error. */
  r = uv_udp_bind(&handle3, (const struct sockaddr*) &addr3,
                  UV_UDP_REUSEADDR | UV_UDP_REUSEPORT);
  ASSERT_EQ(r, UV_ENOTSUP);

  MAKE_VALGRIND_HAPPY(loop);

  return 0;
}

#else

#define NUM_RECEIVING_THREADS 2
#define MAX_UDP_DATAGRAMS 10

static uv_udp_t udp_send_handles[MAX_UDP_DATAGRAMS];
static uv_udp_send_t udp_send_requests[MAX_UDP_DATAGRAMS];

static uv_sem_t semaphore;

static uv_mutex_t mutex;
static unsigned int received;

static unsigned int thread_loop1_recv;
static unsigned int thread_loop2_recv;
static unsigned int sent;

static uv_loop_t* main_loop;
static uv_loop_t thread_loop1;
static uv_loop_t thread_loop2;
static uv_udp_t thread_handle1;
static uv_udp_t thread_handle2;
static uv_timer_t thread_timer_handle1;
static uv_timer_t thread_timer_handle2;

static void alloc_cb(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = (int) suggested_size;
}

static void ticktack(uv_timer_t* timer) {
  int done = 0;

  ASSERT(timer == &thread_timer_handle1 || timer == &thread_timer_handle2);

  uv_mutex_lock(&mutex);
  if (received == MAX_UDP_DATAGRAMS) {
    done = 1;
  }
  uv_mutex_unlock(&mutex);

  if (done) {
    uv_close((uv_handle_t*) timer, NULL);
    if (timer->loop == &thread_loop1)
      uv_close((uv_handle_t*) &thread_handle1, NULL);
    if (timer->loop == &thread_loop2)
      uv_close((uv_handle_t*) &thread_handle2, NULL);
  }
}

static void on_recv(uv_udp_t* handle,
                    ssize_t nr,
                    const uv_buf_t* buf,
                    const struct sockaddr* addr,
                    unsigned flags) {
  ASSERT_OK(flags);
  ASSERT(handle == &thread_handle1 || handle == &thread_handle2);

  ASSERT_GE(nr, 0);

  if (nr == 0) {
    ASSERT_NULL(addr);
    free(buf->base);
    return;
  }

  ASSERT_NOT_NULL(addr);
  ASSERT_EQ(5, nr);
  ASSERT(!memcmp("Hello", buf->base, nr));
  free(buf->base);

  if (handle->loop == &thread_loop1)
    thread_loop1_recv++;

  if (handle->loop == &thread_loop2)
    thread_loop2_recv++;

  uv_mutex_lock(&mutex);
  received++;
  uv_mutex_unlock(&mutex);
}

static void on_send(uv_udp_send_t* req, int status) {
  ASSERT_OK(status);
  ASSERT_PTR_EQ(req->handle->loop, main_loop);

  if (++sent == MAX_UDP_DATAGRAMS)
    uv_close((uv_handle_t*) req->handle, NULL);
}

static void bind_socket_and_prepare_recv(uv_loop_t* loop, uv_udp_t* handle) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_udp_init(loop, handle);
  ASSERT_OK(r);

  /* For platforms where SO_REUSEPORTs have the capability of
   * load balancing, specifying both UV_UDP_REUSEADDR and
   * UV_UDP_REUSEPORT in flags is allowed and SO_REUSEPORT will
   * always override the behavior of SO_REUSEADDR. */
  r = uv_udp_bind(handle, (const struct sockaddr*) &addr,
                  UV_UDP_REUSEADDR | UV_UDP_REUSEPORT);
  ASSERT_OK(r);

  r = uv_udp_recv_start(handle, alloc_cb, on_recv);
  ASSERT_OK(r);
}

static void run_event_loop(void* arg) {
  int r;
  uv_udp_t* handle;
  uv_timer_t* timer;
  uv_loop_t* loop = (uv_loop_t*) arg;
  ASSERT(loop == &thread_loop1 || loop == &thread_loop2);

  if (loop == &thread_loop1) {
    handle = &thread_handle1;
    timer = &thread_timer_handle1;
  } else {
    handle = &thread_handle2;
    timer = &thread_timer_handle2;
  }

  bind_socket_and_prepare_recv(loop, handle);
  r = uv_timer_init(loop, timer);
  ASSERT_OK(r);
  r = uv_timer_start(timer, ticktack, 0, 10);
  ASSERT_OK(r);

  /* Notify the main thread to start sending data. */
  uv_sem_post(&semaphore);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);
}

TEST_IMPL(udp_reuseport) {
  struct sockaddr_in addr;
  uv_buf_t buf;
  int r;
  int i;

  r = uv_mutex_init(&mutex);
  ASSERT_OK(r);

  r = uv_sem_init(&semaphore, 0);
  ASSERT_OK(r);

  main_loop = uv_default_loop();
  ASSERT_NOT_NULL(main_loop);

  /* Run event loops of receiving sockets in separate threads. */
  uv_loop_init(&thread_loop1);
  uv_loop_init(&thread_loop2);
  uv_thread_t thread_loop_id1;
  uv_thread_t thread_loop_id2;
  uv_thread_create(&thread_loop_id1, run_event_loop, &thread_loop1);
  uv_thread_create(&thread_loop_id2, run_event_loop, &thread_loop2);

  /* Wait until all threads to poll for receiving datagrams
   * before we start to sending. Otherwise the incoming datagrams
   * might not be distributed across all receiving threads. */
  for (i = 0; i < NUM_RECEIVING_THREADS; i++)
    uv_sem_wait(&semaphore);
  /* Now we know all threads are up and entering the uv_run(),
   * but we still sleep a little bit just for dual fail-safe. */
  uv_sleep(100);

  /* Start sending datagrams to the peers. */
  buf = uv_buf_init("Hello", 5);
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  for (i = 0; i < MAX_UDP_DATAGRAMS; i++) {
    r = uv_udp_init(main_loop, &udp_send_handles[i]);
    ASSERT_OK(r);
    r = uv_udp_send(&udp_send_requests[i],
                    &udp_send_handles[i],
                    &buf,
                    1,
                    (const struct sockaddr*) &addr,
                    on_send);
    ASSERT_OK(r);
  }

  r = uv_run(main_loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  /* Wait for all threads to exit. */
  uv_thread_join(&thread_loop_id1);
  uv_thread_join(&thread_loop_id2);

  /* Verify if each receiving socket per event loop received datagrams
   * and the amount of received datagrams matches the one of sent datagrams.
   */
  ASSERT_EQ(received, MAX_UDP_DATAGRAMS);
  ASSERT_EQ(sent, MAX_UDP_DATAGRAMS);
  ASSERT_GT(thread_loop1_recv, 0);
  ASSERT_GT(thread_loop2_recv, 0);
  ASSERT_EQ(thread_loop1_recv + thread_loop2_recv, sent);

  /* Clean up. */
  uv_mutex_destroy(&mutex);

  uv_sem_destroy(&semaphore);

  uv_loop_close(&thread_loop1);
  uv_loop_close(&thread_loop2);
  MAKE_VALGRIND_HAPPY(main_loop);

  return 0;
}

#endif
