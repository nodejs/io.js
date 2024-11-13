
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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <shellapi.h>
# include <wchar.h>
  typedef BOOL (WINAPI *sCompareObjectHandles)(_In_ HANDLE, _In_ HANDLE);
# define unlink _unlink
# define putenv _putenv
# define close _close
#else
# include <unistd.h>
# include <sys/wait.h>
#endif


static int close_cb_called;
static int exit_cb_called;
static uv_process_t process;
static uv_timer_t timer;
static uv_process_options_t options;
static char exepath[1024];
static size_t exepath_size = 1024;
static char* args[5];
static int no_term_signal;
static int timer_counter;
static uv_tcp_t tcp_server;

#define OUTPUT_SIZE 1024
static char output[OUTPUT_SIZE];
static int output_used;


static void close_cb(uv_handle_t* handle) {
  printf("close_cb\n");
  close_cb_called++;
}

static void exit_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  printf("exit_cb\n");
  exit_cb_called++;
  ASSERT_EQ(1, exit_status);
  ASSERT_OK(term_signal);
  uv_close((uv_handle_t*) process, close_cb);
}


static void fail_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  ASSERT(0 && "fail_cb called");
}


static void kill_cb(uv_process_t* process,
                    int64_t exit_status,
                    int term_signal) {
  int err;

  printf("exit_cb\n");
  exit_cb_called++;
#ifdef _WIN32
  ASSERT_EQ(1, exit_status);
#else
  ASSERT_OK(exit_status);
#endif
#if defined(__APPLE__) || defined(__MVS__)
  /*
   * At least starting with Darwin Kernel Version 16.4.0, sending a SIGTERM to a
   * process that is still starting up kills it with SIGKILL instead of SIGTERM.
   * See: https://github.com/libuv/libuv/issues/1226
   */
  ASSERT(no_term_signal || term_signal == SIGTERM || term_signal == SIGKILL);
#else
  ASSERT(no_term_signal || term_signal == SIGTERM);
#endif
  uv_close((uv_handle_t*) process, close_cb);

  /*
   * Sending signum == 0 should check if the
   * child process is still alive, not kill it.
   * This process should be dead.
   */
  err = uv_kill(process->pid, 0);
  ASSERT_EQ(err, UV_ESRCH);
}

static void detach_failure_cb(uv_process_t* process,
                              int64_t exit_status,
                              int term_signal) {
  printf("detach_cb\n");
  exit_cb_called++;
}

static void on_alloc(uv_handle_t* handle,
                     size_t suggested_size,
                     uv_buf_t* buf) {
  buf->base = output + output_used;
  buf->len = OUTPUT_SIZE - output_used;
}


static void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  if (nread > 0) {
    output_used += nread;
  } else if (nread < 0) {
    ASSERT_EQ(nread, UV_EOF);
    uv_close((uv_handle_t*) tcp, close_cb);
  }
}


static void on_read_once(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
  uv_read_stop(tcp);
  on_read(tcp, nread, buf);
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
  uv_close((uv_handle_t*) req->handle, close_cb);
}


static void write_null_cb(uv_write_t* req, int status) {
  ASSERT_OK(status);
}


static void init_process_options(char* test, uv_exit_cb exit_cb) {
  /* Note spawn_helper1 defined in test/run-tests.c */
  int r = uv_exepath(exepath, &exepath_size);
  ASSERT_OK(r);
  exepath[exepath_size] = '\0';
  args[0] = exepath;
  args[1] = test;
  args[2] = NULL;
  args[3] = NULL;
  args[4] = NULL;
  options.file = exepath;
  options.args = args;
  options.exit_cb = exit_cb;
  options.flags = 0;
}


static void timer_cb(uv_timer_t* handle) {
  uv_process_kill(&process, SIGTERM);
  uv_close((uv_handle_t*) handle, close_cb);
}


static void timer_counter_cb(uv_timer_t* handle) {
  ++timer_counter;
}


