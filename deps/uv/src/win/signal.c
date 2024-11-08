/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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
#include <signal.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "req-inl.h"


RB_HEAD(uv_signal_tree_s, uv_signal_s);

static struct uv_signal_tree_s uv__signal_tree = RB_INITIALIZER(uv__signal_tree);
static CRITICAL_SECTION uv__signal_lock;

static BOOL WINAPI uv__signal_control_handler(DWORD type);

int uv__signal_start(uv_signal_t* handle,
                     uv_signal_cb signal_cb,
                     int signum,
                     int oneshot);

void uv__signals_init(void) {
  InitializeCriticalSection(&uv__signal_lock);
  if (!SetConsoleCtrlHandler(uv__signal_control_handler, TRUE))
    abort();
}


void uv__signal_cleanup(void) {
  /* TODO(bnoordhuis) Undo effects of uv_signal_init()? */
}


static int uv__signal_compare(uv_signal_t* w1, uv_signal_t* w2) {
  /* Compare signums first so all watchers with the same signnum end up
   * adjacent. */
  if (w1->signum < w2->signum) return -1;
  if (w1->signum > w2->signum) return 1;

  /* Sort by loop pointer, so we can easily look up the first item after
   * { .signum = x, .loop = NULL }. */
  if ((uintptr_t) w1->loop < (uintptr_t) w2->loop) return -1;
  if ((uintptr_t) w1->loop > (uintptr_t) w2->loop) return 1;

  if ((uintptr_t) w1 < (uintptr_t) w2) return -1;
  if ((uintptr_t) w1 > (uintptr_t) w2) return 1;

  return 0;
}


RB_GENERATE_STATIC(uv_signal_tree_s, uv_signal_s, tree_entry, uv__signal_compare)


/*
 * Dispatches signal {signum} to all active uv_signal_t watchers in all loops.
 * Returns 1 if the signal was dispatched to any watcher, or 0 if there were
 * no active signal watchers observing this signal.
 */
int uv__signal_dispatch(int signum) {
  uv_signal_t lookup;
  uv_signal_t* handle;
  int dispatched;

  dispatched = 0;

  EnterCriticalSection(&uv__signal_lock);

  lookup.signum = signum;
  lookup.loop = NULL;

  for (handle = RB_NFIND(uv_signal_tree_s, &uv__signal_tree, &lookup);
       handle != NULL && handle->signum == signum;
       handle = RB_NEXT(uv_signal_tree_s, handle)) {
    unsigned long previous = InterlockedExchange(
            (volatile LONG*) &handle->pending_signum, signum);

    if (handle->flags & UV_SIGNAL_ONE_SHOT_DISPATCHED)
      continue;

    if (!previous) {
      POST_COMPLETION_FOR_REQ(handle->loop, &handle->signal_req);
    }

    dispatched = 1;
    if (handle->flags & UV_SIGNAL_ONE_SHOT)
      handle->flags |= UV_SIGNAL_ONE_SHOT_DISPATCHED;
  }

  LeaveCriticalSection(&uv__signal_lock);

  return dispatched;
}


static BOOL WINAPI uv__signal_control_handler(DWORD type) {
  switch (type) {
    case CTRL_C_EVENT:
      return uv__signal_dispatch(SIGINT);

    case CTRL_BREAK_EVENT:
      return uv__signal_dispatch(SIGBREAK);

    case CTRL_CLOSE_EVENT:
      if (uv__signal_dispatch(SIGHUP)) {
        /* Windows will terminate the process after the control handler
         * returns. After that it will just terminate our process. Therefore
         * block the signal handler so the main loop has some time to pick up
         * the signal and do something for a few seconds. */
        Sleep(INFINITE);
        return TRUE;
      }
      return FALSE;

    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      /* These signals are only sent to services. Services have their own
       * notification mechanism, so there's no point in handling these. */

    default:
      /* We don't handle these. */
      return FALSE;
  }
}


