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

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
#include <crtdbg.h>
#endif

#include "uv.h"
#include "internal.h"
#include "queue.h"
#include "handle-inl.h"
#include "heap-inl.h"
#include "req-inl.h"

/* uv_once initialization guards */
static uv_once_t uv_init_guard_ = UV_ONCE_INIT;


#if defined(_DEBUG) && (defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR))
/* Our crt debug report handler allows us to temporarily disable asserts
 * just for the current thread.
 */

UV_THREAD_LOCAL int uv__crt_assert_enabled = TRUE;

static int uv__crt_dbg_report_handler(int report_type, char *message, int *ret_val) {
  if (uv__crt_assert_enabled || report_type != _CRT_ASSERT)
    return FALSE;

  if (ret_val) {
    /* Set ret_val to 0 to continue with normal execution.
     * Set ret_val to 1 to trigger a breakpoint.
    */

    if(IsDebuggerPresent())
      *ret_val = 1;
    else
      *ret_val = 0;
  }

  /* Don't call _CrtDbgReport. */
  return TRUE;
}
#else
UV_THREAD_LOCAL int uv__crt_assert_enabled = FALSE;
#endif


#if !defined(__MINGW32__) || __MSVCRT_VERSION__ >= 0x800
static void uv__crt_invalid_parameter_handler(const wchar_t* expression,
    const wchar_t* function, const wchar_t * file, unsigned int line,
    uintptr_t reserved) {
  /* No-op. */
}
#endif

static uv_loop_t** uv__loops;
static int uv__loops_size;
static int uv__loops_capacity;
#define UV__LOOPS_CHUNK_SIZE 8
static uv_mutex_t uv__loops_lock;


static void uv__loops_init(void) {
  uv_mutex_init(&uv__loops_lock);
}


static int uv__loops_add(uv_loop_t* loop) {
  uv_loop_t** new_loops;
  int new_capacity, i;

  uv_mutex_lock(&uv__loops_lock);

  if (uv__loops_size == uv__loops_capacity) {
    new_capacity = uv__loops_capacity + UV__LOOPS_CHUNK_SIZE;
    new_loops = uv__realloc(uv__loops, sizeof(uv_loop_t*) * new_capacity);
    if (!new_loops)
      goto failed_loops_realloc;
    uv__loops = new_loops;
    for (i = uv__loops_capacity; i < new_capacity; ++i)
      uv__loops[i] = NULL;
    uv__loops_capacity = new_capacity;
  }
  uv__loops[uv__loops_size] = loop;
  ++uv__loops_size;

  uv_mutex_unlock(&uv__loops_lock);
  return 0;

failed_loops_realloc:
  uv_mutex_unlock(&uv__loops_lock);
  return ERROR_OUTOFMEMORY;
}


static void uv__loops_remove(uv_loop_t* loop) {
  int loop_index;
  int smaller_capacity;
  uv_loop_t** new_loops;

  uv_mutex_lock(&uv__loops_lock);

  for (loop_index = 0; loop_index < uv__loops_size; ++loop_index) {
    if (uv__loops[loop_index] == loop)
      break;
  }
  /* If loop was not found, ignore */
  if (loop_index == uv__loops_size)
    goto loop_removed;

  uv__loops[loop_index] = uv__loops[uv__loops_size - 1];
  uv__loops[uv__loops_size - 1] = NULL;
  --uv__loops_size;

  if (uv__loops_size == 0) {
    uv__loops_capacity = 0;
    uv__free(uv__loops);
    uv__loops = NULL;
    goto loop_removed;
  }

  /* If we didn't grow to big skip downsizing */
  if (uv__loops_capacity < 4 * UV__LOOPS_CHUNK_SIZE)
    goto loop_removed;

  /* Downsize only if more than half of buffer is free */
  smaller_capacity = uv__loops_capacity / 2;
  if (uv__loops_size >= smaller_capacity)
    goto loop_removed;
  new_loops = uv__realloc(uv__loops, sizeof(uv_loop_t*) * smaller_capacity);
  if (!new_loops)
    goto loop_removed;
  uv__loops = new_loops;
  uv__loops_capacity = smaller_capacity;

loop_removed:
  uv_mutex_unlock(&uv__loops_lock);
}