TEST_IMPL(spawn_fails) {
  int r;

  init_process_options("", fail_cb);
  options.file = options.args[0] = "program-that-had-better-not-exist";

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT(r == UV_ENOENT || r == UV_EACCES);
  ASSERT_OK(uv_is_active((uv_handle_t*) &process));
  uv_close((uv_handle_t*) &process, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


#ifndef _WIN32
TEST_IMPL(spawn_fails_check_for_waitpid_cleanup) {
  int r;
  int status;
  int err;

  init_process_options("", fail_cb);
  options.file = options.args[0] = "program-that-had-better-not-exist";

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT(r == UV_ENOENT || r == UV_EACCES);
  ASSERT_OK(uv_is_active((uv_handle_t*) &process));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  /* verify the child is successfully cleaned up within libuv */
  do
    err = waitpid(process.pid, &status, 0);
  while (err == -1 && errno == EINTR);

  ASSERT_EQ(err, -1);
  ASSERT_EQ(errno, ECHILD);

  uv_close((uv_handle_t*) &process, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif


TEST_IMPL(spawn_empty_env) {
  char* env[1];

  /* The autotools dynamic library build requires the presence of
   * DYLD_LIBARY_PATH (macOS) or LD_LIBRARY_PATH/LIBPATH (other Unices)
   * in the environment, but of course that doesn't work with
   * the empty environment that we're testing here.
   */
  if (NULL != getenv("DYLD_LIBRARY_PATH") ||
      NULL != getenv("LD_LIBRARY_PATH") ||
      NULL != getenv("LIBPATH")) {
    RETURN_SKIP("doesn't work with DYLD_LIBRARY_PATH/LD_LIBRARY_PATH/LIBPATH");
  }

  init_process_options("spawn_helper1", exit_cb);
  options.env = env;
  env[0] = NULL;

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_exit_code) {
  int r;

  init_process_options("spawn_helper1", exit_cb);

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_stdout) {
  int r;
  uv_pipe_t out;
  uv_stdio_container_t stdio[2];

  init_process_options("spawn_helper2", exit_cb);

  uv_pipe_init(uv_default_loop(), &out, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio_count = 2;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(2, close_cb_called); /* Once for process once for the pipe. */
  printf("output is: %s", output);
  ASSERT_OK(strcmp("hello world\n", output));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_stdout_to_file) {
  int r;
  uv_file file;
  uv_fs_t fs_req;
  uv_stdio_container_t stdio[2];
  uv_buf_t buf;

  /* Setup. */
  unlink("stdout_file");

  init_process_options("spawn_helper2", exit_cb);

  r = uv_fs_open(NULL, &fs_req, "stdout_file", UV_FS_O_CREAT | UV_FS_O_RDWR,
      S_IRUSR | S_IWUSR, NULL);
  ASSERT_NE(r, -1);
  uv_fs_req_cleanup(&fs_req);

  file = r;

  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_INHERIT_FD;
  options.stdio[1].data.fd = file;
  options.stdio_count = 2;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  buf = uv_buf_init(output, sizeof(output));
  r = uv_fs_read(NULL, &fs_req, file, &buf, 1, 0, NULL);
  ASSERT_EQ(12, r);
  uv_fs_req_cleanup(&fs_req);

  r = uv_fs_close(NULL, &fs_req, file, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&fs_req);

  printf("output is: %s", output);
  ASSERT_OK(strcmp("hello world\n", output));

  /* Cleanup. */
  unlink("stdout_file");

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_stdout_and_stderr_to_file) {
  int r;
  uv_file file;
  uv_fs_t fs_req;
  uv_stdio_container_t stdio[3];
  uv_buf_t buf;

  /* Setup. */
  unlink("stdout_file");

  init_process_options("spawn_helper6", exit_cb);

  r = uv_fs_open(NULL, &fs_req, "stdout_file", UV_FS_O_CREAT | UV_FS_O_RDWR,
      S_IRUSR | S_IWUSR, NULL);
  ASSERT_NE(r, -1);
  uv_fs_req_cleanup(&fs_req);

  file = r;

  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_INHERIT_FD;
  options.stdio[1].data.fd = file;
  options.stdio[2].flags = UV_INHERIT_FD;
  options.stdio[2].data.fd = file;
  options.stdio_count = 3;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  buf = uv_buf_init(output, sizeof(output));
  r = uv_fs_read(NULL, &fs_req, file, &buf, 1, 0, NULL);
  ASSERT_EQ(27, r);
  uv_fs_req_cleanup(&fs_req);

  r = uv_fs_close(NULL, &fs_req, file, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&fs_req);

  printf("output is: %s", output);
  ASSERT_OK(strcmp("hello world\nhello errworld\n", output));

  /* Cleanup. */
  unlink("stdout_file");

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_stdout_and_stderr_to_file2) {
#ifndef _WIN32
  int r;
  uv_file file;
  uv_fs_t fs_req;
  uv_stdio_container_t stdio[3];
  uv_buf_t buf;

  /* Setup. */
  unlink("stdout_file");

  init_process_options("spawn_helper6", exit_cb);

  /* Replace stderr with our file */
  r = uv_fs_open(NULL,
                 &fs_req,
                 "stdout_file",
                 O_CREAT | O_RDWR,
                 S_IRUSR | S_IWUSR,
                 NULL);
  ASSERT_NE(r, -1);
  uv_fs_req_cleanup(&fs_req);
  file = dup2(r, STDERR_FILENO);
  ASSERT_NE(file, -1);

  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_INHERIT_FD;
  options.stdio[1].data.fd = file;
  options.stdio[2].flags = UV_INHERIT_FD;
  options.stdio[2].data.fd = file;
  options.stdio_count = 3;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  buf = uv_buf_init(output, sizeof(output));
  r = uv_fs_read(NULL, &fs_req, file, &buf, 1, 0, NULL);
  ASSERT_EQ(27, r);
  uv_fs_req_cleanup(&fs_req);

  r = uv_fs_close(NULL, &fs_req, file, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&fs_req);

  printf("output is: %s", output);
  ASSERT_OK(strcmp("hello world\nhello errworld\n", output));

  /* Cleanup. */
  unlink("stdout_file");

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#else
  RETURN_SKIP("Unix only test");
#endif
}


TEST_IMPL(spawn_stdout_and_stderr_to_file_swap) {
#ifndef _WIN32
  int r;
  uv_file stdout_file;
  uv_file stderr_file;
  uv_fs_t fs_req;
  uv_stdio_container_t stdio[3];
  uv_buf_t buf;

  /* Setup. */
  unlink("stdout_file");
  unlink("stderr_file");

  init_process_options("spawn_helper6", exit_cb);

  /* open 'stdout_file' and replace STDOUT_FILENO with it */
  r = uv_fs_open(NULL,
                 &fs_req,
                 "stdout_file",
                 O_CREAT | O_RDWR,
                 S_IRUSR | S_IWUSR,
                 NULL);
  ASSERT_NE(r, -1);
  uv_fs_req_cleanup(&fs_req);
  stdout_file = dup2(r, STDOUT_FILENO);
  ASSERT_NE(stdout_file, -1);

  /* open 'stderr_file' and replace STDERR_FILENO with it */
  r = uv_fs_open(NULL, &fs_req, "stderr_file", O_CREAT | O_RDWR,
      S_IRUSR | S_IWUSR, NULL);
  ASSERT_NE(r, -1);
  uv_fs_req_cleanup(&fs_req);
  stderr_file = dup2(r, STDERR_FILENO);
  ASSERT_NE(stderr_file, -1);

  /* now we're going to swap them: the child process' stdout will be our
   * stderr_file and vice versa */
  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_INHERIT_FD;
  options.stdio[1].data.fd = stderr_file;
  options.stdio[2].flags = UV_INHERIT_FD;
  options.stdio[2].data.fd = stdout_file;
  options.stdio_count = 3;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  buf = uv_buf_init(output, sizeof(output));

  /* check the content of stdout_file */
  r = uv_fs_read(NULL, &fs_req, stdout_file, &buf, 1, 0, NULL);
  ASSERT_GE(r, 15);
  uv_fs_req_cleanup(&fs_req);

  r = uv_fs_close(NULL, &fs_req, stdout_file, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&fs_req);

  printf("output is: %s", output);
  ASSERT_OK(strncmp("hello errworld\n", output, 15));

  /* check the content of stderr_file */
  r = uv_fs_read(NULL, &fs_req, stderr_file, &buf, 1, 0, NULL);
  ASSERT_GE(r, 12);
  uv_fs_req_cleanup(&fs_req);

  r = uv_fs_close(NULL, &fs_req, stderr_file, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&fs_req);

  printf("output is: %s", output);
  ASSERT_OK(strncmp("hello world\n", output, 12));

  /* Cleanup. */
  unlink("stdout_file");
  unlink("stderr_file");

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#else
  RETURN_SKIP("Unix only test");
#endif
}


TEST_IMPL(spawn_stdin) {
  int r;
  uv_pipe_t out;
  uv_pipe_t in;
  uv_write_t write_req;
  uv_buf_t buf;
  uv_stdio_container_t stdio[2];
  char buffer[] = "hello-from-spawn_stdin";

  init_process_options("spawn_helper3", exit_cb);

  uv_pipe_init(uv_default_loop(), &out, 0);
  uv_pipe_init(uv_default_loop(), &in, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
  options.stdio[0].data.stream = (uv_stream_t*) &in;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio_count = 2;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  buf.base = buffer;
  buf.len = sizeof(buffer);
  r = uv_write(&write_req, (uv_stream_t*) &in, &buf, 1, write_cb);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(3, close_cb_called); /* Once for process twice for the pipe. */
  ASSERT_OK(strcmp(buffer, output));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_stdio_greater_than_3) {
  int r;
  uv_pipe_t pipe;
  uv_stdio_container_t stdio[4];

  init_process_options("spawn_helper5", exit_cb);

  uv_pipe_init(uv_default_loop(), &pipe, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_IGNORE;
  options.stdio[2].flags = UV_IGNORE;
  options.stdio[3].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[3].data.stream = (uv_stream_t*) &pipe;
  options.stdio_count = 4;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &pipe, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(2, close_cb_called); /* Once for process once for the pipe. */
  printf("output from stdio[3] is: %s", output);
  ASSERT_OK(strcmp("fourth stdio!\n", output));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


int spawn_tcp_server_helper(void) {
  uv_tcp_t tcp;
  uv_os_sock_t handle;
  int r;

  r = uv_tcp_init(uv_default_loop(), &tcp);
  ASSERT_OK(r);

#ifdef _WIN32
  handle = _get_osfhandle(3);
#else
  handle = 3;
#endif
  r = uv_tcp_open(&tcp, handle);
  ASSERT_OK(r);

  /* Make sure that we can listen on a socket that was
   * passed down from the parent process
   */
  r = uv_listen((uv_stream_t*) &tcp, SOMAXCONN, NULL);
  ASSERT_OK(r);

  return 1;
}


TEST_IMPL(spawn_tcp_server) {
  uv_stdio_container_t stdio[4];
  struct sockaddr_in addr;
  int fd;
  int r;
#ifdef _WIN32
  uv_os_fd_t handle;
#endif

  init_process_options("spawn_tcp_server_helper", exit_cb);

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  fd = -1;
  r = uv_tcp_init_ex(uv_default_loop(), &tcp_server, AF_INET);
  ASSERT_OK(r);
  r = uv_tcp_bind(&tcp_server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);
#ifdef _WIN32
  r = uv_fileno((uv_handle_t*) &tcp_server, &handle);
  fd = _open_osfhandle((intptr_t) handle, 0);
#else
  r = uv_fileno((uv_handle_t*) &tcp_server, &fd);
 #endif
  ASSERT_OK(r);
  ASSERT_GT(fd, 0);

  options.stdio = stdio;
  options.stdio[0].flags = UV_INHERIT_FD;
  options.stdio[0].data.fd = 0;
  options.stdio[1].flags = UV_INHERIT_FD;
  options.stdio[1].data.fd = 1;
  options.stdio[2].flags = UV_INHERIT_FD;
  options.stdio[2].data.fd = 2;
  options.stdio[3].flags = UV_INHERIT_FD;
  options.stdio[3].data.fd = fd;
  options.stdio_count = 4;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_ignored_stdio) {
  int r;

  init_process_options("spawn_helper6", exit_cb);

  options.stdio = NULL;
  options.stdio_count = 0;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_and_kill) {
  int r;

  init_process_options("spawn_helper4", kill_cb);

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  r = uv_timer_start(&timer, timer_cb, 500, 0);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(2, close_cb_called); /* Once for process and once for timer. */

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_preserve_env) {
  int r;
  uv_pipe_t out;
  uv_stdio_container_t stdio[2];

  init_process_options("spawn_helper7", exit_cb);

  uv_pipe_init(uv_default_loop(), &out, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio_count = 2;

  r = putenv("ENV_TEST=testval");
  ASSERT_OK(r);

  /* Explicitly set options.env to NULL to test for env clobbering. */
  options.env = NULL;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(2, close_cb_called);

  printf("output is: %s", output);
  ASSERT_OK(strcmp("testval", output));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_detached) {
  int r;

  init_process_options("spawn_helper4", detach_failure_cb);

  options.flags |= UV_PROCESS_DETACHED;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  uv_unref((uv_handle_t*) &process);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_OK(exit_cb_called);

  ASSERT_EQ(process.pid, uv_process_get_pid(&process));

  r = uv_kill(process.pid, 0);
  ASSERT_OK(r);

  r = uv_kill(process.pid, SIGTERM);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(spawn_and_kill_with_std) {
  int r;
  uv_pipe_t in, out, err;
  uv_write_t write;
  char message[] = "Nancy's joining me because the message this evening is "
                   "not my message but ours.";
  uv_buf_t buf;
  uv_stdio_container_t stdio[3];

  init_process_options("spawn_helper4", kill_cb);

  r = uv_pipe_init(uv_default_loop(), &in, 0);
  ASSERT_OK(r);

  r = uv_pipe_init(uv_default_loop(), &out, 0);
  ASSERT_OK(r);

  r = uv_pipe_init(uv_default_loop(), &err, 0);
  ASSERT_OK(r);

  options.stdio = stdio;
  options.stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
  options.stdio[0].data.stream = (uv_stream_t*) &in;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[2].data.stream = (uv_stream_t*) &err;
  options.stdio_count = 3;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  buf = uv_buf_init(message, sizeof message);
  r = uv_write(&write, (uv_stream_t*) &in, &buf, 1, write_cb);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &err, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  r = uv_timer_start(&timer, timer_cb, 500, 0);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(5, close_cb_called); /* process x 1, timer x 1, stdio x 3. */

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_and_ping) {
  uv_write_t write_req;
  uv_pipe_t in, out;
  uv_buf_t buf;
  uv_stdio_container_t stdio[2];
  int r;

  init_process_options("spawn_helper3", exit_cb);
  buf = uv_buf_init("TEST", 4);

  uv_pipe_init(uv_default_loop(), &out, 0);
  uv_pipe_init(uv_default_loop(), &in, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
  options.stdio[0].data.stream = (uv_stream_t*) &in;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio_count = 2;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  /* Sending signum == 0 should check if the
   * child process is still alive, not kill it.
   */
  r = uv_process_kill(&process, 0);
  ASSERT_OK(r);

  r = uv_write(&write_req, (uv_stream_t*) &in, &buf, 1, write_cb);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  ASSERT_OK(exit_cb_called);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_OK(strcmp(output, "TEST"));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_same_stdout_stderr) {
  uv_write_t write_req;
  uv_pipe_t in, out;
  uv_buf_t buf;
  uv_stdio_container_t stdio[3];
  int r;

  init_process_options("spawn_helper3", exit_cb);
  buf = uv_buf_init("TEST", 4);

  uv_pipe_init(uv_default_loop(), &out, 0);
  uv_pipe_init(uv_default_loop(), &in, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
  options.stdio[0].data.stream = (uv_stream_t*) &in;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio_count = 2;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  /* Sending signum == 0 should check if the
   * child process is still alive, not kill it.
   */
  r = uv_process_kill(&process, 0);
  ASSERT_OK(r);

  r = uv_write(&write_req, (uv_stream_t*) &in, &buf, 1, write_cb);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  ASSERT_OK(exit_cb_called);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_OK(strcmp(output, "TEST"));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_closed_process_io) {
  uv_pipe_t in;
  uv_write_t write_req;
  uv_buf_t buf;
  uv_stdio_container_t stdio[2];
  static char buffer[] = "hello-from-spawn_stdin\n";

  init_process_options("spawn_helper3", exit_cb);

  uv_pipe_init(uv_default_loop(), &in, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
  options.stdio[0].data.stream = (uv_stream_t*) &in;
  options.stdio_count = 1;

  close(0); /* Close process stdin. */

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));

  buf = uv_buf_init(buffer, sizeof(buffer));
  ASSERT_OK(uv_write(&write_req, (uv_stream_t*) &in, &buf, 1, write_cb));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(2, close_cb_called); /* process, child stdin */

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(kill) {
  int r;

#ifdef _WIN32
  no_term_signal = 1;
#endif

  init_process_options("spawn_helper4", kill_cb);

  /* Verify that uv_spawn() resets the signal disposition. */
#ifndef _WIN32
  {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    ASSERT_OK(pthread_sigmask(SIG_BLOCK, &set, NULL));
  }
  ASSERT_PTR_NE(SIG_ERR, signal(SIGTERM, SIG_IGN));
#endif

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

#ifndef _WIN32
  {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    ASSERT_OK(pthread_sigmask(SIG_UNBLOCK, &set, NULL));
  }
  ASSERT_PTR_NE(SIG_ERR, signal(SIGTERM, SIG_DFL));
#endif

  /* Sending signum == 0 should check if the
   * child process is still alive, not kill it.
   */
  r = uv_kill(process.pid, 0);
  ASSERT_OK(r);

  /* Kill the process. */
  r = uv_kill(process.pid, SIGTERM);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


#ifdef _WIN32
TEST_IMPL(spawn_detect_pipe_name_collisions_on_windows) {
  int r;
  uv_pipe_t out;
  char name[64];
  HANDLE pipe_handle;
  uv_stdio_container_t stdio[2];

  init_process_options("spawn_helper2", exit_cb);

  uv_pipe_init(uv_default_loop(), &out, 0);
  options.stdio = stdio;
  options.stdio[0].flags = UV_IGNORE;
  options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  options.stdio[1].data.stream = (uv_stream_t*) &out;
  options.stdio_count = 2;

  /* Create a pipe that'll cause a collision. */
  snprintf(name,
           sizeof(name),
           "\\\\.\\pipe\\uv\\%p-%lu",
           &out,
           GetCurrentProcessId());
  pipe_handle = CreateNamedPipeA(name,
                                PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                10,
                                65536,
                                65536,
                                0,
                                NULL);
  ASSERT_PTR_NE(pipe_handle, INVALID_HANDLE_VALUE);

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &out, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(2, close_cb_called); /* Once for process once for the pipe. */
  printf("output is: %s", output);
  ASSERT_OK(strcmp("hello world\n", output));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


#if !defined(USING_UV_SHARED)
int make_program_args(char** args, int verbatim_arguments, WCHAR** dst_ptr);
WCHAR* quote_cmd_arg(const WCHAR *source, WCHAR *target);

TEST_IMPL(argument_escaping) {
  const WCHAR* test_str[] = {
    L"",
    L"HelloWorld",
    L"Hello World",
    L"Hello\"World",
    L"Hello World\\",
    L"Hello\\\"World",
    L"Hello\\World",
    L"Hello\\\\World",
    L"Hello World\\",
    L"c:\\path\\to\\node.exe --eval \"require('c:\\\\path\\\\to\\\\test.js')\""
  };
  const int count = sizeof(test_str) / sizeof(*test_str);
  WCHAR** test_output;
  WCHAR* command_line;
  WCHAR** cracked;
  size_t total_size = 0;
  int i;
  int num_args;
  int result;

  char* verbatim[] = {
    "cmd.exe",
    "/c",
    "c:\\path\\to\\node.exe --eval \"require('c:\\\\path\\\\to\\\\test.js')\"",
    NULL
  };
  WCHAR* verbatim_output;
  WCHAR* non_verbatim_output;

  test_output = calloc(count, sizeof(WCHAR*));
  ASSERT_NOT_NULL(test_output);
  for (i = 0; i < count; ++i) {
    test_output[i] = calloc(2 * (wcslen(test_str[i]) + 2), sizeof(WCHAR));
    quote_cmd_arg(test_str[i], test_output[i]);
    wprintf(L"input : %s\n", test_str[i]);
    wprintf(L"output: %s\n", test_output[i]);
    total_size += wcslen(test_output[i]) + 1;
  }
  command_line = calloc(total_size + 1, sizeof(WCHAR));
  ASSERT_NOT_NULL(command_line);
  for (i = 0; i < count; ++i) {
    wcscat(command_line, test_output[i]);
    wcscat(command_line, L" ");
  }
  command_line[total_size - 1] = L'\0';

  wprintf(L"command_line: %s\n", command_line);

  cracked = CommandLineToArgvW(command_line, &num_args);
  for (i = 0; i < num_args; ++i) {
    wprintf(L"%d: %s\t%s\n", i, test_str[i], cracked[i]);
    ASSERT_OK(wcscmp(test_str[i], cracked[i]));
  }

  LocalFree(cracked);
  for (i = 0; i < count; ++i) {
    free(test_output[i]);
  }
  free(test_output);

  result = make_program_args(verbatim, 1, &verbatim_output);
  ASSERT_OK(result);
  result = make_program_args(verbatim, 0, &non_verbatim_output);
  ASSERT_OK(result);

  wprintf(L"    verbatim_output: %s\n", verbatim_output);
  wprintf(L"non_verbatim_output: %s\n", non_verbatim_output);

  ASSERT_OK(wcscmp(verbatim_output,
                   L"cmd.exe /c c:\\path\\to\\node.exe --eval "
                   L"\"require('c:\\\\path\\\\to\\\\test.js')\""));
  ASSERT_OK(wcscmp(non_verbatim_output,
                   L"cmd.exe /c \"c:\\path\\to\\node.exe --eval "
                   L"\\\"require('c:\\\\path\\\\to\\\\test.js')\\\"\""));

  free(verbatim_output);
  free(non_verbatim_output);

  return 0;
}

int make_program_env(char** env_block, WCHAR** dst_ptr);

TEST_IMPL(environment_creation) {
  size_t i;
  char* environment[] = {
    "FOO=BAR",
    "SYSTEM=ROOT", /* substring of a supplied var name */
    "SYSTEMROOTED=OMG", /* supplied var name is a substring */
    "TEMP=C:\\Temp",
    "INVALID",
    "BAZ=QUX",
    "B_Z=QUX",
    "B\xe2\x82\xacZ=QUX",
    "B\xf0\x90\x80\x82Z=QUX",
    "B\xef\xbd\xa1Z=QUX",
    "B\xf0\xa3\x91\x96Z=QUX",
    "BAZ", /* repeat, invalid variable */
    NULL
  };
  WCHAR* wenvironment[] = {
    L"BAZ=QUX",
    L"B_Z=QUX",
    L"B\x20acZ=QUX",
    L"B\xd800\xdc02Z=QUX",
    L"B\xd84d\xdc56Z=QUX",
    L"B\xff61Z=QUX",
    L"FOO=BAR",
    L"SYSTEM=ROOT", /* substring of a supplied var name */
    L"SYSTEMROOTED=OMG", /* supplied var name is a substring */
    L"TEMP=C:\\Temp",
  };
  WCHAR* from_env[] = {
    /* list should be kept in sync with list
     * in process.c, minus variables in wenvironment */
    L"HOMEDRIVE",
    L"HOMEPATH",
    L"LOGONSERVER",
    L"PATH",
    L"USERDOMAIN",
    L"USERNAME",
    L"USERPROFILE",
    L"SYSTEMDRIVE",
    L"SYSTEMROOT",
    L"WINDIR",
    /* test for behavior in the absence of a
     * required-environment variable: */
    L"ZTHIS_ENV_VARIABLE_DOES_NOT_EXIST",
  };
  int found_in_loc_env[ARRAY_SIZE(wenvironment)] = {0};
  int found_in_usr_env[ARRAY_SIZE(from_env)] = {0};
  WCHAR *expected[ARRAY_SIZE(from_env)];
  int result;
  WCHAR* str;
  WCHAR* prev;
  WCHAR* env;

  for (i = 0; i < ARRAY_SIZE(from_env); i++) {
      /* copy expected additions to environment locally */
      size_t len = GetEnvironmentVariableW(from_env[i], NULL, 0);
      if (len == 0) {
        found_in_usr_env[i] = 1;
        str = malloc(1 * sizeof(WCHAR));
        *str = 0;
        expected[i] = str;
      } else {
        size_t name_len = wcslen(from_env[i]);
        str = malloc((name_len+1+len) * sizeof(WCHAR));
        wmemcpy(str, from_env[i], name_len);
        expected[i] = str;
        str += name_len;
        *str++ = L'=';
        GetEnvironmentVariableW(from_env[i], str, len);
     }
  }

  result = make_program_env(environment, &env);
  ASSERT_OK(result);

  for (str = env, prev = NULL; *str; prev = str, str += wcslen(str) + 1) {
    int found = 0;
#if 0
    _cputws(str);
    putchar('\n');
#endif
    for (i = 0; i < ARRAY_SIZE(wenvironment) && !found; i++) {
      if (!wcscmp(str, wenvironment[i])) {
        ASSERT(!found_in_loc_env[i]);
        found_in_loc_env[i] = 1;
        found = 1;
      }
    }
    for (i = 0; i < ARRAY_SIZE(expected) && !found; i++) {
      if (!wcscmp(str, expected[i])) {
        ASSERT(!found_in_usr_env[i]);
        found_in_usr_env[i] = 1;
        found = 1;
      }
    }
    if (prev) { /* verify sort order */
#if !defined(__MINGW32__) || defined(__MINGW64_VERSION_MAJOR)
      ASSERT_EQ(1, CompareStringOrdinal(prev, -1, str, -1, TRUE));
#endif
    }
    ASSERT(found); /* verify that we expected this variable */
  }

  /* verify that we found all expected variables */
  for (i = 0; i < ARRAY_SIZE(wenvironment); i++) {
    ASSERT(found_in_loc_env[i]);
  }
  for (i = 0; i < ARRAY_SIZE(expected); i++) {
    ASSERT(found_in_usr_env[i]);
  }

  return 0;
}
#endif

/* Regression test for issue #909 */
TEST_IMPL(spawn_with_an_odd_path) {
  int r;

  char newpath[2048];
  char *path = getenv("PATH");
  ASSERT_NOT_NULL(path);
  snprintf(newpath, 2048, ";.;%s", path);
  SetEnvironmentVariable("PATH", newpath);

  init_process_options("", exit_cb);
  options.file = options.args[0] = "program-that-had-better-not-exist";
  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT(r == UV_ENOENT || r == UV_EACCES);
  ASSERT_OK(uv_is_active((uv_handle_t*) &process));
  uv_close((uv_handle_t*) &process, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_no_path) {
  char* env[1];
  WCHAR* old_path = NULL;
  DWORD old_path_len;

  if ((old_path_len = GetEnvironmentVariableW(L"PATH", NULL, 0)) > 0) {
    old_path = malloc(old_path_len * sizeof(WCHAR));
    GetEnvironmentVariableW(L"PATH", old_path, old_path_len);
    SetEnvironmentVariableW(L"PATH", NULL);
  }

  init_process_options("spawn_helper1", exit_cb);
  options.env = env;
  env[0] = NULL;

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  SetEnvironmentVariableW(L"PATH", old_path);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_no_ext) {
  char new_exepath[1024];

  init_process_options("spawn_helper1", exit_cb);
  options.flags |= UV_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME;
  snprintf(new_exepath, sizeof(new_exepath), "%.*s_no_ext",
           (int) (exepath_size - sizeof(".exe") + 1),
           exepath);
  options.file = options.args[0] = new_exepath;

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_path_no_ext) {
  int r;
  int len;
  int file_len;
  char file[64];
  char path[1024];
  char* env[2];

  /* Set up the process, but make sure that the file to run is relative and
   * requires a lookup into PATH. */
  init_process_options("spawn_helper1", exit_cb);
  options.flags |= UV_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME;

  /* Set up the PATH env variable */
  for (len = strlen(exepath), file_len = 0;
       exepath[len - 1] != '/' && exepath[len - 1] != '\\';
       len--, file_len++);
  snprintf(file, sizeof(file), "%.*s_no_ext",
           (int) (file_len - sizeof(".exe") + 1),
           exepath + len);
  exepath[len] = 0;
  snprintf(path, sizeof(path), "PATH=%s", exepath);

  env[0] = path;
  env[1] = NULL;

  options.file = options.args[0] = file;
  options.env = env;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT(r == UV_ENOENT || r == UV_EACCES);
  ASSERT_OK(uv_is_active((uv_handle_t*) &process));
  uv_close((uv_handle_t*) &process, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif

#ifndef _WIN32
TEST_IMPL(spawn_setuid_setgid) {
  int r;
  struct passwd* pw;
  char uidstr[10];
  char gidstr[10];

  /* if not root, then this will fail. */
  uv_uid_t uid = getuid();
  if (uid != 0) {
    RETURN_SKIP("It should be run as root user");
  }

  init_process_options("spawn_helper_setuid_setgid", exit_cb);

  /* become the "nobody" user. */
  pw = getpwnam("nobody");
  ASSERT_NOT_NULL(pw);
  options.uid = pw->pw_uid;
  options.gid = pw->pw_gid;
  snprintf(uidstr, sizeof(uidstr), "%d", pw->pw_uid);
  snprintf(gidstr, sizeof(gidstr), "%d", pw->pw_gid);
  options.args[2] = uidstr;
  options.args[3] = gidstr;
  options.flags = UV_PROCESS_SETUID | UV_PROCESS_SETGID;

  r = uv_spawn(uv_default_loop(), &process, &options);
  if (r == UV_EACCES)
    RETURN_SKIP("user 'nobody' cannot access the test runner");

  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif


#ifndef _WIN32
TEST_IMPL(spawn_setuid_fails) {
  int r;

  /* if root, become nobody. */
  /* On IBMi PASE, there is no nobody user. */
#ifndef __PASE__
  uv_uid_t uid = getuid();
  if (uid == 0) {
    struct passwd* pw;
    pw = getpwnam("nobody");
    ASSERT_NOT_NULL(pw);
    ASSERT_OK(setgid(pw->pw_gid));
    ASSERT_OK(setuid(pw->pw_uid));
  }
#endif  /* !__PASE__ */

  init_process_options("spawn_helper1", fail_cb);

  options.flags |= UV_PROCESS_SETUID;
  /* On IBMi PASE, there is no root user. User may grant 
   * root-like privileges, including setting uid to 0.
   */
#if defined(__PASE__)
  options.uid = -1;
#else
  options.uid = 0;
#endif

  /* These flags should be ignored on Unices. */
  options.flags |= UV_PROCESS_WINDOWS_HIDE;
  options.flags |= UV_PROCESS_WINDOWS_HIDE_CONSOLE;
  options.flags |= UV_PROCESS_WINDOWS_HIDE_GUI;
  options.flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;

  r = uv_spawn(uv_default_loop(), &process, &options);
#if defined(__CYGWIN__)
  ASSERT_EQ(r, UV_EINVAL);
#else
  ASSERT_EQ(r, UV_EPERM);
#endif

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_OK(close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_setgid_fails) {
  int r;

  /* if root, become nobody. */
  /* On IBMi PASE, there is no nobody user. */
#ifndef __PASE__
  uv_uid_t uid = getuid();
  if (uid == 0) {
    struct passwd* pw;
    pw = getpwnam("nobody");
    ASSERT_NOT_NULL(pw);
    ASSERT_OK(setgid(pw->pw_gid));
    ASSERT_OK(setuid(pw->pw_uid));
  }
#endif  /* !__PASE__ */

  init_process_options("spawn_helper1", fail_cb);

  options.flags |= UV_PROCESS_SETGID;
  /* On IBMi PASE, there is no root user. User may grant 
   * root-like privileges, including setting gid to 0.
   */
#if defined(__MVS__) || defined(__PASE__)
  options.gid = -1;
#else
  options.gid = 0;
#endif

  r = uv_spawn(uv_default_loop(), &process, &options);
#if defined(__CYGWIN__) || defined(__MVS__)
  ASSERT_EQ(r, UV_EINVAL);
#else
  ASSERT_EQ(r, UV_EPERM);
#endif

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_OK(close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif


#ifdef _WIN32

static void exit_cb_unexpected(uv_process_t* process,
                               int64_t exit_status,
                               int term_signal) {
  ASSERT(0 && "should not have been called");
}


TEST_IMPL(spawn_setuid_fails) {
  int r;

  init_process_options("spawn_helper1", exit_cb_unexpected);

  options.flags |= UV_PROCESS_SETUID;
  options.uid = (uv_uid_t) -42424242;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_EQ(r, UV_ENOTSUP);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_OK(close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_setgid_fails) {
  int r;

  init_process_options("spawn_helper1", exit_cb_unexpected);

  options.flags |= UV_PROCESS_SETGID;
  options.gid = (uv_gid_t) -42424242;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_EQ(r, UV_ENOTSUP);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_OK(close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif


TEST_IMPL(spawn_auto_unref) {
  init_process_options("spawn_helper1", NULL);
  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_OK(uv_is_closing((uv_handle_t*) &process));
  uv_close((uv_handle_t*) &process, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, uv_is_closing((uv_handle_t*) &process));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_fs_open) {
  int r;
  uv_os_fd_t fd;
  uv_os_fd_t dup_fd;
  uv_fs_t fs_req;
  uv_pipe_t in;
  uv_write_t write_req;
  uv_write_t write_req2;
  uv_buf_t buf;
  uv_stdio_container_t stdio[1];
#ifdef _WIN32
  const char dev_null[] = "NUL";
  HMODULE kernelbase_module;
  sCompareObjectHandles pCompareObjectHandles; /* function introduced in Windows 10 */
#else
  const char dev_null[] = "/dev/null";
#endif

  r = uv_fs_open(NULL, &fs_req, dev_null, UV_FS_O_RDWR, 0, NULL);
  ASSERT_NE(r, -1);
  fd = uv_get_osfhandle((uv_file) fs_req.result);
  uv_fs_req_cleanup(&fs_req);

  init_process_options("spawn_helper8", exit_cb);

  ASSERT_OK(uv_pipe_init(uv_default_loop(), &in, 0));

  options.stdio = stdio;
  options.stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
  options.stdio[0].data.stream = (uv_stream_t*) &in;
  options.stdio_count = 1;

  /* make an inheritable copy */
#ifdef _WIN32
  ASSERT_NE(0, DuplicateHandle(GetCurrentProcess(), fd, GetCurrentProcess(), &dup_fd,
                               0, /* inherit */ TRUE, DUPLICATE_SAME_ACCESS));
  kernelbase_module = GetModuleHandleA("kernelbase.dll");
  pCompareObjectHandles = (sCompareObjectHandles)
      GetProcAddress(kernelbase_module, "CompareObjectHandles");
  ASSERT_NE(pCompareObjectHandles == NULL ||
            pCompareObjectHandles(fd, dup_fd),
            0);
#else
  dup_fd = dup(fd);
#endif

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));

  buf = uv_buf_init((char*) &fd, sizeof(fd));
  ASSERT_OK(uv_write(&write_req,
                     (uv_stream_t*) &in,
                     &buf,
                     1,
                     write_null_cb));

  buf = uv_buf_init((char*) &dup_fd, sizeof(fd));
  ASSERT_OK(uv_write(&write_req2, (uv_stream_t*) &in, &buf, 1, write_cb));

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_OK(uv_fs_close(NULL, &fs_req, r, NULL));

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(2, close_cb_called);  /* One for `in`, one for process */

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(closed_fd_events) {
  uv_stdio_container_t stdio[3];
  uv_pipe_t pipe_handle;
  uv_fs_t req;
  uv_buf_t bufs[1];
  uv_file fd[2];
  bufs[0] = uv_buf_init("", 1);

  /* create a pipe and share it with a child process */
  ASSERT_OK(uv_pipe(fd, 0, 0));
  ASSERT_GT(fd[0], 2);
  ASSERT_GT(fd[1], 2);

  /* spawn_helper4 blocks indefinitely. */
  init_process_options("spawn_helper4", exit_cb);
  options.stdio_count = 3;
  options.stdio = stdio;
  options.stdio[0].flags = UV_INHERIT_FD;
  options.stdio[0].data.fd = fd[0];
  options.stdio[1].flags = UV_IGNORE;
  options.stdio[2].flags = UV_IGNORE;

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));
  uv_unref((uv_handle_t*) &process);

  /* read from the pipe with uv */
  ASSERT_OK(uv_pipe_init(uv_default_loop(), &pipe_handle, 0));
  ASSERT_OK(uv_pipe_open(&pipe_handle, fd[0]));
  /* uv_pipe_open() takes ownership of the file descriptor. */
  fd[0] = -1;

  ASSERT_OK(uv_read_start((uv_stream_t*) &pipe_handle,
                          on_alloc,
                          on_read_once));

  ASSERT_EQ(1, uv_fs_write(NULL, &req, fd[1], bufs, 1, -1, NULL));
  ASSERT_EQ(1, req.result);
  uv_fs_req_cleanup(&req);

  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_ONCE));

  /* should have received just one byte */
  ASSERT_EQ(1, output_used);

  /* close the pipe and see if we still get events */
  uv_close((uv_handle_t*) &pipe_handle, close_cb);

  ASSERT_EQ(1, uv_fs_write(NULL, &req, fd[1], bufs, 1, -1, NULL));
  ASSERT_EQ(1, req.result);
  uv_fs_req_cleanup(&req);

  ASSERT_OK(uv_timer_init(uv_default_loop(), &timer));
  ASSERT_OK(uv_timer_start(&timer, timer_counter_cb, 10, 0));

  /* see if any spurious events interrupt the timer */
  if (1 == uv_run(uv_default_loop(), UV_RUN_ONCE))
    /* have to run again to really trigger the timer */
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_ONCE));

  ASSERT_EQ(1, timer_counter);

  /* cleanup */
  ASSERT_OK(uv_process_kill(&process, SIGTERM));
#ifdef _WIN32
  ASSERT_OK(_close(fd[1]));
#else
  ASSERT_OK(close(fd[1]));
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(spawn_reads_child_path) {
  int r;
  int len;
  char file[64];
  char path[1024];
  char* env[3];

  /* Need to carry over the dynamic linker path when the test runner is
   * linked against libuv.so, see https://github.com/libuv/libuv/issues/85.
   */
#if defined(__APPLE__)
  static const char dyld_path_var[] = "DYLD_LIBRARY_PATH";
#elif defined(__MVS__) || defined(__PASE__)
  static const char dyld_path_var[] = "LIBPATH";
#else
  static const char dyld_path_var[] = "LD_LIBRARY_PATH";
#endif

  /* Set up the process, but make sure that the file to run is relative and
   * requires a lookup into PATH. */
  init_process_options("spawn_helper1", exit_cb);

  /* Set up the PATH env variable */
  for (len = strlen(exepath);
       exepath[len - 1] != '/' && exepath[len - 1] != '\\';
       len--);
  strcpy(file, exepath + len);
  exepath[len] = 0;
  strcpy(path, "PATH=");
  strcpy(path + 5, exepath);
#if defined(__CYGWIN__) || defined(__MSYS__)
  /* Carry over the dynamic linker path in case the test runner
     is linked against cyguv-1.dll or msys-uv-1.dll, see above.  */
  {
    char* syspath = getenv("PATH");
    if (syspath != NULL) {
      strcat(path, ":");
      strcat(path, syspath);
    }
  }
#endif

  env[0] = path;
  env[1] = getenv(dyld_path_var);
  env[2] = NULL;

  if (env[1] != NULL) {
    static char buf[1024 + sizeof(dyld_path_var)];
    snprintf(buf, sizeof(buf), "%s=%s", dyld_path_var, env[1]);
    env[1] = buf;
  }

  options.file = file;
  options.args[0] = file;
  options.env = env;

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(spawn_inherit_streams) {
  uv_process_t child_req;
  uv_stdio_container_t child_stdio[2];
  int fds_stdin[2];
  int fds_stdout[2];
  uv_pipe_t pipe_stdin_child;
  uv_pipe_t pipe_stdout_child;
  uv_pipe_t pipe_stdin_parent;
  uv_pipe_t pipe_stdout_parent;
  unsigned char ubuf[OUTPUT_SIZE - 1];
  uv_buf_t buf;
  unsigned int i;
  int r;
  int bidir;
  uv_write_t write_req;
  uv_loop_t* loop;

  init_process_options("spawn_helper9", exit_cb);

  loop = uv_default_loop();
  ASSERT_OK(uv_pipe_init(loop, &pipe_stdin_child, 0));
  ASSERT_OK(uv_pipe_init(loop, &pipe_stdout_child, 0));
  ASSERT_OK(uv_pipe_init(loop, &pipe_stdin_parent, 0));
  ASSERT_OK(uv_pipe_init(loop, &pipe_stdout_parent, 0));

  ASSERT_OK(uv_pipe(fds_stdin, 0, 0));
  ASSERT_OK(uv_pipe(fds_stdout, 0, 0));

  ASSERT_OK(uv_pipe_open(&pipe_stdin_child, fds_stdin[0]));
  ASSERT_OK(uv_pipe_open(&pipe_stdout_child, fds_stdout[1]));
  ASSERT_OK(uv_pipe_open(&pipe_stdin_parent, fds_stdin[1]));
  ASSERT_OK(uv_pipe_open(&pipe_stdout_parent, fds_stdout[0]));
  ASSERT(uv_is_readable((uv_stream_t*) &pipe_stdin_child));
  ASSERT(uv_is_writable((uv_stream_t*) &pipe_stdout_child));
  ASSERT(uv_is_writable((uv_stream_t*) &pipe_stdin_parent));
  ASSERT(uv_is_readable((uv_stream_t*) &pipe_stdout_parent));
  /* Some systems (SVR4) open a bidirectional pipe, most don't. */
  bidir = uv_is_writable((uv_stream_t*) &pipe_stdin_child);
  ASSERT_EQ(uv_is_readable((uv_stream_t*) &pipe_stdout_child), bidir);
  ASSERT_EQ(uv_is_readable((uv_stream_t*) &pipe_stdin_parent), bidir);
  ASSERT_EQ(uv_is_writable((uv_stream_t*) &pipe_stdout_parent), bidir);

  child_stdio[0].flags = UV_INHERIT_STREAM;
  child_stdio[0].data.stream = (uv_stream_t *) &pipe_stdin_child;

  child_stdio[1].flags = UV_INHERIT_STREAM;
  child_stdio[1].data.stream = (uv_stream_t *) &pipe_stdout_child;

  options.stdio = child_stdio;
  options.stdio_count = 2;

  ASSERT_OK(uv_spawn(loop, &child_req, &options));

  uv_close((uv_handle_t*) &pipe_stdin_child, NULL);
  uv_close((uv_handle_t*) &pipe_stdout_child, NULL);

  buf = uv_buf_init((char*) ubuf, sizeof ubuf);
  for (i = 0; i < sizeof ubuf; ++i)
    ubuf[i] = i & 255u;
  memset(output, 0, sizeof ubuf);

  r = uv_write(&write_req,
               (uv_stream_t*) &pipe_stdin_parent,
               &buf,
               1,
               write_cb);
  ASSERT_OK(r);

  r = uv_read_start((uv_stream_t*) &pipe_stdout_parent, on_alloc, on_read);
  ASSERT_OK(r);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(3, close_cb_called);

  r = memcmp(ubuf, output, sizeof ubuf);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

TEST_IMPL(spawn_quoted_path) {
#ifndef _WIN32
  RETURN_SKIP("Test for Windows");
#else
  char* quoted_path_env[2];
  args[0] = "not_existing";
  args[1] = NULL;
  options.file = args[0];
  options.args = args;
  options.exit_cb = exit_cb;
  options.flags = 0;
  /* We test if search_path works correctly with semicolons in quoted path. We
   * will use an invalid drive, so we are sure no executable is spawned. */
  quoted_path_env[0] = "PATH=\"xyz:\\test;\";xyz:\\other";
  quoted_path_env[1] = NULL;
  options.env = quoted_path_env;

  /* We test if libuv will not segfault. */
  uv_spawn(uv_default_loop(), &process, &options);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}

TEST_IMPL(spawn_exercise_sigchld_issue) {
  int r;
  int i;
  uv_process_options_t dummy_options = {0};
  uv_process_t dummy_processes[100];
  char* args[2];

  init_process_options("spawn_helper1", exit_cb);

  r = uv_spawn(uv_default_loop(), &process, &options);
  ASSERT_OK(r);

  // This test exercises a bug in the darwin kernel that causes SIGCHLD not to
  // be delivered sometimes. Calling posix_spawn many times increases the
  // likelihood of encountering this issue, so spin a few times to make this
  // test more reliable.
  dummy_options.file = args[0] = "program-that-had-better-not-exist";
  args[1] = NULL;
  dummy_options.args = args;
  dummy_options.exit_cb = fail_cb;
  dummy_options.flags = 0;
  for (i = 0; i < 100; i++) {
    r = uv_spawn(uv_default_loop(), &dummy_processes[i], &dummy_options);
    if (r != UV_ENOENT)
      ASSERT_EQ(r, UV_EACCES);
    uv_close((uv_handle_t*) &dummy_processes[i], close_cb);
  }

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(101, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

/* Helper for child process of spawn_inherit_streams */
#ifndef _WIN32
void spawn_stdin_stdout(void) {
  char buf[1024];
  char* pbuf;
  for (;;) {
    ssize_t r, w, c;
    do {
      r = read(0, buf, sizeof buf);
    } while (r == -1 && errno == EINTR);
    if (r == 0) {
      return;
    }
    ASSERT_GT(r, 0);
    c = r;
    pbuf = buf;
    while (c) {
      do {
        w = write(1, pbuf, (size_t)c);
      } while (w == -1 && errno == EINTR);
      ASSERT_GE(w, 0);
      pbuf = pbuf + w;
      c = c - w;
    }
  }
}
#else
void spawn_stdin_stdout(void) {
  char buf[1024];
  char* pbuf;
  HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
  ASSERT_PTR_NE(h_stdin, INVALID_HANDLE_VALUE);
  ASSERT_PTR_NE(h_stdout, INVALID_HANDLE_VALUE);
  for (;;) {
    DWORD n_read;
    DWORD n_written;
    DWORD to_write;
    if (!ReadFile(h_stdin, buf, sizeof buf, &n_read, NULL)) {
      ASSERT_EQ(GetLastError(), ERROR_BROKEN_PIPE);
      return;
    }
    to_write = n_read;
    pbuf = buf;
    while (to_write) {
      ASSERT(WriteFile(h_stdout, pbuf, to_write, &n_written, NULL));
      to_write -= n_written;
      pbuf += n_written;
    }
  }
}
#endif /* !_WIN32 */

TEST_IMPL(spawn_relative_path) {
  char* sep;

  init_process_options("spawn_helper1", exit_cb);

  exepath_size = sizeof(exepath) - 2;
  ASSERT_OK(uv_exepath(exepath, &exepath_size));
  exepath[exepath_size] = '\0';

  /* Poor man's basename(3). */
  sep = strrchr(exepath, '/');
  if (sep == NULL)
    sep = strrchr(exepath, '\\');
  ASSERT_NOT_NULL(sep);

  /* Split into dirname and basename and make basename relative. */
  memmove(sep + 2, sep, 1 + strlen(sep));
  sep[0] = '\0';
  sep[1] = '.';
  sep[2] = '/';

  options.cwd = exepath;
  options.file = options.args[0] = sep + 1;

  ASSERT_OK(uv_spawn(uv_default_loop(), &process, &options));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  ASSERT_EQ(1, exit_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
