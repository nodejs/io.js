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

#if !defined(_WIN32)

#include "uv.h"
#include "task.h"

#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>

static void connection_cb(uv_stream_t* server_handle, int status);
static void connect_cb(uv_connect_t* req, int status);

static const int maxfd = 31;
static unsigned connect_cb_called;
static uv_tcp_t server_handle;
static uv_tcp_t client_handle;


TEST_IMPL(emfile) {
  struct sockaddr_in addr;
  struct rlimit limits;
  uv_connect_t connect_req;
  uv_loop_t* loop;
  int first_fd;
#if defined(_AIX) || defined(__MVS__)
  /* On AIX, if a 'accept' call fails ECONNRESET is set on the socket
   * which causes uv__emfile_trick to not work as intended and this test
   * to fail.
   */
  RETURN_SKIP("uv__emfile_trick does not work on this OS");
#endif

  /* Lower the file descriptor limit and use up all fds save one. */
  limits.rlim_cur = limits.rlim_max = maxfd + 1;
  if (setrlimit(RLIMIT_NOFILE, &limits)) {
    ASSERT_EQ(errno, EPERM);  /* Valgrind blocks the setrlimit() call. */
    RETURN_SKIP("setrlimit(RLIMIT_NOFILE) failed, running under valgrind?");
  }

  loop = uv_default_loop();
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &server_handle));
  ASSERT_OK(uv_tcp_init(loop, &client_handle));
  ASSERT_OK(uv_tcp_bind(&server_handle, (const struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server_handle, 8, connection_cb));

  /* Remember the first one so we can clean up afterwards. */
  do
    first_fd = dup(0);
  while (first_fd == -1 && errno == EINTR);
  ASSERT_GT(first_fd, 0);

  while (dup(0) != -1 || errno == EINTR);
  ASSERT_EQ(errno, EMFILE);
  close(maxfd);

  /* Now connect and use up the last available file descriptor.  The EMFILE
   * handling logic in src/unix/stream.c should ensure that connect_cb() runs
   * whereas connection_cb() should *not* run.
   */
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client_handle,
                           (const struct sockaddr*) &addr,
                           connect_cb));
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, connect_cb_called);

  /* Close the dups again. Ignore errors in the unlikely event that the
   * file descriptors were not contiguous.
   */
  while (first_fd < maxfd) {
    close(first_fd);
    first_fd += 1;
  }

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


static void connection_cb(uv_stream_t* server_handle, int status) {
  ASSERT(0 && "connection_cb should not be called.");
}


static void connect_cb(uv_connect_t* req, int status) {
  /* |status| should equal 0 because the connection should have been accepted,
   * it's just that the server immediately closes it again.
   */
  ASSERT_OK(status);
  connect_cb_called += 1;
  uv_close((uv_handle_t*) &server_handle, NULL);
  uv_close((uv_handle_t*) &client_handle, NULL);
}

#else

typedef int file_has_no_tests; /* ISO C forbids an empty translation unit. */

#endif /* !_WIN32 */
