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
#include "internal.h"

#include <pthread.h>
#ifdef __OpenBSD__
#include <pthread_np.h>
#endif
#include <assert.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/resource.h>  /* getrlimit() */
#include <unistd.h>  /* getpagesize() */

#include <limits.h>

#ifdef __MVS__
#include <sys/ipc.h>
#include <sys/sem.h>
#endif

#if defined(__GLIBC__) && !defined(__UCLIBC__)
#include <gnu/libc-version.h>  /* gnu_get_libc_version() */
#endif

#if defined(__linux__)
# include <sched.h>
# define uv__cpu_set_t cpu_set_t
#elif defined(__FreeBSD__)
# include <sys/param.h>
# include <sys/cpuset.h>
# include <pthread_np.h>
# define uv__cpu_set_t cpuset_t
#endif


#undef NANOSEC
#define NANOSEC ((uint64_t) 1e9)

/* Musl's PTHREAD_STACK_MIN is 2 KB on all architectures, which is
 * too small to safely receive signals on.
 *
 * Musl's PTHREAD_STACK_MIN + MINSIGSTKSZ == 8192 on arm64 (which has
 * the largest MINSIGSTKSZ of the architectures that musl supports) so
 * let's use that as a lower bound.
 *
 * We use a hardcoded value because PTHREAD_STACK_MIN + MINSIGSTKSZ
 * is between 28 and 133 KB when compiling against glibc, depending
 * on the architecture.
 */
static size_t uv__min_stack_size(void) {
  static const size_t min = 8192;

#ifdef PTHREAD_STACK_MIN  /* Not defined on NetBSD. */
  if (min < (size_t) PTHREAD_STACK_MIN)
    return PTHREAD_STACK_MIN;
#endif  /* PTHREAD_STACK_MIN */

  return min;
}


/* On Linux, threads created by musl have a much smaller stack than threads
 * created by glibc (80 vs. 2048 or 4096 kB.)  Follow glibc for consistency.
 */
static size_t uv__default_stack_size(void) {
#if !defined(__linux__)
  return 0;
#elif defined(__PPC__) || defined(__ppc__) || defined(__powerpc__)
  return 4 << 20;  /* glibc default. */
#else
  return 2 << 20;  /* glibc default. */
#endif
}


/* On MacOS, threads other than the main thread are created with a reduced
 * stack size by default.  Adjust to RLIMIT_STACK aligned to the page size.
 */
size_t uv__thread_stack_size(void) {
#if defined(__APPLE__) || defined(__linux__)
  struct rlimit lim;

  /* getrlimit() can fail on some aarch64 systems due to a glibc bug where
   * the system call wrapper invokes the wrong system call. Don't treat
   * that as fatal, just use the default stack size instead.
   */
  if (getrlimit(RLIMIT_STACK, &lim))
    return uv__default_stack_size();

  if (lim.rlim_cur == RLIM_INFINITY)
    return uv__default_stack_size();

  /* pthread_attr_setstacksize() expects page-aligned values. */
  lim.rlim_cur -= lim.rlim_cur % (rlim_t) getpagesize();

  if (lim.rlim_cur >= (rlim_t) uv__min_stack_size())
    return lim.rlim_cur;
#endif

  return uv__default_stack_size();
}


int uv_thread_create(uv_thread_t *tid, void (*entry)(void *arg), void *arg) {
  uv_thread_options_t params;
  params.flags = UV_THREAD_NO_FLAGS;
  return uv_thread_create_ex(tid, &params, entry, arg);
}


int uv_thread_detach(uv_thread_t *tid) {
  return UV__ERR(pthread_detach(*tid));
}


