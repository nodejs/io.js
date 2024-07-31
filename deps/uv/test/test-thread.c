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
#include <string.h> /* memset */

#ifdef __POSIX__
#include <pthread.h>
#endif

struct getaddrinfo_req {
  uv_thread_t thread_id;
  unsigned int counter;
  uv_loop_t* loop;
  uv_getaddrinfo_t handle;
};


struct fs_req {
  uv_thread_t thread_id;
  unsigned int counter;
  uv_loop_t* loop;
  uv_fs_t handle;
};


struct test_thread {
  uv_thread_t thread_id;
  int thread_called;
};

static void getaddrinfo_do(struct getaddrinfo_req* req);
static void getaddrinfo_cb(uv_getaddrinfo_t* handle,
                           int status,
                           struct addrinfo* res);
static void fs_do(struct fs_req* req);
static void fs_cb(uv_fs_t* handle);

static int thread_called;
static uv_key_t tls_key;


static void getaddrinfo_do(struct getaddrinfo_req* req) {
  int r;

  r = uv_getaddrinfo(req->loop,
                     &req->handle,
                     getaddrinfo_cb,
                     "localhost",
                     NULL,
                     NULL);
  ASSERT_OK(r);
}


static void getaddrinfo_cb(uv_getaddrinfo_t* handle,
                           int status,
                           struct addrinfo* res) {
  struct getaddrinfo_req* req;

  ASSERT_OK(status);

  req = container_of(handle, struct getaddrinfo_req, handle);
  uv_freeaddrinfo(res);

  if (--req->counter)
    getaddrinfo_do(req);
}


static void fs_do(struct fs_req* req) {
  int r;

  r = uv_fs_stat(req->loop, &req->handle, ".", fs_cb);
  ASSERT_OK(r);
}


static void fs_cb(uv_fs_t* handle) {
  struct fs_req* req = container_of(handle, struct fs_req, handle);

  uv_fs_req_cleanup(handle);

  if (--req->counter)
    fs_do(req);
}


static void do_work(void* arg) {
  struct getaddrinfo_req getaddrinfo_reqs[4];
  struct fs_req fs_reqs[4];
  uv_loop_t loop;
  size_t i;
  struct test_thread* thread = arg;

  ASSERT_OK(uv_loop_init(&loop));

  for (i = 0; i < ARRAY_SIZE(getaddrinfo_reqs); i++) {
    struct getaddrinfo_req* req = getaddrinfo_reqs + i;
    req->counter = 4;
    req->loop = &loop;
    getaddrinfo_do(req);
  }

  for (i = 0; i < ARRAY_SIZE(fs_reqs); i++) {
    struct fs_req* req = fs_reqs + i;
    req->counter = 4;
    req->loop = &loop;
    fs_do(req);
  }

  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_OK(uv_loop_close(&loop));
  thread->thread_called = 1;
}


static void thread_entry(void* arg) {
  ASSERT_PTR_EQ(arg, (void *) 42);
  thread_called++;
}


TEST_IMPL(thread_create) {
  uv_thread_t tid;
  int r;

  r = uv_thread_create(&tid, thread_entry, (void *) 42);
  ASSERT_OK(r);

  r = uv_thread_join(&tid);
  ASSERT_OK(r);

  ASSERT_EQ(1, thread_called);

  return 0;
}


/* Hilariously bad test name. Run a lot of tasks in the thread pool and verify
 * that each "finished" callback is run in its originating thread.
 */
TEST_IMPL(threadpool_multiple_event_loops) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif
  
  struct test_thread threads[8];
  size_t i;
  int r;

  memset(threads, 0, sizeof(threads));

  for (i = 0; i < ARRAY_SIZE(threads); i++) {
    r = uv_thread_create(&threads[i].thread_id, do_work, &threads[i]);
    ASSERT_OK(r);
  }

  for (i = 0; i < ARRAY_SIZE(threads); i++) {
    r = uv_thread_join(&threads[i].thread_id);
    ASSERT_OK(r);
    ASSERT_EQ(1, threads[i].thread_called);
  }

  return 0;
}


static void tls_thread(void* arg) {
  ASSERT_NULL(uv_key_get(&tls_key));
  uv_key_set(&tls_key, arg);
  ASSERT_PTR_EQ(arg, uv_key_get(&tls_key));
  uv_key_set(&tls_key, NULL);
  ASSERT_NULL(uv_key_get(&tls_key));
}


