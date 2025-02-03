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

TEST_IMPL(tcp_reuseport) {
  struct sockaddr_in addr;
  uv_loop_t* loop;
  uv_tcp_t handle;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  loop = uv_default_loop();
  ASSERT_NOT_NULL(loop);

  r = uv_tcp_init(loop, &handle);
  ASSERT_OK(r);

  r = uv_tcp_bind(&handle, (const struct sockaddr*) &addr, UV_TCP_REUSEPORT);
  ASSERT_EQ(r, UV_ENOTSUP);

  MAKE_VALGRIND_HAPPY(loop);

  return 0;
}

#else

#define NUM_LISTENING_THREADS 2
#define MAX_TCP_CLIENTS 10

static uv_tcp_t tcp_connect_handles[MAX_TCP_CLIENTS];
static uv_connect_t tcp_connect_requests[MAX_TCP_CLIENTS];

static uv_sem_t semaphore;

static uv_mutex_t mutex;
static unsigned int accepted;

static unsigned int thread_loop1_accepted;
static unsigned int thread_loop2_accepted;
static unsigned int connected;

static uv_loop_t* main_loop;
static uv_loop_t thread_loop1;
static uv_loop_t thread_loop2;
static uv_tcp_t thread_handle1;
static uv_tcp_t thread_handle2;
static uv_timer_t thread_timer_handle1;
static uv_timer_t thread_timer_handle2;

static void on_close(uv_handle_t* handle) {
  free(handle);
}

static void ticktack(uv_timer_t* timer) {
  ASSERT(timer == &thread_timer_handle1 || timer == &thread_timer_handle2);

  int done = 0;
  uv_mutex_lock(&mutex);
  if (accepted == MAX_TCP_CLIENTS) {
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

static void on_connection(uv_stream_t* server, int status)
{
  ASSERT_OK(status);
  ASSERT(server == (uv_stream_t*) &thread_handle1 || \
         server == (uv_stream_t*) &thread_handle2);

  uv_tcp_t *client = malloc(sizeof(uv_tcp_t));
  ASSERT_OK(uv_tcp_init(server->loop, client));
  ASSERT_OK(uv_accept(server, (uv_stream_t*) client));
  uv_close((uv_handle_t*) client, on_close);

  if (server->loop == &thread_loop1)
    thread_loop1_accepted++;

  if (server->loop == &thread_loop2)
    thread_loop2_accepted++;

  uv_mutex_lock(&mutex);
  accepted++;
  uv_mutex_unlock(&mutex);
}

static void on_connect(uv_connect_t* req, int status) {
  ASSERT_OK(status);
  ASSERT_NOT_NULL(req->handle);
  ASSERT_PTR_EQ(req->handle->loop, main_loop);

  connected++;
  uv_close((uv_handle_t*) req->handle, NULL);
}

static void create_listener(uv_loop_t* loop, uv_tcp_t* handle) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_tcp_init(loop, handle);
  ASSERT_OK(r);

  r = uv_tcp_bind(handle, (const struct sockaddr*) &addr, UV_TCP_REUSEPORT);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*) handle, 128, on_connection);
  ASSERT_OK(r);
}

static void run_event_loop(void* arg) {
  int r;
  uv_tcp_t* handle;
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

  create_listener(loop, handle);
  r = uv_timer_init(loop, timer);
  ASSERT_OK(r);
  r = uv_timer_start(timer, ticktack, 0, 10);
  ASSERT_OK(r);

  /* Notify the main thread to start connecting. */
  uv_sem_post(&semaphore);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);
}

TEST_IMPL(tcp_reuseport) {
  struct sockaddr_in addr;
  int r;
  int i;

  r = uv_mutex_init(&mutex);
  ASSERT_OK(r);

  r = uv_sem_init(&semaphore, 0);
  ASSERT_OK(r);

  main_loop = uv_default_loop();
  ASSERT_NOT_NULL(main_loop);

  /* Run event loops of listeners in separate threads. */
  uv_loop_init(&thread_loop1);
  uv_loop_init(&thread_loop2);
  uv_thread_t thread_loop_id1;
  uv_thread_t thread_loop_id2;
  uv_thread_create(&thread_loop_id1, run_event_loop, &thread_loop1);
  uv_thread_create(&thread_loop_id2, run_event_loop, &thread_loop2);

  /* Wait until all threads to poll for accepting connections
   * before we start to connect. Otherwise the incoming connections
   * might not be distributed across all listening threads. */
  for (i = 0; i < NUM_LISTENING_THREADS; i++)
    uv_sem_wait(&semaphore);
  /* Now we know all threads are up and entering the uv_run(),
   * but we still sleep a little bit just for dual fail-safe. */
  uv_sleep(100);

  /* Start connecting to the peers. */
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  for (i = 0; i < MAX_TCP_CLIENTS; i++) {
    r = uv_tcp_init(main_loop, &tcp_connect_handles[i]);
    ASSERT_OK(r);
    r = uv_tcp_connect(&tcp_connect_requests[i],
                       &tcp_connect_handles[i],
                       (const struct sockaddr*) &addr,
                       on_connect);
    ASSERT_OK(r);
  }

  r = uv_run(main_loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  /* Wait for all threads to exit. */
  uv_thread_join(&thread_loop_id1);
  uv_thread_join(&thread_loop_id2);

  /* Verify if each listener per event loop accepted connections
   * and the amount of accepted connections matches the one of
   * connected connections.
   */
  ASSERT_EQ(accepted, MAX_TCP_CLIENTS);
  ASSERT_EQ(connected, MAX_TCP_CLIENTS);
  ASSERT_GT(thread_loop1_accepted, 0);
  ASSERT_GT(thread_loop2_accepted, 0);
  ASSERT_EQ(thread_loop1_accepted + thread_loop2_accepted, connected);

  /* Clean up. */
  uv_mutex_destroy(&mutex);

  uv_sem_destroy(&semaphore);

  uv_loop_close(&thread_loop1);
  uv_loop_close(&thread_loop2);
  MAKE_VALGRIND_HAPPY(main_loop);

  return 0;
}

#endif