void uv__wake_all_loops(void) {
  int i;
  uv_loop_t* loop;

  uv_mutex_lock(&uv__loops_lock);
  for (i = 0; i < uv__loops_size; ++i) {
    loop = uv__loops[i];
    assert(loop);
    if (loop->iocp != INVALID_HANDLE_VALUE)
      PostQueuedCompletionStatus(loop->iocp, 0, 0, NULL);
  }
  uv_mutex_unlock(&uv__loops_lock);
}

static void uv__init(void) {
  /* Tell Windows that we will handle critical errors. */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);

  /* Tell the CRT to not exit the application when an invalid parameter is
   * passed. The main issue is that invalid FDs will trigger this behavior.
   */
#if !defined(__MINGW32__) || __MSVCRT_VERSION__ >= 0x800
  _set_invalid_parameter_handler(uv__crt_invalid_parameter_handler);
#endif

  /* We also need to setup our debug report handler because some CRT
   * functions (eg _get_osfhandle) raise an assert when called with invalid
   * FDs even though they return the proper error code in the release build.
   */
#if defined(_DEBUG) && (defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR))
  _CrtSetReportHook(uv__crt_dbg_report_handler);
#endif

  /* Initialize tracking of all uv loops */
  uv__loops_init();

  /* Fetch winapi function pointers. This must be done first because other
   * initialization code might need these function pointers to be loaded.
   */
  uv__winapi_init();

  /* Initialize winsock */
  uv__winsock_init();

  /* Initialize FS */
  uv__fs_init();

  /* Initialize signal stuff */
  uv__signals_init();

  /* Initialize console */
  uv__console_init();

  /* Initialize utilities */
  uv__util_init();

  /* Initialize system wakeup detection */
  uv__init_detect_system_wakeup();
}


int uv_loop_init(uv_loop_t* loop) {
  uv__loop_internal_fields_t* lfields;
  struct heap* timer_heap;
  int err;

  /* Initialize libuv itself first */
  uv__once_init();

  /* Create an I/O completion port */
  loop->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (loop->iocp == NULL)
    return uv_translate_sys_error(GetLastError());

  lfields = (uv__loop_internal_fields_t*) uv__calloc(1, sizeof(*lfields));
  if (lfields == NULL)
    return UV_ENOMEM;
  loop->internal_fields = lfields;

  err = uv_mutex_init(&lfields->loop_metrics.lock);
  if (err)
    goto fail_metrics_mutex_init;
  memset(&lfields->loop_metrics.metrics,
         0,
         sizeof(lfields->loop_metrics.metrics));

  /* To prevent uninitialized memory access, loop->time must be initialized
   * to zero before calling uv_update_time for the first time.
   */
  loop->time = 0;
  uv_update_time(loop);

  uv__queue_init(&loop->wq);
  uv__queue_init(&loop->handle_queue);
  loop->active_reqs.count = 0;
  loop->active_handles = 0;

  loop->pending_reqs_tail = NULL;

  loop->endgame_handles = NULL;

  loop->timer_heap = timer_heap = uv__malloc(sizeof(*timer_heap));
  if (timer_heap == NULL) {
    err = UV_ENOMEM;
    goto fail_timers_alloc;
  }

  heap_init(timer_heap);

  loop->check_handles = NULL;
  loop->prepare_handles = NULL;
  loop->idle_handles = NULL;

  loop->next_prepare_handle = NULL;
  loop->next_check_handle = NULL;
  loop->next_idle_handle = NULL;

  memset(&loop->poll_peer_sockets, 0, sizeof loop->poll_peer_sockets);

  loop->timer_counter = 0;
  loop->stop_flag = 0;

  err = uv_mutex_init(&loop->wq_mutex);
  if (err)
    goto fail_mutex_init;

  err = uv_async_init(loop, &loop->wq_async, uv__work_done);
  if (err)
    goto fail_async_init;

  uv__handle_unref(&loop->wq_async);
  loop->wq_async.flags |= UV_HANDLE_INTERNAL;

  err = uv__loops_add(loop);
  if (err)
    goto fail_async_init;

  return 0;

fail_async_init:
  uv_mutex_destroy(&loop->wq_mutex);

fail_mutex_init:
  uv__free(timer_heap);
  loop->timer_heap = NULL;

fail_timers_alloc:
  uv_mutex_destroy(&lfields->loop_metrics.lock);

fail_metrics_mutex_init:
  uv__free(lfields);
  loop->internal_fields = NULL;
  CloseHandle(loop->iocp);
  loop->iocp = INVALID_HANDLE_VALUE;

  return err;
}


