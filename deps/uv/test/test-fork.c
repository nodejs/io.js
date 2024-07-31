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

/* These tests are Unix only. */
#ifndef _WIN32

#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "uv.h"
#include "task.h"

static int timer_cb_called;
static int socket_cb_called;

static void timer_cb(uv_timer_t* timer) {
  timer_cb_called++;
  uv_close((uv_handle_t*) timer, NULL);
}


static int socket_cb_read_fd;
static int socket_cb_read_size;
static char socket_cb_read_buf[1024];


static void socket_cb(uv_poll_t* poll, int status, int events) {
  ssize_t cnt;
  socket_cb_called++;
  ASSERT_OK(status);
  printf("Socket cb got events %d\n", events);
  ASSERT_EQ(UV_READABLE, (events & UV_READABLE));
  if (socket_cb_read_fd) {
    cnt = read(socket_cb_read_fd, socket_cb_read_buf, socket_cb_read_size);
    ASSERT_EQ(cnt, socket_cb_read_size);
  }
  uv_close((uv_handle_t*) poll, NULL);
}


static void run_timer_loop_once(void) {
  uv_loop_t loop;
  uv_timer_t timer_handle;

  ASSERT_OK(uv_loop_init(&loop));

  timer_cb_called = 0; /* Reset for the child. */

  ASSERT_OK(uv_timer_init(&loop, &timer_handle));
  ASSERT_OK(uv_timer_start(&timer_handle, timer_cb, 1, 0));
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, timer_cb_called);
  ASSERT_OK(uv_loop_close(&loop));
}


static void assert_wait_child(pid_t child_pid) {
  pid_t waited_pid;
  int child_stat;

  waited_pid = waitpid(child_pid, &child_stat, 0);
  printf("Waited pid is %d with status %d\n", waited_pid, child_stat);
  if (waited_pid == -1) {
    perror("Failed to wait");
  }
  ASSERT_EQ(child_pid, waited_pid);
  ASSERT(WIFEXITED(child_stat)); /* Clean exit, not a signal. */
  ASSERT(!WIFSIGNALED(child_stat));
  ASSERT_OK(WEXITSTATUS(child_stat));
}


