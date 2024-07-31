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

#include <string.h>
#include <errno.h>

struct worker_config;

typedef void (*signal_func)(struct worker_config* c, int* flag);
typedef int (*wait_func)(struct worker_config* c, const int* flag);

typedef struct worker_config {
  uv_sem_t sem_waiting; /* post before waiting. */
  uv_sem_t sem_signaled; /* post after signaling. */
  uv_mutex_t mutex;
  uv_cond_t cond;
  int use_broadcast;
  int posted_1;
  int posted_2;
  signal_func signal_cond;
  wait_func wait_cond;
} worker_config;

void worker_config_init(worker_config* wc,
                        int use_broadcast,
                        signal_func signal_f,
                        wait_func wait_f) {
  /* Wipe. */
  memset(wc, 0, sizeof(*wc));

  /* Copy vars. */
  wc->signal_cond = signal_f;
  wc->wait_cond = wait_f;
  wc->use_broadcast = use_broadcast;

  /* Init. */
  ASSERT_OK(uv_sem_init(&wc->sem_waiting, 0));
  ASSERT_OK(uv_sem_init(&wc->sem_signaled, 0));
  ASSERT_OK(uv_cond_init(&wc->cond));
  ASSERT_OK(uv_mutex_init(&wc->mutex));
}

void worker_config_destroy(worker_config* wc) {
  uv_mutex_destroy(&wc->mutex);
  uv_cond_destroy(&wc->cond);
  uv_sem_destroy(&wc->sem_signaled);
  uv_sem_destroy(&wc->sem_waiting);
}

/* arg is a worker_config.
 * Call signal_cond then wait_cond.
 * Partner should call wait then signal. */
static void worker(void* arg) {
  worker_config* c = arg;
  c->signal_cond(c, &c->posted_1);
  c->wait_cond(c, &c->posted_2);
}

/* 1. Signal a waiting waiter.
 * 2. Tell waiter we finished. */
static void condvar_signal(worker_config* c, int* flag) {
  /* Wait until waiter holds mutex and is preparing to wait. */
  uv_sem_wait(&c->sem_waiting);

  /* Make sure waiter has begun waiting. */
  uv_mutex_lock(&c->mutex);

  /* Help waiter differentiate between spurious and legitimate wakeup. */
  ASSERT_OK(*flag);
  *flag = 1;

  if (c->use_broadcast)
    uv_cond_broadcast(&c->cond);
  else
    uv_cond_signal(&c->cond);

  uv_mutex_unlock(&c->mutex);

  /* Done signaling. */
  uv_sem_post(&c->sem_signaled);
}

/* 1. Wait on a signal.
 * 2. Ensure that the signaler finished. */
static int condvar_wait(worker_config* c, const int* flag) {
  uv_mutex_lock(&c->mutex);

  /* Tell signal'er that I am waiting. */
  uv_sem_post(&c->sem_waiting);

  /* Wait until I get a non-spurious signal. */
  do {
    uv_cond_wait(&c->cond, &c->mutex);
  } while (*flag == 0);
  ASSERT_EQ(1, *flag);

  uv_mutex_unlock(&c->mutex);

  /* Wait for my signal'er to finish. */
  uv_sem_wait(&c->sem_signaled);

  return 0;
}

/* uv_cond_wait: One thread signals, the other waits. */
TEST_IMPL(condvar_1) {
  worker_config wc;
  uv_thread_t thread;

  /* Helper signal-then-wait. */
  worker_config_init(&wc, 0, condvar_signal, condvar_wait);
  ASSERT_OK(uv_thread_create(&thread, worker, &wc));

  /* We wait-then-signal. */
  ASSERT_OK(wc.wait_cond(&wc, &wc.posted_1));
  wc.signal_cond(&wc, &wc.posted_2);

  ASSERT_OK(uv_thread_join(&thread));
  worker_config_destroy(&wc);

  return 0;
}

/* uv_cond_wait: One thread broadcasts, the other waits. */
TEST_IMPL(condvar_2) {
  worker_config wc;
  uv_thread_t thread;

  /* Helper to signal-then-wait. */
  worker_config_init(&wc, 1, condvar_signal, condvar_wait);
  ASSERT_OK(uv_thread_create(&thread, worker, &wc));

  /* We wait-then-signal. */
  ASSERT_OK(wc.wait_cond(&wc, &wc.posted_1));
  wc.signal_cond(&wc, &wc.posted_2);

  ASSERT_OK(uv_thread_join(&thread));
  worker_config_destroy(&wc);

  return 0;
}

/* 1. Wait on a signal (hopefully not timeout, else we'll hang).
 * 2. Ensure that the signaler finished. */
static int condvar_timedwait(worker_config* c, const int* flag) {
  int r;

  r = 0;

  uv_mutex_lock(&c->mutex);

  /* Tell signal'er that I am waiting. */
  uv_sem_post(&c->sem_waiting);

  /* Wait until I get a non-spurious signal. */
  do {
    r = uv_cond_timedwait(&c->cond, &c->mutex, (uint64_t)(1 * 1e9)); /* 1 s */
    ASSERT_OK(r); /* Should not time out. */
  } while (*flag == 0);
  ASSERT_EQ(1, *flag);

  uv_mutex_unlock(&c->mutex);

  /* Wait for my signal'er to finish. */
  uv_sem_wait(&c->sem_signaled);
  return r;
}

/* uv_cond_timedwait: One thread signals, the other timedwaits. */
TEST_IMPL(condvar_3) {
  worker_config wc;
  uv_thread_t thread;

  /* Helper to signal-then-wait. */
  worker_config_init(&wc, 0, condvar_signal, condvar_timedwait);
  ASSERT_OK(uv_thread_create(&thread, worker, &wc));

  /* We wait-then-signal. */
  wc.wait_cond(&wc, &wc.posted_1);
  wc.signal_cond(&wc, &wc.posted_2);

  ASSERT_OK(uv_thread_join(&thread));
  worker_config_destroy(&wc);

  return 0;
}

/* uv_cond_timedwait: One thread broadcasts, the other waits. */
TEST_IMPL(condvar_4) {
  worker_config wc;
  uv_thread_t thread;

  /* Helper to signal-then-wait. */
  worker_config_init(&wc, 1, condvar_signal, condvar_timedwait);
  ASSERT_OK(uv_thread_create(&thread, worker, &wc));

  /* We wait-then-signal. */
  wc.wait_cond(&wc, &wc.posted_1);
  wc.signal_cond(&wc, &wc.posted_2);

  ASSERT_OK(uv_thread_join(&thread));
  worker_config_destroy(&wc);

  return 0;
}

/* uv_cond_timedwait: One thread waits, no signal. Timeout should be delivered. */
TEST_IMPL(condvar_5) {
  worker_config wc;
  uint64_t timeout;

  timeout = 100 * 1000 * 1000; /* 100 ms in ns */

  /* Mostly irrelevant. We need cond and mutex initialized. */
  worker_config_init(&wc, 0, NULL, NULL);

  uv_mutex_lock(&wc.mutex);

  /* We wait. No signaler, so this will only return if timeout is delivered. */
  ASSERT_EQ(UV_ETIMEDOUT, uv_cond_timedwait(&wc.cond, &wc.mutex, timeout));

  uv_mutex_unlock(&wc.mutex);

  worker_config_destroy(&wc);

  return 0;
}