int uv_thread_create_ex(uv_thread_t* tid,
                        const uv_thread_options_t* params,
                        void (*entry)(void *arg),
                        void *arg) {
  int err;
  pthread_attr_t* attr;
  pthread_attr_t attr_storage;
  size_t pagesize;
  size_t stack_size;
  size_t min_stack_size;

  /* Used to squelch a -Wcast-function-type warning. */
  union {
    void (*in)(void*);
    void* (*out)(void*);
  } f;

  stack_size =
      params->flags & UV_THREAD_HAS_STACK_SIZE ? params->stack_size : 0;

  attr = NULL;
  if (stack_size == 0) {
    stack_size = uv__thread_stack_size();
  } else {
    pagesize = (size_t)getpagesize();
    /* Round up to the nearest page boundary. */
    stack_size = (stack_size + pagesize - 1) &~ (pagesize - 1);
    min_stack_size = uv__min_stack_size();
    if (stack_size < min_stack_size)
      stack_size = min_stack_size;
  }

  if (stack_size > 0) {
    attr = &attr_storage;

    if (pthread_attr_init(attr))
      abort();

    if (pthread_attr_setstacksize(attr, stack_size))
      abort();
  }

  f.in = entry;
  err = pthread_create(tid, attr, f.out, arg);

  if (attr != NULL)
    pthread_attr_destroy(attr);

  return UV__ERR(err);
}

#if UV__CPU_AFFINITY_SUPPORTED

int uv_thread_setaffinity(uv_thread_t* tid,
                          char* cpumask,
                          char* oldmask,
                          size_t mask_size) {
  int i;
  int r;
  uv__cpu_set_t cpuset;
  int cpumasksize;

  cpumasksize = uv_cpumask_size();
  if (cpumasksize < 0)
    return cpumasksize;
  if (mask_size < (size_t)cpumasksize)
    return UV_EINVAL;

  if (oldmask != NULL) {
    r = uv_thread_getaffinity(tid, oldmask, mask_size);
    if (r < 0)
      return r;
  }

  CPU_ZERO(&cpuset);
  for (i = 0; i < cpumasksize; i++)
    if (cpumask[i])
      CPU_SET(i, &cpuset);

#if defined(__ANDROID__)
  if (sched_setaffinity(pthread_gettid_np(*tid), sizeof(cpuset), &cpuset))
    r = errno;
  else
    r = 0;
#else
  r = pthread_setaffinity_np(*tid, sizeof(cpuset), &cpuset);
#endif

  return UV__ERR(r);
}


int uv_thread_getaffinity(uv_thread_t* tid,
                          char* cpumask,
                          size_t mask_size) {
  int r;
  int i;
  uv__cpu_set_t cpuset;
  int cpumasksize;

  cpumasksize = uv_cpumask_size();
  if (cpumasksize < 0)
    return cpumasksize;
  if (mask_size < (size_t)cpumasksize)
    return UV_EINVAL;

  CPU_ZERO(&cpuset);
#if defined(__ANDROID__)
  if (sched_getaffinity(pthread_gettid_np(*tid), sizeof(cpuset), &cpuset))
    r = errno;
  else
    r = 0;
#else
  r = pthread_getaffinity_np(*tid, sizeof(cpuset), &cpuset);
#endif
  if (r)
    return UV__ERR(r);
  for (i = 0; i < cpumasksize; i++)
    cpumask[i] = !!CPU_ISSET(i, &cpuset);

  return 0;
}
#else
int uv_thread_setaffinity(uv_thread_t* tid,
                          char* cpumask,
                          char* oldmask,
                          size_t mask_size) {
  return UV_ENOTSUP;
}


int uv_thread_getaffinity(uv_thread_t* tid,
                          char* cpumask,
                          size_t mask_size) {
  return UV_ENOTSUP;
}
#endif /* defined(__linux__) || defined(UV_BSD_H) */

int uv_thread_getcpu(void) {
#if UV__CPU_AFFINITY_SUPPORTED
  int cpu;

  cpu = sched_getcpu();
  if (cpu < 0)
    return UV__ERR(errno);

  return cpu;
#else
  return UV_ENOTSUP;
#endif
}

uv_thread_t uv_thread_self(void) {
  return pthread_self();
}

int uv_thread_join(uv_thread_t *tid) {
  return UV__ERR(pthread_join(*tid, NULL));
}


int uv_thread_equal(const uv_thread_t* t1, const uv_thread_t* t2) {
  return pthread_equal(*t1, *t2);
}

int uv_thread_setname(const char* name) {
  if (name == NULL)
    return UV_EINVAL;
  return uv__thread_setname(name);
}

int uv_thread_getname(uv_thread_t* tid, char* name, size_t size) {
  if (name == NULL || size == 0)
    return UV_EINVAL;

  return uv__thread_getname(tid, name, size);
}