TEST_IMPL(fork_timer) {
  /* Timers continue to work after we fork. */

  /*
   * Establish the loop before we fork to make sure that it
   * has state to get reset after the fork.
   */
  pid_t child_pid;

  run_timer_loop_once();
#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);

  if (child_pid != 0) {
    /* parent */
    assert_wait_child(child_pid);
  } else {
    /* child */
    ASSERT_OK(uv_loop_fork(uv_default_loop()));
    run_timer_loop_once();
  }

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(fork_socketpair) {
  /* A socket opened in the parent and accept'd in the
     child works after a fork. */
  pid_t child_pid;
  int socket_fds[2];
  uv_poll_t poll_handle;

  /* Prime the loop. */
  run_timer_loop_once();

  ASSERT_OK(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds));

  /* Create the server watcher in the parent, use it in the child. */
  ASSERT_OK(uv_poll_init(uv_default_loop(), &poll_handle, socket_fds[0]));

#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);

  if (child_pid != 0) {
    /* parent */
    ASSERT_EQ(3, send(socket_fds[1], "hi\n", 3, 0));
    assert_wait_child(child_pid);
  } else {
    /* child */
    ASSERT_OK(uv_loop_fork(uv_default_loop()));
    ASSERT_OK(socket_cb_called);
    ASSERT_OK(uv_poll_start(&poll_handle, UV_READABLE, socket_cb));
    printf("Going to run the loop in the child\n");
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
    ASSERT_EQ(1, socket_cb_called);
  }

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(fork_socketpair_started) {
  /* A socket opened in the parent and accept'd in the
     child works after a fork, even if the watcher was already
     started, and then stopped in the parent. */
  pid_t child_pid;
  int socket_fds[2];
  int sync_pipe[2];
  char sync_buf[1];
  uv_poll_t poll_handle;

  ASSERT_OK(pipe(sync_pipe));

  /* Prime the loop. */
  run_timer_loop_once();

  ASSERT_OK(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds));

  /* Create and start the server watcher in the parent, use it in the child. */
  ASSERT_OK(uv_poll_init(uv_default_loop(), &poll_handle, socket_fds[0]));
  ASSERT_OK(uv_poll_start(&poll_handle, UV_READABLE, socket_cb));

  /* Run the loop AFTER the poll watcher is registered to make sure it
     gets passed to the kernel. Use NOWAIT and expect a non-zero
     return to prove the poll watcher is active.
  */
  ASSERT_EQ(1, uv_run(uv_default_loop(), UV_RUN_NOWAIT));

#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);

  if (child_pid != 0) {
    /* parent */
    ASSERT_OK(uv_poll_stop(&poll_handle));
    uv_close((uv_handle_t*)&poll_handle, NULL);
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
    ASSERT_OK(socket_cb_called);
    ASSERT_EQ(1, write(sync_pipe[1], "1", 1)); /* alert child */
    ASSERT_EQ(3, send(socket_fds[1], "hi\n", 3, 0));

    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
    ASSERT_OK(socket_cb_called);

    assert_wait_child(child_pid);
  } else {
    /* child */
    printf("Child is %d\n", getpid());
    ASSERT_EQ(1, read(sync_pipe[0], sync_buf, 1)); /* wait for parent */
    ASSERT_OK(uv_loop_fork(uv_default_loop()));
    ASSERT_OK(socket_cb_called);

    printf("Going to run the loop in the child\n");
    socket_cb_read_fd = socket_fds[0];
    socket_cb_read_size = 3;
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
    ASSERT_EQ(1, socket_cb_called);
    printf("Buf %s\n", socket_cb_read_buf);
    ASSERT_OK(strcmp("hi\n", socket_cb_read_buf));
  }

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static int fork_signal_cb_called;

void fork_signal_to_child_cb(uv_signal_t* handle, int signum)
{
  fork_signal_cb_called = signum;
  uv_close((uv_handle_t*)handle, NULL);
}


TEST_IMPL(fork_signal_to_child) {
  /* A signal handler installed before forking
     is run only in the child when the child is signalled. */
  uv_signal_t signal_handle;
  pid_t child_pid;
  int sync_pipe[2];
  char sync_buf[1];

  fork_signal_cb_called = 0;    /* reset */

  ASSERT_OK(pipe(sync_pipe));

  /* Prime the loop. */
  run_timer_loop_once();

  ASSERT_OK(uv_signal_init(uv_default_loop(), &signal_handle));
  ASSERT_OK(uv_signal_start(&signal_handle,
                            fork_signal_to_child_cb,
                            SIGUSR1));

#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);

  if (child_pid != 0) {
    /* parent */
    ASSERT_EQ(1, read(sync_pipe[0], sync_buf, 1)); /* wait for child */
    ASSERT_OK(kill(child_pid, SIGUSR1));
    /* Run the loop, make sure we don't get the signal. */
    printf("Running loop in parent\n");
    uv_unref((uv_handle_t*)&signal_handle);
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_NOWAIT));
    ASSERT_OK(fork_signal_cb_called);
    printf("Waiting for child in parent\n");
    assert_wait_child(child_pid);
  } else {
    /* child */
    ASSERT_OK(uv_loop_fork(uv_default_loop()));
    ASSERT_EQ(1, write(sync_pipe[1], "1", 1)); /* alert parent */
    /* Get the signal. */
    ASSERT_NE(0, uv_loop_alive(uv_default_loop()));
    printf("Running loop in child\n");
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_ONCE));
    ASSERT_EQ(SIGUSR1, fork_signal_cb_called);
  }

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(fork_signal_to_child_closed) {
  /* A signal handler installed before forking
     doesn't get received anywhere when the child is signalled,
     but isnt running the loop. */
  uv_signal_t signal_handle;
  pid_t child_pid;
  int sync_pipe[2];
  int sync_pipe2[2];
  char sync_buf[1];
  int r;

  fork_signal_cb_called = 0;    /* reset */

  ASSERT_OK(pipe(sync_pipe));
  ASSERT_OK(pipe(sync_pipe2));

  /* Prime the loop. */
  run_timer_loop_once();

  ASSERT_OK(uv_signal_init(uv_default_loop(), &signal_handle));
  ASSERT_OK(uv_signal_start(&signal_handle,
                            fork_signal_to_child_cb,
                            SIGUSR1));

#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);

  if (child_pid != 0) {
    /* parent */
    printf("Wating on child in parent\n");
    ASSERT_EQ(1, read(sync_pipe[0], sync_buf, 1)); /* wait for child */
    printf("Parent killing child\n");
    ASSERT_OK(kill(child_pid, SIGUSR1));
    /* Run the loop, make sure we don't get the signal. */
    printf("Running loop in parent\n");
    uv_unref((uv_handle_t*)&signal_handle); /* so the loop can exit;
                                               we *shouldn't* get any signals */
    run_timer_loop_once(); /* but while we share a pipe, we do, so
                              have something active. */
    ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_ONCE));
    printf("Signal in parent %d\n", fork_signal_cb_called);
    ASSERT_OK(fork_signal_cb_called);
    ASSERT_EQ(1, write(sync_pipe2[1], "1", 1)); /* alert child */
    printf("Waiting for child in parent\n");
    assert_wait_child(child_pid);
  } else {
    /* Child. Our signal handler should still be installed. */
    ASSERT_OK(uv_loop_fork(uv_default_loop()));
    printf("Checking loop in child\n");
    ASSERT_NE(0, uv_loop_alive(uv_default_loop()));
    printf("Alerting parent in child\n");
    ASSERT_EQ(1, write(sync_pipe[1], "1", 1)); /* alert parent */
    /* Don't run the loop. Wait for the parent to call us */
    printf("Waiting on parent in child\n");
    /* Wait for parent. read may fail if the parent tripped an ASSERT
       and exited, so this ASSERT is generous.
    */
    r = read(sync_pipe2[0], sync_buf, 1);
    ASSERT(-1 <= r && r <= 1);
    ASSERT_OK(fork_signal_cb_called);
    printf("Exiting child \n");
    /* Note that we're deliberately not running the loop
     * in the child, and also not closing the loop's handles,
     * so the child default loop can't be cleanly closed.
     * We need to explicitly exit to avoid an automatic failure
     * in that case.
     */
    exit(0);
  }

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

