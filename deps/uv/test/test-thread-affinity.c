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

#include <string.h>

#ifndef NO_CPU_AFFINITY

static void check_affinity(void* arg) {
  int r;
  char* cpumask;
  int cpumasksize;
  uv_thread_t tid;

  cpumask = (char*)arg;
  cpumasksize = uv_cpumask_size();
  ASSERT_GT(cpumasksize, 0);
  tid = uv_thread_self();
  r = uv_thread_setaffinity(&tid, cpumask, NULL, cpumasksize);
  ASSERT_OK(r);
  r = uv_thread_setaffinity(&tid, cpumask + cpumasksize, cpumask, cpumasksize);
  ASSERT_OK(r);
}


TEST_IMPL(thread_affinity) {
  int t1first;
  int t1second;
  int t2first;
  int t2second;
  int cpumasksize;
  char* cpumask;
  int ncpus;
  int r;
  int c;
  int i;
  uv_thread_t threads[3];

#ifdef _WIN32
  /* uv_thread_self isn't defined for the main thread on Windows */
  threads[0] = GetCurrentThread();
#else
  threads[0] = uv_thread_self();
#endif
  cpumasksize = uv_cpumask_size();
  ASSERT_GT(cpumasksize, 0);

  cpumask = calloc(4 * cpumasksize, 1);
  ASSERT(cpumask);

  r = uv_thread_getaffinity(&threads[0], cpumask, cpumasksize);
  ASSERT_OK(r);
  ASSERT(cpumask[0] && "test must be run with cpu 0 affinity");
  ncpus = 0;
  while (cpumask[++ncpus]) { }
  memset(cpumask, 0, 4 * cpumasksize);

  t1first = cpumasksize * 0;
  t1second = cpumasksize * 1;
  t2first = cpumasksize * 2;
  t2second = cpumasksize * 3;

  cpumask[t1second + 0] = 1;
  cpumask[t2first + 0] = 1;
  cpumask[t1first + (ncpus >= 2)] = 1;
  cpumask[t2second + (ncpus >= 2)] = 1;
#ifdef __linux__
  cpumask[t1second + 2] = 1;
  cpumask[t2first + 2] = 1;
  cpumask[t1first + 3] = 1;
  cpumask[t2second + 3] = 1;
#else
  if (ncpus >= 3) {
    cpumask[t1second + 2] = 1;
    cpumask[t2first + 2] = 1;
  }
  if (ncpus >= 4) {
    cpumask[t1first + 3] = 1;
    cpumask[t2second + 3] = 1;
  }
#endif

  ASSERT_OK(uv_thread_create(threads + 1,
                             check_affinity,
                             &cpumask[t1first]));
  ASSERT_OK(uv_thread_create(threads + 2,
                             check_affinity,
                             &cpumask[t2first]));
  ASSERT_OK(uv_thread_join(threads + 1));
  ASSERT_OK(uv_thread_join(threads + 2));

  ASSERT(cpumask[t1first + 0] == (ncpus == 1));
  ASSERT(cpumask[t1first + 1] == (ncpus >= 2));
  ASSERT_OK(cpumask[t1first + 2]);
  ASSERT(cpumask[t1first + 3] == (ncpus >= 4));

  ASSERT_EQ(1, cpumask[t2first + 0]);
  ASSERT_OK(cpumask[t2first + 1]);
  ASSERT(cpumask[t2first + 2] == (ncpus >= 3));
  ASSERT_OK(cpumask[t2first + 3]);

  c = uv_thread_getcpu();
  ASSERT_GE(c, 0);

  memset(cpumask, 0, cpumasksize);
  cpumask[c] = 1;
  r = uv_thread_setaffinity(&threads[0], cpumask, NULL, cpumasksize);
  ASSERT_OK(r);

  memset(cpumask, 0, cpumasksize);
  r = uv_thread_getaffinity(&threads[0], cpumask, cpumasksize);
  ASSERT_OK(r);
  for (i = 0; i < cpumasksize; i++) {
    if (i == c)
      ASSERT_EQ(1, cpumask[i]);
    else
      ASSERT_OK(cpumask[i]);
  }

  free(cpumask);

  return 0;
}

#else

TEST_IMPL(thread_affinity) {
  int cpumasksize;
  cpumasksize = uv_cpumask_size();
  ASSERT_EQ(cpumasksize, UV_ENOTSUP);
  return 0;
}

#endif