void uv_update_time(uv_loop_t* loop) {
  uint64_t new_time = uv__hrtime(1000);
  assert(new_time >= loop->time);
  loop->time = new_time;
}


void uv__once_init(void) {
  uv_once(&uv_init_guard_, uv__init);
}


void uv__loop_close(uv_loop_t* loop) {
  uv__loop_internal_fields_t* lfields;
  size_t i;

  uv__loops_remove(loop);

  /* Close the async handle without needing an extra loop iteration.
   * We might have a pending message, but we're just going to destroy the IOCP
   * soon, so we can just discard it now without the usual risk of a getting
   * another notification from GetQueuedCompletionStatusEx after calling the
   * close_cb (which we also skip defining). We'll assert later that queue was
   * actually empty and all reqs handled. */
  loop->wq_async.async_sent = 0;
  loop->wq_async.close_cb = NULL;
  uv__handle_closing(&loop->wq_async);
  uv__handle_close(&loop->wq_async);

  for (i = 0; i < ARRAY_SIZE(loop->poll_peer_sockets); i++) {
    SOCKET sock = loop->poll_peer_sockets[i];
    if (sock != 0 && sock != INVALID_SOCKET)
      closesocket(sock);
  }

  uv_mutex_lock(&loop->wq_mutex);
  assert(uv__queue_empty(&loop->wq) && "thread pool work queue not empty!");
  assert(!uv__has_active_reqs(loop));
  uv_mutex_unlock(&loop->wq_mutex);
  uv_mutex_destroy(&loop->wq_mutex);

  uv__free(loop->timer_heap);
  loop->timer_heap = NULL;

  lfields = uv__get_internal_fields(loop);
  uv_mutex_destroy(&lfields->loop_metrics.lock);
  uv__free(lfields);
  loop->internal_fields = NULL;

  CloseHandle(loop->iocp);
}


int uv__loop_configure(uv_loop_t* loop, uv_loop_option option, va_list ap) {
  uv__loop_internal_fields_t* lfields;

  lfields = uv__get_internal_fields(loop);
  if (option == UV_METRICS_IDLE_TIME) {
    lfields->flags |= UV_METRICS_IDLE_TIME;
    return 0;
  }

  return UV_ENOSYS;
}


int uv_backend_fd(const uv_loop_t* loop) {
  return -1;
}


int uv_loop_fork(uv_loop_t* loop) {
  return UV_ENOSYS;
}


static int uv__loop_alive(const uv_loop_t* loop) {
  return uv__has_active_handles(loop) ||
         uv__has_active_reqs(loop) ||
         loop->pending_reqs_tail != NULL ||
         loop->endgame_handles != NULL;
}


int uv_loop_alive(const uv_loop_t* loop) {
  return uv__loop_alive(loop);
}


int uv_backend_timeout(const uv_loop_t* loop) {
  if (loop->stop_flag == 0 &&
      /* uv__loop_alive(loop) && */
      (uv__has_active_handles(loop) || uv__has_active_reqs(loop)) &&
      loop->pending_reqs_tail == NULL &&
      loop->idle_handles == NULL &&
      loop->endgame_handles == NULL)
    return uv__next_timeout(loop);
  return 0;
}