static void fork_signal_cb(uv_signal_t* h, int s) {
  fork_signal_cb_called = s;
}
static void empty_close_cb(uv_handle_t* h){}

TEST_IMPL(fork_close_signal_in_child) {
  uv_loop_t loop;
  uv_signal_t signal_handle;
  pid_t child_pid;

  ASSERT_OK(uv_loop_init(&loop));
  ASSERT_OK(uv_signal_init(&loop, &signal_handle));
  ASSERT_OK(uv_signal_start(&signal_handle, &fork_signal_cb, SIGHUP));

  ASSERT_OK(kill(getpid(), SIGHUP));
  child_pid = fork();
  ASSERT_NE(child_pid, -1);
  ASSERT_OK(fork_signal_cb_called);

  if (!child_pid) {
    uv_loop_fork(&loop);
    uv_close((uv_handle_t*)&signal_handle, &empty_close_cb);
    uv_run(&loop, UV_RUN_DEFAULT);
    /* Child doesn't receive the signal */
    ASSERT_OK(fork_signal_cb_called);
  } else {
    /* Parent. Runing once to receive the signal */
    uv_run(&loop, UV_RUN_ONCE);
    ASSERT_EQ(SIGHUP, fork_signal_cb_called);

    /* loop should stop after closing the only handle */
    uv_close((uv_handle_t*)&signal_handle, &empty_close_cb);
    ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));

    assert_wait_child(child_pid);
  }

  MAKE_VALGRIND_HAPPY(&loop);
  return 0;
}


static void create_file(const char* name) {
  int r;
  uv_file file;
  uv_fs_t req;

  r = uv_fs_open(NULL, &req, name, O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR, NULL);
  ASSERT_GE(r, 0);
  file = r;
  uv_fs_req_cleanup(&req);
  r = uv_fs_close(NULL, &req, file, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&req);
}


static void touch_file(const char* name) {
  int r;
  uv_file file;
  uv_fs_t req;
  uv_buf_t buf;

  r = uv_fs_open(NULL, &req, name, O_RDWR, 0, NULL);
  ASSERT_GE(r, 0);
  file = r;
  uv_fs_req_cleanup(&req);

  buf = uv_buf_init("foo", 4);
  r = uv_fs_write(NULL, &req, file, &buf, 1, -1, NULL);
  ASSERT_GE(r, 0);
  uv_fs_req_cleanup(&req);

  r = uv_fs_close(NULL, &req, file, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&req);
}


static int timer_cb_touch_called;