int uv_mutex_init(uv_mutex_t* mutex) {
#if defined(NDEBUG) || !defined(PTHREAD_MUTEX_ERRORCHECK)
  return UV__ERR(pthread_mutex_init(mutex, NULL));
#else
  pthread_mutexattr_t attr;
  int err;

  if (pthread_mutexattr_init(&attr))
    abort();

  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK))
    abort();

  err = pthread_mutex_init(mutex, &attr);

  if (pthread_mutexattr_destroy(&attr))
    abort();

  return UV__ERR(err);
#endif
}


int uv_mutex_init_recursive(uv_mutex_t* mutex) {
  pthread_mutexattr_t attr;
  int err;

  if (pthread_mutexattr_init(&attr))
    abort();

  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE))
    abort();

  err = pthread_mutex_init(mutex, &attr);

  if (pthread_mutexattr_destroy(&attr))
    abort();

  return UV__ERR(err);
}


void uv_mutex_destroy(uv_mutex_t* mutex) {
  if (pthread_mutex_destroy(mutex))
    abort();
}


void uv_mutex_lock(uv_mutex_t* mutex) {
  if (pthread_mutex_lock(mutex))
    abort();
}


int uv_mutex_trylock(uv_mutex_t* mutex) {
  int err;

  err = pthread_mutex_trylock(mutex);
  if (err) {
    if (err != EBUSY && err != EAGAIN)
      abort();
    return UV_EBUSY;
  }

  return 0;
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
  if (pthread_mutex_unlock(mutex))
    abort();
}


int uv_rwlock_init(uv_rwlock_t* rwlock) {
  return UV__ERR(pthread_rwlock_init(rwlock, NULL));
}


void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_destroy(rwlock))
    abort();
}


void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_rdlock(rwlock))
    abort();
}


int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  int err;

  err = pthread_rwlock_tryrdlock(rwlock);
  if (err) {
    if (err != EBUSY && err != EAGAIN)
      abort();
    return UV_EBUSY;
  }

  return 0;
}


void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_unlock(rwlock))
    abort();
}


void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_wrlock(rwlock))
    abort();
}


int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  int err;

  err = pthread_rwlock_trywrlock(rwlock);
  if (err) {
    if (err != EBUSY && err != EAGAIN)
      abort();
    return UV_EBUSY;
  }

  return 0;
}


void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_unlock(rwlock))
    abort();
}


void uv_once(uv_once_t* guard, void (*callback)(void)) {
  if (pthread_once(guard, callback))
    abort();
}

#if defined(__APPLE__) && defined(__MACH__)

int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  kern_return_t err;

  err = semaphore_create(mach_task_self(), sem, SYNC_POLICY_FIFO, value);
  if (err == KERN_SUCCESS)
    return 0;
  if (err == KERN_INVALID_ARGUMENT)
    return UV_EINVAL;
  if (err == KERN_RESOURCE_SHORTAGE)
    return UV_ENOMEM;

  abort();
  return UV_EINVAL;  /* Satisfy the compiler. */
}


void uv_sem_destroy(uv_sem_t* sem) {
  if (semaphore_destroy(mach_task_self(), *sem))
    abort();
}


void uv_sem_post(uv_sem_t* sem) {
  if (semaphore_signal(*sem))
    abort();
}


void uv_sem_wait(uv_sem_t* sem) {
  int r;

  do
    r = semaphore_wait(*sem);
  while (r == KERN_ABORTED);

  if (r != KERN_SUCCESS)
    abort();
}


int uv_sem_trywait(uv_sem_t* sem) {
  mach_timespec_t interval;
  kern_return_t err;

  interval.tv_sec = 0;
  interval.tv_nsec = 0;

  err = semaphore_timedwait(*sem, interval);
  if (err == KERN_SUCCESS)
    return 0;
  if (err == KERN_OPERATION_TIMED_OUT)
    return UV_EAGAIN;

  abort();
  return UV_EINVAL;  /* Satisfy the compiler. */
}

#else /* !(defined(__APPLE__) && defined(__MACH__)) */

#if defined(__GLIBC__) && !defined(__UCLIBC__)

/* Hack around https://sourceware.org/bugzilla/show_bug.cgi?id=12674
 * by providing a custom implementation for glibc < 2.21 in terms of other
 * concurrency primitives.
 * Refs: https://github.com/nodejs/node/issues/19903 */

