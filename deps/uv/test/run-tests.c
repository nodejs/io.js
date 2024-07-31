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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
# include <io.h>
# define read _read
#else
# include <unistd.h>
#endif

#include "uv.h"
#include "runner.h"
#include "task.h"

/* Actual tests and helpers are defined in test-list.h */
#include "test-list.h"

#ifdef __MVS__
#include "zos-base.h"
/* Initialize environment and zoslib */
__attribute__((constructor)) void init() {
  zoslib_config_t config;
  init_zoslib_config(&config);
  init_zoslib(config);
}
#endif

int ipc_helper(int listen_after_write);
int ipc_helper_heavy_traffic_deadlock_bug(void);
int ipc_helper_tcp_connection(void);
int ipc_send_recv_helper(void);
int ipc_helper_bind_twice(void);
int ipc_helper_send_zero(void);
int stdio_over_pipes_helper(void);
void spawn_stdin_stdout(void);
void process_title_big_argv(void);
int spawn_tcp_server_helper(void);

static int maybe_run_test(int argc, char **argv);

#ifdef _WIN32
typedef BOOL (WINAPI *sCompareObjectHandles)(_In_ HANDLE, _In_ HANDLE);
#endif


int main(int argc, char **argv) {
#ifndef _WIN32
  if (0 == geteuid() && NULL == getenv("UV_RUN_AS_ROOT")) {
    fprintf(stderr, "The libuv test suite cannot be run as root.\n");
    return EXIT_FAILURE;
  }
#endif

  platform_init(argc, argv);
  argv = uv_setup_args(argc, argv);

  switch (argc) {
  case 1: return run_tests(0);
  case 2: return maybe_run_test(argc, argv);
  case 3: return run_test_part(argv[1], argv[2]);
  case 4: return maybe_run_test(argc, argv);
  default:
    fprintf(stderr, "Too many arguments.\n");
    fflush(stderr);
    return EXIT_FAILURE;
  }
}


static int maybe_run_test(int argc, char **argv) {
  if (strcmp(argv[1], "--list") == 0) {
    print_tests(stdout);
    return 0;
  }

  if (strcmp(argv[1], "ipc_helper_listen_before_write") == 0) {
    return ipc_helper(0);
  }

  if (strcmp(argv[1], "ipc_helper_listen_after_write") == 0) {
    return ipc_helper(1);
  }

  if (strcmp(argv[1], "ipc_helper_heavy_traffic_deadlock_bug") == 0) {
    return ipc_helper_heavy_traffic_deadlock_bug();
  }

  if (strcmp(argv[1], "ipc_send_recv_helper") == 0) {
    return ipc_send_recv_helper();
  }

  if (strcmp(argv[1], "ipc_helper_tcp_connection") == 0) {
    return ipc_helper_tcp_connection();
  }

  if (strcmp(argv[1], "ipc_helper_bind_twice") == 0) {
    return ipc_helper_bind_twice();
  }

  if (strcmp(argv[1], "ipc_helper_send_zero") == 0) {
    return ipc_helper_send_zero();
  }

  if (strcmp(argv[1], "stdio_over_pipes_helper") == 0) {
    return stdio_over_pipes_helper();
  }

  if (strcmp(argv[1], "spawn_helper1") == 0) {
    notify_parent_process();
    return 1;
  }

  if (strcmp(argv[1], "spawn_helper2") == 0) {
    notify_parent_process();
    printf("hello world\n");
    return 1;
  }

  if (strcmp(argv[1], "spawn_tcp_server_helper") == 0) {
    notify_parent_process();
    return spawn_tcp_server_helper();
  }

  if (strcmp(argv[1], "spawn_helper3") == 0) {
    char buffer[256];
    notify_parent_process();
    ASSERT_PTR_EQ(buffer, fgets(buffer, sizeof(buffer) - 1, stdin));
    buffer[sizeof(buffer) - 1] = '\0';
    fputs(buffer, stdout);
    return 1;
  }

  if (strcmp(argv[1], "spawn_helper4") == 0) {
    notify_parent_process();
    /* Never surrender, never return! */
    for (;;) uv_sleep(10000);
  }

  if (strcmp(argv[1], "spawn_helper5") == 0) {
    const char out[] = "fourth stdio!\n";
    notify_parent_process();
    {
#ifdef _WIN32
      DWORD bytes;
      WriteFile((HANDLE) _get_osfhandle(3), out, sizeof(out) - 1, &bytes, NULL);
#else
      ssize_t r;

      do
        r = write(3, out, sizeof(out) - 1);
      while (r == -1 && errno == EINTR);

      fsync(3);
#endif
    }
    return 1;
  }

  if (strcmp(argv[1], "spawn_helper6") == 0) {
    int r;

    notify_parent_process();

    r = fprintf(stdout, "hello world\n");
    ASSERT_GT(r, 0);

    r = fprintf(stderr, "hello errworld\n");
    ASSERT_GT(r, 0);

    return 1;
  }

  if (strcmp(argv[1], "spawn_helper7") == 0) {
    int r;
    char *test;

    notify_parent_process();

    /* Test if the test value from the parent is still set */
    test = getenv("ENV_TEST");
    ASSERT_NOT_NULL(test);

    r = fprintf(stdout, "%s", test);
    ASSERT_GT(r, 0);

    return 1;
  }

  if (strcmp(argv[1], "spawn_helper8") == 0) {
    uv_os_fd_t closed_fd;
    uv_os_fd_t open_fd;
#ifdef _WIN32
    DWORD flags;
    HMODULE kernelbase_module;
    sCompareObjectHandles pCompareObjectHandles; /* function introduced in Windows 10 */
#endif
    notify_parent_process();
    ASSERT_EQ(sizeof(closed_fd), read(0, &closed_fd, sizeof(closed_fd)));
    ASSERT_EQ(sizeof(open_fd), read(0, &open_fd, sizeof(open_fd)));
#ifdef _WIN32
    ASSERT_GT((intptr_t) closed_fd, 0);
    ASSERT_GT((intptr_t) open_fd, 0);
    ASSERT_NE(0, GetHandleInformation(open_fd, &flags));
    kernelbase_module = GetModuleHandleA("kernelbase.dll");
    pCompareObjectHandles = (sCompareObjectHandles)
        GetProcAddress(kernelbase_module, "CompareObjectHandles");
    ASSERT_NE(pCompareObjectHandles == NULL || \
              !pCompareObjectHandles(open_fd, closed_fd), 0);
#else
    ASSERT_GT(open_fd, 2);
    ASSERT_GT(closed_fd, 2);
# if defined(__PASE__)  /* On IBMi PASE, write() returns 1 */
    ASSERT_EQ(1, write(closed_fd, "x", 1));
# else
    ASSERT_EQ(-1, write(closed_fd, "x", 1));
# endif  /* !__PASE__ */
#endif
    return 1;
  }

  if (strcmp(argv[1], "spawn_helper9") == 0) {
    notify_parent_process();
    spawn_stdin_stdout();
    return 1;
  }

#ifndef _WIN32
  if (strcmp(argv[1], "spawn_helper_setuid_setgid") == 0) {
    uv_uid_t uid = atoi(argv[2]);
    uv_gid_t gid = atoi(argv[3]);

    ASSERT_EQ(uid, getuid());
    ASSERT_EQ(gid, getgid());
    notify_parent_process();

    return 1;
  }
#endif  /* !_WIN32 */

  if (strcmp(argv[1], "process_title_big_argv_helper") == 0) {
    notify_parent_process();
    process_title_big_argv();
    return 0;
  }

  return run_test(argv[1], 0, 1);
}