static void uv__poll(uv_loop_t* loop, DWORD timeout) {
  uv__loop_internal_fields_t* lfields;
  BOOL success;
  uv_req_t* req;
  OVERLAPPED_ENTRY overlappeds[128];
  ULONG count;
  ULONG i;
  int repeat;
  uint64_t timeout_time;
  uint64_t user_timeout;
  uint64_t actual_timeout;
  int reset_timeout;

  lfields = uv__get_internal_fields(loop);
  timeout_time = loop->time + timeout;

  if (lfields->flags & UV_METRICS_IDLE_TIME) {
    reset_timeout = 1;
    user_timeout = timeout;
    timeout = 0;
  } else {
    reset_timeout = 0;
  }

  for (repeat = 0; ; repeat++) {
    actual_timeout = timeout;

    /* Only need to set the provider_entry_time if timeout != 0. The function
     * will return early if the loop isn't configured with UV_METRICS_IDLE_TIME.
     */
    if (timeout != 0)
      uv__metrics_set_provider_entry_time(loop);

    /* Store the current timeout in a location that's globally accessible so
     * other locations like uv__work_done() can determine whether the queue
     * of events in the callback were waiting when poll was called.
     */
    lfields->current_timeout = timeout;

    success = GetQueuedCompletionStatusEx(loop->iocp,
                                          overlappeds,
                                          ARRAY_SIZE(overlappeds),
                                          &count,
                                          timeout,
                                          FALSE);

    if (reset_timeout != 0) {
      timeout = user_timeout;
      reset_timeout = 0;
    }

    /* Placed here because on success the loop will break whether there is an
     * empty package or not, or if GetQueuedCompletionStatusEx returned early
     * then the timeout will be updated and the loop will run again. In either
     * case the idle time will need to be updated.
     */
    uv__metrics_update_idle_time(loop);

    if (success) {
      for (i = 0; i < count; i++) {
        /* Package was dequeued, but see if it is not a empty package
         * meant only to wake us up.
         */
        if (overlappeds[i].lpOverlapped) {
          uv__metrics_inc_events(loop, 1);
          if (actual_timeout == 0)
            uv__metrics_inc_events_waiting(loop, 1);

          req = uv__overlapped_to_req(overlappeds[i].lpOverlapped);
          uv__insert_pending_req(loop, req);
        }
      }

      /* Some time might have passed waiting for I/O,
       * so update the loop time here.
       */
      uv_update_time(loop);
    } else if (GetLastError() != WAIT_TIMEOUT) {
      /* Serious error */
      uv_fatal_error(GetLastError(), "GetQueuedCompletionStatusEx");
    } else if (timeout > 0) {
      /* GetQueuedCompletionStatus can occasionally return a little early.
       * Make sure that the desired timeout target time is reached.
       */
      uv_update_time(loop);
      if (timeout_time > loop->time) {
        timeout = (DWORD)(timeout_time - loop->time);
        /* The first call to GetQueuedCompletionStatus should return very
         * close to the target time and the second should reach it, but
         * this is not stated in the documentation. To make sure a busy
         * loop cannot happen, the timeout is increased exponentially
         * starting on the third round.
         */
        timeout += repeat ? (1 << (repeat - 1)) : 0;
        continue;
      }
    }
    break;
  }
}