/* To preserve ABI compatibility, we treat the uv_sem_t as storage for
 * a pointer to the actual struct we're using underneath. */

static uv_once_t glibc_version_check_once = UV_ONCE_INIT;
static int platform_needs_custom_semaphore = 0;

static void glibc_version_check(void) {
  const char* version = gnu_get_libc_version();
  platform_needs_custom_semaphore =
      version[0] == '2' && version[1] == '.' &&
      atoi(version + 2) < 21;
}

#elif defined(__MVS__)

#define platform_needs_custom_semaphore 1

#else /* !defined(__GLIBC__) && !defined(__MVS__) */

#define platform_needs_custom_semaphore 0

#endif

typedef struct uv_semaphore_s {
  uv_mutex_t mutex;
  uv_cond_t cond;
  unsigned int value;
} uv_semaphore_t;

#if (defined(__GLIBC__) && !defined(__UCLIBC__)) || \
    platform_needs_custom_semaphore
STATIC_ASSERT(sizeof(uv_sem_t) >= sizeof(uv_semaphore_t*));
#endif

static int uv__custom_sem_init(uv_sem_t* sem_, unsigned int value) {
  int err;
  uv_semaphore_t* sem;

  sem = uv__malloc(sizeof(*sem));
  if (sem == NULL)
    return UV_ENOMEM;

  if ((err = uv_mutex_init(&sem->mutex)) != 0) {
    uv__free(sem);
    return err;
  }

  if ((err = uv_cond_init(&sem->cond)) != 0) {
    uv_mutex_destroy(&sem->mutex);
    uv__free(sem);
    return err;
  }

  sem->value = value;
  *(uv_semaphore_t**)sem_ = sem;
  return 0;
}


static void uv__custom_sem_destroy(uv_sem_t* sem_) {
  uv_semaphore_t* sem;

  sem = *(uv_semaphore_t**)sem_;
  uv_cond_destroy(&sem->cond);
  uv_mutex_destroy(&sem->mutex);
  uv__free(sem);
}


static void uv__custom_sem_post(uv_sem_t* sem_) {
  uv_semaphore_t* sem;

  sem = *(uv_semaphore_t**)sem_;
  uv_mutex_lock(&sem->mutex);
  sem->value++;
  if (sem->value == 1)
    uv_cond_signal(&sem->cond); /* Release one to replace us. */
  uv_mutex_unlock(&sem->mutex);
}


static void uv__custom_sem_wait(uv_sem_t* sem_) {
  uv_semaphore_t* sem;

  sem = *(uv_semaphore_t**)sem_;
  uv_mutex_lock(&sem->mutex);
  while (sem->value == 0)
    uv_cond_wait(&sem->cond, &sem->mutex);
  sem->value--;
  uv_mutex_unlock(&sem->mutex);
}


static int uv__custom_sem_trywait(uv_sem_t* sem_) {
  uv_semaphore_t* sem;

  sem = *(uv_semaphore_t**)sem_;
  if (uv_mutex_trylock(&sem->mutex) != 0)
    return UV_EAGAIN;

  if (sem->value == 0) {
    uv_mutex_unlock(&sem->mutex);
    return UV_EAGAIN;
  }

  sem->value--;
  uv_mutex_unlock(&sem->mutex);

  return 0;
}

static int uv__sem_init(uv_sem_t* sem, unsigned int value) {
  if (sem_init(sem, 0, value))
    return UV__ERR(errno);
  return 0;
}


static void uv__sem_destroy(uv_sem_t* sem) {
  if (sem_destroy(sem))
    abort();
}


static void uv__sem_post(uv_sem_t* sem) {
  if (sem_post(sem))
    abort();
}


static void uv__sem_wait(uv_sem_t* sem) {
  int r;

  do
    r = sem_wait(sem);
  while (r == -1 && errno == EINTR);

  if (r)
    abort();
}


static int uv__sem_trywait(uv_sem_t* sem) {
  int r;

  do
    r = sem_trywait(sem);
  while (r == -1 && errno == EINTR);

  if (r) {
    if (errno == EAGAIN)
      return UV_EAGAIN;
    abort();
  }

  return 0;
}