int uv_signal_init(uv_loop_t* loop, uv_signal_t* handle) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_SIGNAL);
  handle->pending_signum = 0;
  handle->signum = 0;
  handle->signal_cb = NULL;

  UV_REQ_INIT(&handle->signal_req, UV_SIGNAL_REQ);
  handle->signal_req.data = handle;

  return 0;
}


int uv_signal_stop(uv_signal_t* handle) {
  uv_signal_t* removed_handle;

  /* If the watcher wasn't started, this is a no-op. */
  if (handle->signum == 0)
    return 0;

  EnterCriticalSection(&uv__signal_lock);

  removed_handle = RB_REMOVE(uv_signal_tree_s, &uv__signal_tree, handle);
  assert(removed_handle == handle);

  LeaveCriticalSection(&uv__signal_lock);

  handle->signum = 0;
  uv__handle_stop(handle);

  return 0;
}


int uv_signal_start(uv_signal_t* handle, uv_signal_cb signal_cb, int signum) {
  return uv__signal_start(handle, signal_cb, signum, 0);
}


int uv_signal_start_oneshot(uv_signal_t* handle,
                            uv_signal_cb signal_cb,
                            int signum) {
  return uv__signal_start(handle, signal_cb, signum, 1);
}


int uv__signal_start(uv_signal_t* handle,
                            uv_signal_cb signal_cb,
                            int signum,
                            int oneshot) {
  /* Test for invalid signal values. */
  if (signum <= 0 || signum >= NSIG)
    return UV_EINVAL;

  /* Short circuit: if the signal watcher is already watching {signum} don't go
   * through the process of deregistering and registering the handler.
   * Additionally, this avoids pending signals getting lost in the (small) time
   * frame that handle->signum == 0. */
  if (signum == handle->signum) {
    handle->signal_cb = signal_cb;
    return 0;
  }

  /* If the signal handler was already active, stop it first. */
  if (handle->signum != 0) {
    int r = uv_signal_stop(handle);
    /* uv_signal_stop is infallible. */
    assert(r == 0);
  }

  EnterCriticalSection(&uv__signal_lock);

  handle->signum = signum;
  if (oneshot)
    handle->flags |= UV_SIGNAL_ONE_SHOT;

  RB_INSERT(uv_signal_tree_s, &uv__signal_tree, handle);

  LeaveCriticalSection(&uv__signal_lock);

  handle->signal_cb = signal_cb;
  uv__handle_start(handle);

  return 0;
}


void uv__process_signal_req(uv_loop_t* loop, uv_signal_t* handle,
    uv_req_t* req) {
  long dispatched_signum;

  assert(handle->type == UV_SIGNAL);
  assert(req->type == UV_SIGNAL_REQ);

  dispatched_signum = InterlockedExchange(
          (volatile LONG*) &handle->pending_signum, 0);
  assert(dispatched_signum != 0);

  /* Check if the pending signal equals the signum that we are watching for.
   * These can get out of sync when the handler is stopped and restarted while
   * the signal_req is pending. */
  if (dispatched_signum == handle->signum)
    handle->signal_cb(handle, dispatched_signum);

  if (handle->flags & UV_SIGNAL_ONE_SHOT)
    uv_signal_stop(handle);

  if (handle->flags & UV_HANDLE_CLOSING) {
    /* When it is closing, it must be stopped at this point. */
    assert(handle->signum == 0);
    uv__want_endgame(loop, (uv_handle_t*) handle);
  }
}


void uv__signal_close(uv_loop_t* loop, uv_signal_t* handle) {
  uv_signal_stop(handle);
  uv__handle_closing(handle);

  if (handle->pending_signum == 0) {
    uv__want_endgame(loop, (uv_handle_t*) handle);
  }
}


void uv__signal_endgame(uv_loop_t* loop, uv_signal_t* handle) {
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));

  assert(handle->signum == 0);
  assert(handle->pending_signum == 0);

  handle->flags |= UV_HANDLE_CLOSED;

  uv__handle_close(handle);
}