static void timer_cb_touch(uv_timer_t* timer) {
  uv_close((uv_handle_t*)timer, NULL);
  touch_file("watch_file");
  timer_cb_touch_called++;
}


static int fs_event_cb_called;

static void fs_event_cb_file_current_dir(uv_fs_event_t* handle,
                                         const char* filename,
                                         int events,
                                         int status) {
  ASSERT_OK(fs_event_cb_called);
  ++fs_event_cb_called;
  ASSERT_OK(status);
#if defined(__APPLE__) || defined(__linux__)
  ASSERT_OK(strcmp(filename, "watch_file"));
#else
  ASSERT(filename == NULL || strcmp(filename, "watch_file") == 0);
#endif
  uv_close((uv_handle_t*)handle, NULL);
}


static void assert_watch_file_current_dir(uv_loop_t* const loop, int file_or_dir) {
  uv_timer_t timer;
  uv_fs_event_t fs_event;
  int r;

  /* Setup */
  remove("watch_file");
  create_file("watch_file");

  r = uv_fs_event_init(loop, &fs_event);
  ASSERT_OK(r);
  /* watching a dir is the only way to get fsevents involved on apple
     platforms */
  r = uv_fs_event_start(&fs_event,
                        fs_event_cb_file_current_dir,
                        file_or_dir == 1 ? "." : "watch_file",
                        0);
  ASSERT_OK(r);

  r = uv_timer_init(loop, &timer);
  ASSERT_OK(r);

  r = uv_timer_start(&timer, timer_cb_touch, 100, 0);
  ASSERT_OK(r);

  ASSERT_OK(timer_cb_touch_called);
  ASSERT_OK(fs_event_cb_called);

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(1, timer_cb_touch_called);
  ASSERT_EQ(1, fs_event_cb_called);

  /* Cleanup */
  remove("watch_file");
  fs_event_cb_called = 0;
  timer_cb_touch_called = 0;
  uv_run(loop, UV_RUN_DEFAULT); /* flush pending closes */
}


#define FS_TEST_FILE 0
#define FS_TEST_DIR 1

static int _do_fork_fs_events_child(int file_or_dir) {
  /* basic fsevents work in the child after a fork */
  pid_t child_pid;
  uv_loop_t loop;

  /* Watch in the parent, prime the loop and/or threads. */
  assert_watch_file_current_dir(uv_default_loop(), file_or_dir);
#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);

  if (child_pid != 0) {
    /* parent */
    assert_wait_child(child_pid);
  } else {
    /* child */
    /* Ee can watch in a new loop, but dirs only work
       if we're on linux. */
#if defined(__APPLE__)
    file_or_dir = FS_TEST_FILE;
#endif
    printf("Running child\n");
    uv_loop_init(&loop);
    printf("Child first watch\n");
    assert_watch_file_current_dir(&loop, file_or_dir);
    ASSERT_OK(uv_loop_close(&loop));
    printf("Child second watch default loop\n");
    /* Ee can watch in the default loop. */
    ASSERT_OK(uv_loop_fork(uv_default_loop()));
    /* On some platforms (OS X), if we don't update the time now,
     * the timer cb fires before the event loop enters uv__io_poll,
     * instead of after, meaning we don't see the change! This may be
     * a general race.
     */
    uv_update_time(uv_default_loop());
    assert_watch_file_current_dir(uv_default_loop(), file_or_dir);

    /* We can close the parent loop successfully too. This is
       especially important on Apple platforms where if we're not
       careful trying to touch the CFRunLoop, even just to shut it
       down, that we allocated in the FS_TEST_DIR case would crash. */
    ASSERT_OK(uv_loop_close(uv_default_loop()));

    printf("Exiting child \n");
  }

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;

}


TEST_IMPL(fork_fs_events_child) {
#if defined(NO_FS_EVENTS)
  RETURN_SKIP(NO_FS_EVENTS);
#endif
  return _do_fork_fs_events_child(FS_TEST_FILE);
}


TEST_IMPL(fork_fs_events_child_dir) {
#if defined(NO_FS_EVENTS)
  RETURN_SKIP(NO_FS_EVENTS);
#endif
#if defined(__APPLE__) || defined (__linux__)
  return _do_fork_fs_events_child(FS_TEST_DIR);
#else
  /* You can't spin up a cfrunloop thread on an apple platform
     and then fork. See
     http://objectivistc.tumblr.com/post/16187948939/you-must-exec-a-core-foundation-fork-safety-tale
  */
  return 0;
#endif
}