int uv_sem_init(uv_sem_t* sem, unsigned int value) {
#if defined(__GLIBC__) && !defined(__UCLIBC__)
  uv_once(&glibc_version_check_once, glibc_version_check);
#endif

  if (platform_needs_custom_semaphore)
    return uv__custom_sem_init(sem, value);
  else
    return uv__sem_init(sem, value);
}


void uv_sem_destroy(uv_sem_t* sem) {
  if (platform_needs_custom_semaphore)
    uv__custom_sem_destroy(sem);
  else
    uv__sem_destroy(sem);
}


void uv_sem_post(uv_sem_t* sem) {
  if (platform_needs_custom_semaphore)
    uv__custom_sem_post(sem);
  else
    uv__sem_post(sem);
}


void uv_sem_wait(uv_sem_t* sem) {
  if (platform_needs_custom_semaphore)
    uv__custom_sem_wait(sem);
  else
    uv__sem_wait(sem);
}


int uv_sem_trywait(uv_sem_t* sem) {
  if (platform_needs_custom_semaphore)
    return uv__custom_sem_trywait(sem);
  else
    return uv__sem_trywait(sem);
}

#endif /* defined(__APPLE__) && defined(__MACH__) */


#if defined(__APPLE__) && defined(__MACH__) || defined(__MVS__)

int uv_cond_init(uv_cond_t* cond) {
  return UV__ERR(pthread_cond_init(cond, NULL));
}

#else /* !(defined(__APPLE__) && defined(__MACH__)) */

int uv_cond_init(uv_cond_t* cond) {
  pthread_condattr_t attr;
  int err;

  err = pthread_condattr_init(&attr);
  if (err)
    return UV__ERR(err);

  err = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  if (err)
    goto error2;

  err = pthread_cond_init(cond, &attr);
  if (err)
    goto error2;

  err = pthread_condattr_destroy(&attr);
  if (err)
    goto error;

  return 0;

error:
  pthread_cond_destroy(cond);
error2:
  pthread_condattr_destroy(&attr);
  return UV__ERR(err);
}

#endif /* defined(__APPLE__) && defined(__MACH__) */

void uv_cond_destroy(uv_cond_t* cond) {
#if defined(__APPLE__) && defined(__MACH__)
  /* It has been reported that destroying condition variables that have been
   * signalled but not waited on can sometimes result in application crashes.
   * See https://codereview.chromium.org/1323293005.
   */
  pthread_mutex_t mutex;
  struct timespec ts;
  int err;

  if (pthread_mutex_init(&mutex, NULL))
    abort();

  if (pthread_mutex_lock(&mutex))
    abort();

  ts.tv_sec = 0;
  ts.tv_nsec = 1;

  err = pthread_cond_timedwait_relative_np(cond, &mutex, &ts);
  if (err != 0 && err != ETIMEDOUT)
    abort();

  if (pthread_mutex_unlock(&mutex))
    abort();

  if (pthread_mutex_destroy(&mutex))
    abort();
#endif /* defined(__APPLE__) && defined(__MACH__) */

  if (pthread_cond_destroy(cond))
    abort();
}

void uv_cond_signal(uv_cond_t* cond) {
  if (pthread_cond_signal(cond))
    abort();
}

void uv_cond_broadcast(uv_cond_t* cond) {
  if (pthread_cond_broadcast(cond))
    abort();
}

#if defined(__APPLE__) && defined(__MACH__)

void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
  int r;

  errno = 0;
  r = pthread_cond_wait(cond, mutex);

  /* Workaround for a bug in OS X at least up to 13.6
   * See https://github.com/libuv/libuv/issues/4165
   */
  if (r == EINVAL)
    if (errno == EBUSY)
      return;

  if (r)
    abort();
}

#else /* !(defined(__APPLE__) && defined(__MACH__)) */

void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
  if (pthread_cond_wait(cond, mutex))
    abort();
}

#endif