TEST_IMPL(thread_local_storage) {
  char name[] = "main";
  uv_thread_t threads[2];
  ASSERT_OK(uv_key_create(&tls_key));
  ASSERT_NULL(uv_key_get(&tls_key));
  uv_key_set(&tls_key, name);
  ASSERT_PTR_EQ(name, uv_key_get(&tls_key));
  ASSERT_OK(uv_thread_create(threads + 0, tls_thread, threads + 0));
  ASSERT_OK(uv_thread_create(threads + 1, tls_thread, threads + 1));
  ASSERT_OK(uv_thread_join(threads + 0));
  ASSERT_OK(uv_thread_join(threads + 1));
  uv_key_delete(&tls_key);
  return 0;
}


static void thread_check_stack(void* arg) {
#if defined(__APPLE__)
  size_t expected;
  expected = arg == NULL ? 0 : ((uv_thread_options_t*)arg)->stack_size;
  /* 512 kB is the default stack size of threads other than the main thread
   * on MacOS. */
  if (expected == 0)
    expected = 512 * 1024;
  ASSERT_GE(pthread_get_stacksize_np(pthread_self()), expected);
#elif defined(__linux__) && defined(__GLIBC__)
  size_t expected;
  struct rlimit lim;
  size_t stack_size;
  pthread_attr_t attr;
  ASSERT_OK(getrlimit(RLIMIT_STACK, &lim));
  if (lim.rlim_cur == RLIM_INFINITY)
    lim.rlim_cur = 2 << 20;  /* glibc default. */
  ASSERT_OK(pthread_getattr_np(pthread_self(), &attr));
  ASSERT_OK(pthread_attr_getstacksize(&attr, &stack_size));
  expected = arg == NULL ? 0 : ((uv_thread_options_t*)arg)->stack_size;
  if (expected == 0)
    expected = (size_t)lim.rlim_cur;
  ASSERT_GE(stack_size, expected);
  ASSERT_OK(pthread_attr_destroy(&attr));
#endif
}


TEST_IMPL(thread_stack_size) {
  uv_thread_t thread;
  ASSERT_OK(uv_thread_create(&thread, thread_check_stack, NULL));
  ASSERT_OK(uv_thread_join(&thread));
  return 0;
}

TEST_IMPL(thread_stack_size_explicit) {
  uv_thread_t thread;
  uv_thread_options_t options;

  options.flags = UV_THREAD_HAS_STACK_SIZE;
  options.stack_size = 1024 * 1024;
  ASSERT_OK(uv_thread_create_ex(&thread, &options,
                                thread_check_stack, &options));
  ASSERT_OK(uv_thread_join(&thread));

  options.stack_size = 8 * 1024 * 1024;  /* larger than most default os sizes */
  ASSERT_OK(uv_thread_create_ex(&thread, &options,
                                thread_check_stack, &options));
  ASSERT_OK(uv_thread_join(&thread));

  options.stack_size = 0;
  ASSERT_OK(uv_thread_create_ex(&thread, &options,
                                thread_check_stack, &options));
  ASSERT_OK(uv_thread_join(&thread));

  options.stack_size = 42;
  ASSERT_OK(uv_thread_create_ex(&thread, &options,
                                thread_check_stack, &options));
  ASSERT_OK(uv_thread_join(&thread));

#ifdef PTHREAD_STACK_MIN
  options.stack_size = PTHREAD_STACK_MIN - 42;  /* unaligned size */
  ASSERT_OK(uv_thread_create_ex(&thread, &options,
                                thread_check_stack, &options));
  ASSERT_OK(uv_thread_join(&thread));

  options.stack_size = PTHREAD_STACK_MIN / 2 - 42;  /* unaligned size */
  ASSERT_OK(uv_thread_create_ex(&thread, &options,
                                thread_check_stack, &options));
  ASSERT_OK(uv_thread_join(&thread));
#endif

  /* unaligned size, should be larger than PTHREAD_STACK_MIN */
  options.stack_size = 1234567;
  ASSERT_OK(uv_thread_create_ex(&thread, &options,
                                thread_check_stack, &options));
  ASSERT_OK(uv_thread_join(&thread));

  return 0;
}