TEST_IMPL(fork_fs_events_file_parent_child) {
#if defined(NO_FS_EVENTS)
  RETURN_SKIP(NO_FS_EVENTS);
#endif
#if defined(__sun) || defined(_AIX) || defined(__MVS__)
  /* It's not possible to implement this without additional
   * bookkeeping on SunOS. For AIX it is possible, but has to be
   * written. See https://github.com/libuv/libuv/pull/846#issuecomment-287170420
   * TODO: On z/OS, we need to open another message queue and subscribe to the
   * same events as the parent.
   */
  return 0;
#else
  /* Establishing a started fs events watcher in the parent should
     still work in the child. */
  uv_timer_t timer;
  uv_fs_event_t fs_event;
  int r;
  pid_t child_pid;
  uv_loop_t* loop;

  loop = uv_default_loop();

  /* Setup */
  remove("watch_file");
  create_file("watch_file");

  r = uv_fs_event_init(loop, &fs_event);
  ASSERT_OK(r);
  r = uv_fs_event_start(&fs_event,
                        fs_event_cb_file_current_dir,
                        "watch_file",
                        0);
  ASSERT_OK(r);

  r = uv_timer_init(loop, &timer);
  ASSERT_OK(r);

#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);
  if (child_pid != 0) {
    /* parent */
    assert_wait_child(child_pid);
  } else {
    /* child */
    printf("Running child\n");
    ASSERT_OK(uv_loop_fork(loop));

    r = uv_timer_start(&timer, timer_cb_touch, 100, 0);
    ASSERT_OK(r);

    ASSERT_OK(timer_cb_touch_called);
    ASSERT_OK(fs_event_cb_called);
    printf("Running loop in child \n");
    uv_run(loop, UV_RUN_DEFAULT);

    ASSERT_EQ(1, timer_cb_touch_called);
    ASSERT_EQ(1, fs_event_cb_called);

    /* Cleanup */
    remove("watch_file");
    fs_event_cb_called = 0;
    timer_cb_touch_called = 0;
    uv_run(loop, UV_RUN_DEFAULT); /* Flush pending closes. */
  }


  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}


static int work_cb_count;
static int after_work_cb_count;


static void work_cb(uv_work_t* req) {
  work_cb_count++;
}


static void after_work_cb(uv_work_t* req, int status) {
  ASSERT_OK(status);
  after_work_cb_count++;
}


static void assert_run_work(uv_loop_t* const loop) {
  uv_work_t work_req;
  int r;

  ASSERT_OK(work_cb_count);
  ASSERT_OK(after_work_cb_count);
  printf("Queue in %d\n", getpid());
  r = uv_queue_work(loop, &work_req, work_cb, after_work_cb);
  ASSERT_OK(r);
  printf("Running in %d\n", getpid());
  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(1, work_cb_count);
  ASSERT_EQ(1, after_work_cb_count);

  /* cleanup  */
  work_cb_count = 0;
  after_work_cb_count = 0;
}


#ifndef __MVS__
TEST_IMPL(fork_threadpool_queue_work_simple) {
  /* The threadpool works in a child process. */

  pid_t child_pid;
  uv_loop_t loop;

#ifdef __TSAN__
  RETURN_SKIP("ThreadSanitizer doesn't support multi-threaded fork");
#endif

  /* Prime the pool and default loop. */
  assert_run_work(uv_default_loop());

#if defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH)
  child_pid = -1;
#else
  child_pid = fork();
#endif
  ASSERT_NE(child_pid, -1);

  if (child_pid != 0) {
    /* Parent. We can still run work. */
    assert_run_work(uv_default_loop());
    assert_wait_child(child_pid);
  } else {
    /* Child. We can work in a new loop. */
    printf("Running child in %d\n", getpid());
    uv_loop_init(&loop);
    printf("Child first watch\n");
    assert_run_work(&loop);
    uv_loop_close(&loop);
    printf("Child second watch default loop\n");
    /* We can work in the default loop. */
    ASSERT_OK(uv_loop_fork(uv_default_loop()));
    assert_run_work(uv_default_loop());
    printf("Exiting child \n");
  }


  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif /* !__MVS__ */

#else

typedef int file_has_no_tests; /* ISO C forbids an empty translation unit. */

#endif /* !_WIN32 */