int uv_run(uv_loop_t *loop, uv_run_mode mode) {
  DWORD timeout;
  int r;
  int can_sleep;

  r = uv__loop_alive(loop);
  if (!r)
    uv_update_time(loop);

  /* Maintain backwards compatibility by processing timers before entering the
   * while loop for UV_RUN_DEFAULT. Otherwise timers only need to be executed
   * once, which should be done after polling in order to maintain proper
   * execution order of the conceptual event loop. */
  if (mode == UV_RUN_DEFAULT && r != 0 && loop->stop_flag == 0) {
    uv_update_time(loop);
    uv__run_timers(loop);
  }

  while (r != 0 && loop->stop_flag == 0) {
    can_sleep = loop->pending_reqs_tail == NULL && loop->idle_handles == NULL;

    uv__process_reqs(loop);
    uv__idle_invoke(loop);
    uv__prepare_invoke(loop);

    timeout = 0;
    if ((mode == UV_RUN_ONCE && can_sleep) || mode == UV_RUN_DEFAULT)
      timeout = uv_backend_timeout(loop);

    uv__metrics_inc_loop_count(loop);

    uv__poll(loop, timeout);

    /* Process immediate callbacks (e.g. write_cb) a small fixed number of
     * times to avoid loop starvation.*/
    for (r = 0; r < 8 && loop->pending_reqs_tail != NULL; r++)
      uv__process_reqs(loop);

    /* Run one final update on the provider_idle_time in case uv__poll*
     * returned because the timeout expired, but no events were received. This
     * call will be ignored if the provider_entry_time was either never set (if
     * the timeout == 0) or was already updated b/c an event was received.
     */
    uv__metrics_update_idle_time(loop);

    uv__check_invoke(loop);
    uv__process_endgames(loop);

    uv_update_time(loop);
    uv__run_timers(loop);

    r = uv__loop_alive(loop);
    if (mode == UV_RUN_ONCE || mode == UV_RUN_NOWAIT)
      break;
  }

  /* The if statement lets the compiler compile it to a conditional store.
   * Avoids dirtying a cache line.
   */
  if (loop->stop_flag != 0)
    loop->stop_flag = 0;

  return r;
}


int uv_fileno(const uv_handle_t* handle, uv_os_fd_t* fd) {
  uv_os_fd_t fd_out;

  switch (handle->type) {
  case UV_TCP:
    fd_out = (uv_os_fd_t)((uv_tcp_t*) handle)->socket;
    break;

  case UV_NAMED_PIPE:
    fd_out = ((uv_pipe_t*) handle)->handle;
    break;

  case UV_TTY:
    fd_out = ((uv_tty_t*) handle)->handle;
    break;

  case UV_UDP:
    fd_out = (uv_os_fd_t)((uv_udp_t*) handle)->socket;
    break;

  case UV_POLL:
    fd_out = (uv_os_fd_t)((uv_poll_t*) handle)->socket;
    break;

  default:
    return UV_EINVAL;
  }

  if (uv_is_closing(handle) || fd_out == INVALID_HANDLE_VALUE)
    return UV_EBADF;

  *fd = fd_out;
  return 0;
}


int uv__socket_sockopt(uv_handle_t* handle, int optname, int* value) {
  int r;
  int len;
  SOCKET socket;

  if (handle == NULL || value == NULL)
    return UV_EINVAL;

  if (handle->type == UV_TCP)
    socket = ((uv_tcp_t*) handle)->socket;
  else if (handle->type == UV_UDP)
    socket = ((uv_udp_t*) handle)->socket;
  else
    return UV_ENOTSUP;

  len = sizeof(*value);

  if (*value == 0)
    r = getsockopt(socket, SOL_SOCKET, optname, (char*) value, &len);
  else
    r = setsockopt(socket, SOL_SOCKET, optname, (const char*) value, len);

  if (r == SOCKET_ERROR)
    return uv_translate_sys_error(WSAGetLastError());

  return 0;
}

int uv_cpumask_size(void) {
  return (int)(sizeof(DWORD_PTR) * 8);
}

int uv__getsockpeername(const uv_handle_t* handle,
                        uv__peersockfunc func,
                        struct sockaddr* name,
                        int* namelen,
                        int delayed_error) {

  int result;
  uv_os_fd_t fd;

  result = uv_fileno(handle, &fd);
  if (result != 0)
    return result;

  if (delayed_error)
    return uv_translate_sys_error(delayed_error);

  result = func((SOCKET) fd, name, namelen);
  if (result != 0)
    return uv_translate_sys_error(WSAGetLastError());

  return 0;
}
