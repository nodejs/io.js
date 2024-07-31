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

#ifdef __APPLE__

#include <sys/ioctl.h>
#include <string.h>

static int read_count;


static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  static char slab[1024];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  fprintf(stdout, "got data %d\n", ++read_count);
  fflush(stdout);

  if (read_count == 3)
    uv_close((uv_handle_t*) stream, NULL);
}


TEST_IMPL(osx_select) {
  int r;
  int fd;
  size_t i;
  size_t len;
  const char* str;
  uv_tty_t tty;

  fd = open("/dev/tty", O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Cannot open /dev/tty as read-only: %s\n", strerror(errno));
    fflush(stderr);
    return TEST_SKIP;
  }

  r = uv_tty_init(uv_default_loop(), &tty, fd, 1);
  ASSERT_OK(r);

  uv_read_start((uv_stream_t*) &tty, alloc_cb, read_cb);

  /* Emulate user-input */
  str = "got some input\n"
        "with a couple of lines\n"
        "feel pretty happy\n";
  for (i = 0, len = strlen(str); i < len; i++) {
    r = ioctl(fd, TIOCSTI, str + i);
    ASSERT_OK(r);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(3, read_count);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(osx_select_many_fds) {
  int r;
  int fd;
  size_t i;
  size_t len;
  const char* str;
  struct sockaddr_in addr;
  uv_tty_t tty;
  uv_tcp_t tcps[1500];

  TEST_FILE_LIMIT(ARRAY_SIZE(tcps) + 100);

  r = uv_ip4_addr("127.0.0.1", 0, &addr);
  ASSERT_OK(r);

  for (i = 0; i < ARRAY_SIZE(tcps); i++) {
    r = uv_tcp_init(uv_default_loop(), &tcps[i]);
    ASSERT_OK(r);
    r = uv_tcp_bind(&tcps[i], (const struct sockaddr *) &addr, 0);
    ASSERT_OK(r);
    uv_unref((uv_handle_t*) &tcps[i]);
  }

  fd = open("/dev/tty", O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Cannot open /dev/tty as read-only: %s\n", strerror(errno));
    fflush(stderr);
    return TEST_SKIP;
  }

  r = uv_tty_init(uv_default_loop(), &tty, fd, 1);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &tty, alloc_cb, read_cb);
  ASSERT_OK(r);

  /* Emulate user-input */
  str = "got some input\n"
        "with a couple of lines\n"
        "feel pretty happy\n";
  for (i = 0, len = strlen(str); i < len; i++) {
    r = ioctl(fd, TIOCSTI, str + i);
    ASSERT_OK(r);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(3, read_count);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#endif /* __APPLE__ */