int uv_cond_timedwait(uv_cond_t* cond, uv_mutex_t* mutex, uint64_t timeout) {
  int r;
  struct timespec ts;
#if defined(__MVS__)
  struct timeval tv;
#endif

#if defined(__APPLE__) && defined(__MACH__)
  ts.tv_sec = timeout / NANOSEC;
  ts.tv_nsec = timeout % NANOSEC;
  r = pthread_cond_timedwait_relative_np(cond, mutex, &ts);
#else
#if defined(__MVS__)
  if (gettimeofday(&tv, NULL))
    abort();
  timeout += tv.tv_sec * NANOSEC + tv.tv_usec * 1e3;
#else
  timeout += uv__hrtime(UV_CLOCK_PRECISE);
#endif
  ts.tv_sec = timeout / NANOSEC;
  ts.tv_nsec = timeout % NANOSEC;
  r = pthread_cond_timedwait(cond, mutex, &ts);
#endif


  if (r == 0)
    return 0;

  if (r == ETIMEDOUT)
    return UV_ETIMEDOUT;

  abort();
#ifndef __SUNPRO_C
  return UV_EINVAL;  /* Satisfy the compiler. */
#endif
}


int uv_key_create(uv_key_t* key) {
  return UV__ERR(pthread_key_create(key, NULL));
}


void uv_key_delete(uv_key_t* key) {
  if (pthread_key_delete(*key))
    abort();
}


void* uv_key_get(uv_key_t* key) {
  return pthread_getspecific(*key);
}


void uv_key_set(uv_key_t* key, void* value) {
  if (pthread_setspecific(*key, value))
    abort();
}

#if defined(_AIX) || defined(__MVS__) || defined(__PASE__)
int uv__thread_setname(const char* name) {
  return UV_ENOSYS;
}
#elif defined(__APPLE__)
int uv__thread_setname(const char* name) {
  char namebuf[UV_PTHREAD_MAX_NAMELEN_NP];
  strncpy(namebuf, name, sizeof(namebuf) - 1);
  namebuf[sizeof(namebuf) - 1] = '\0';
  int err = pthread_setname_np(namebuf);
  if (err)
    return UV__ERR(errno);
  return 0;
}
#elif defined(__NetBSD__)
int uv__thread_setname(const char* name) {
  char namebuf[UV_PTHREAD_MAX_NAMELEN_NP];
  strncpy(namebuf, name, sizeof(namebuf) - 1);
  namebuf[sizeof(namebuf) - 1] = '\0';
  return UV__ERR(pthread_setname_np(pthread_self(), "%s", namebuf));
}
#elif defined(__OpenBSD__)
int uv__thread_setname(const char* name) {
  char namebuf[UV_PTHREAD_MAX_NAMELEN_NP];
  strncpy(namebuf, name, sizeof(namebuf) - 1);
  namebuf[sizeof(namebuf) - 1] = '\0';
  pthread_set_name_np(pthread_self(), namebuf);
  return 0;
}
#else
int uv__thread_setname(const char* name) {
  char namebuf[UV_PTHREAD_MAX_NAMELEN_NP];
  strncpy(namebuf, name, sizeof(namebuf) - 1);
  namebuf[sizeof(namebuf) - 1] = '\0';
  return UV__ERR(pthread_setname_np(pthread_self(), namebuf));
}
#endif

#if (defined(__ANDROID_API__) && __ANDROID_API__ < 26) || \
    defined(_AIX) || \
    defined(__MVS__) || \
    defined(__PASE__)
int uv__thread_getname(uv_thread_t* tid, char* name, size_t size) {
  return UV_ENOSYS;
}
#elif defined(__OpenBSD__)
int uv__thread_getname(uv_thread_t* tid, char* name, size_t size) {
  char thread_name[UV_PTHREAD_MAX_NAMELEN_NP];
  pthread_get_name_np(*tid, thread_name, sizeof(thread_name));
  strncpy(name, thread_name, size - 1);
  name[size - 1] = '\0';
  return 0;
}
#elif defined(__APPLE__)
int uv__thread_getname(uv_thread_t* tid, char* name, size_t size) {
  char thread_name[UV_PTHREAD_MAX_NAMELEN_NP];
  if (pthread_getname_np(*tid, thread_name, sizeof(thread_name)) != 0)
    return UV__ERR(errno);

  strncpy(name, thread_name, size - 1);
  name[size - 1] = '\0';
  return 0;
}
#else
int uv__thread_getname(uv_thread_t* tid, char* name, size_t size) {
  int r;
  char thread_name[UV_PTHREAD_MAX_NAMELEN_NP];
  r = pthread_getname_np(*tid, thread_name, sizeof(thread_name));
  if (r != 0)
    return UV__ERR(r);

  strncpy(name, thread_name, size - 1);
  name[size - 1] = '\0';
  return 0;
}
#endif
