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

static uv_cond_t condvar;
static uv_mutex_t mutex;
static uv_rwlock_t rwlock;
static int step;

/* The mutex and rwlock tests are really poor.
 * They're very basic sanity checks and nothing more.
 * Apologies if that rhymes.
 */

TEST_IMPL(thread_mutex) {
  uv_mutex_t mutex;
  int r;

  r = uv_mutex_init(&mutex);
  ASSERT_OK(r);

  uv_mutex_lock(&mutex);
  uv_mutex_unlock(&mutex);
  uv_mutex_destroy(&mutex);

  return 0;
}


TEST_IMPL(thread_mutex_recursive) {
  uv_mutex_t mutex;
  int r;

  r = uv_mutex_init_recursive(&mutex);
  ASSERT_OK(r);

  uv_mutex_lock(&mutex);
  uv_mutex_lock(&mutex);
  ASSERT_OK(uv_mutex_trylock(&mutex));

  uv_mutex_unlock(&mutex);
  uv_mutex_unlock(&mutex);
  uv_mutex_unlock(&mutex);
  uv_mutex_destroy(&mutex);

  return 0;
}


TEST_IMPL(thread_rwlock) {
  uv_rwlock_t rwlock;
  int r;

  r = uv_rwlock_init(&rwlock);
  ASSERT_OK(r);

  uv_rwlock_rdlock(&rwlock);
  uv_rwlock_rdunlock(&rwlock);
  uv_rwlock_wrlock(&rwlock);
  uv_rwlock_wrunlock(&rwlock);
  uv_rwlock_destroy(&rwlock);

  return 0;
}


/* Call when holding |mutex|. */
static void synchronize_nowait(void) {
  step += 1;
  uv_cond_signal(&condvar);
}


/* Call when holding |mutex|. */
static void synchronize(void) {
  int current;

  synchronize_nowait();
  /* Wait for the other thread.  Guard against spurious wakeups. */
  for (current = step; current == step; uv_cond_wait(&condvar, &mutex));
  ASSERT_EQ(step, current + 1);
}


static void thread_rwlock_trylock_peer(void* unused) {
  (void) &unused;

  uv_mutex_lock(&mutex);

  /* Write lock held by other thread. */
  ASSERT_EQ(UV_EBUSY, uv_rwlock_tryrdlock(&rwlock));
  ASSERT_EQ(UV_EBUSY, uv_rwlock_trywrlock(&rwlock));
  synchronize();

  /* Read lock held by other thread. */
  ASSERT_OK(uv_rwlock_tryrdlock(&rwlock));
  uv_rwlock_rdunlock(&rwlock);
  ASSERT_EQ(UV_EBUSY, uv_rwlock_trywrlock(&rwlock));
  synchronize();

  /* Acquire write lock. */
  ASSERT_OK(uv_rwlock_trywrlock(&rwlock));
  synchronize();

  /* Release write lock and acquire read lock. */
  uv_rwlock_wrunlock(&rwlock);
  ASSERT_OK(uv_rwlock_tryrdlock(&rwlock));
  synchronize();

  uv_rwlock_rdunlock(&rwlock);
  synchronize_nowait();  /* Signal main thread we're going away. */
  uv_mutex_unlock(&mutex);
}


TEST_IMPL(thread_rwlock_trylock) {
  uv_thread_t thread;

  ASSERT_OK(uv_cond_init(&condvar));
  ASSERT_OK(uv_mutex_init(&mutex));
  ASSERT_OK(uv_rwlock_init(&rwlock));

  uv_mutex_lock(&mutex);
  ASSERT_OK(uv_thread_create(&thread, thread_rwlock_trylock_peer, NULL));

  /* Hold write lock. */
  ASSERT_OK(uv_rwlock_trywrlock(&rwlock));
  synchronize();  /* Releases the mutex to the other thread. */

  /* Release write lock and acquire read lock.  Pthreads doesn't support
   * the notion of upgrading or downgrading rwlocks, so neither do we.
   */
  uv_rwlock_wrunlock(&rwlock);
  ASSERT_OK(uv_rwlock_tryrdlock(&rwlock));
  synchronize();

  /* Release read lock. */
  uv_rwlock_rdunlock(&rwlock);
  synchronize();

  /* Write lock held by other thread. */
  ASSERT_EQ(UV_EBUSY, uv_rwlock_tryrdlock(&rwlock));
  ASSERT_EQ(UV_EBUSY, uv_rwlock_trywrlock(&rwlock));
  synchronize();

  /* Read lock held by other thread. */
  ASSERT_OK(uv_rwlock_tryrdlock(&rwlock));
  uv_rwlock_rdunlock(&rwlock);
  ASSERT_EQ(UV_EBUSY, uv_rwlock_trywrlock(&rwlock));
  synchronize();

  ASSERT_OK(uv_thread_join(&thread));
  uv_rwlock_destroy(&rwlock);
  uv_mutex_unlock(&mutex);
  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&condvar);

  return 0;
}
