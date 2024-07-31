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

#if defined(_WIN32) && !defined(USING_UV_SHARED)

#include "uv.h"
#include "task.h"

#include "../src/win/fs-fd-hash-inl.h"


#define HASH_MAX 1000000000
#define HASH_INC (1000 * UV__FD_HASH_SIZE + 2)
#define BUCKET_MAX (UV__FD_HASH_SIZE * UV__FD_HASH_GROUP_SIZE * 10)
#define BUCKET_INC UV__FD_HASH_SIZE
#define FD_DIFF 9


void assert_nonexistent(int fd) {
  struct uv__fd_info_s info = { 0 };
  ASSERT(!uv__fd_hash_get(fd, &info));
  ASSERT(!uv__fd_hash_remove(fd, &info));
}

void assert_existent(int fd) {
  struct uv__fd_info_s info = { 0 };
  ASSERT(uv__fd_hash_get(fd, &info));
  ASSERT_EQ(info.flags, fd + FD_DIFF);
}

void assert_insertion(int fd) {
  struct uv__fd_info_s info = { 0 };
  assert_nonexistent(fd);
  info.flags = fd + FD_DIFF;
  uv__fd_hash_add(fd, &info);
  assert_existent(fd);
}

void assert_removal(int fd) {
  struct uv__fd_info_s info = { 0 };
  assert_existent(fd);
  uv__fd_hash_remove(fd, &info);
  ASSERT_EQ(info.flags, fd + FD_DIFF);
  assert_nonexistent(fd);
}


/* Run a function for a set of values up to a very high number */
#define RUN_HASH(function)                                                   \
  do {                                                                       \
    for (fd = 0; fd < HASH_MAX; fd += HASH_INC) {                            \
      function(fd);                                                          \
    }                                                                        \
  } while (0)

/* Run a function for a set of values that will cause many collisions */
#define RUN_COLLISIONS(function)                                             \
  do {                                                                       \
    for (fd = 1; fd < BUCKET_MAX; fd += BUCKET_INC) {                        \
      function(fd);                                                          \
    }                                                                        \
  } while (0)


TEST_IMPL(fs_fd_hash) {
  int fd;

  uv__fd_hash_init();

  /* Empty table */
  RUN_HASH(assert_nonexistent);
  RUN_COLLISIONS(assert_nonexistent);

  /* Fill up */
  RUN_HASH(assert_insertion);
  RUN_COLLISIONS(assert_insertion);

  /* Full */
  RUN_HASH(assert_existent);
  RUN_COLLISIONS(assert_existent);

  /* Update */
  {
    struct uv__fd_info_s info = { 0 };
    info.flags = FD_DIFF + FD_DIFF;
    uv__fd_hash_add(0, &info);
  }
  {
    struct uv__fd_info_s info = { 0 };
    ASSERT(uv__fd_hash_get(0, &info));
    ASSERT_EQ(info.flags, FD_DIFF + FD_DIFF);
  }
  {
    /* Leave as it was, will be again tested below */
    struct uv__fd_info_s info = { 0 };
    info.flags = FD_DIFF;
    uv__fd_hash_add(0, &info);
  }

  /* Remove all */
  RUN_HASH(assert_removal);
  RUN_COLLISIONS(assert_removal);

  /* Empty table */
  RUN_HASH(assert_nonexistent);
  RUN_COLLISIONS(assert_nonexistent);
  
  return 0;
}

#else

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif  /* ifndef _WIN32 */
