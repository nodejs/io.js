/* MIT License
 *
 * Copyright (c) 2024 Brad House
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

/* Uses an anonymous union */
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic push
#  if defined(__clang__)
#    pragma GCC diagnostic ignored "-Wc11-extensions"
#  else
#    pragma GCC diagnostic ignored "-Wpedantic"
#  endif
#endif

#include "ares_private.h"
#include "ares_event.h"
#include "ares_event_win32.h"
#ifdef HAVE_LIMITS_H
#  include <limits.h>
#endif

#if defined(USE_WINSOCK)

/* IMPLEMENTATION NOTES
 * ====================
 *
 * This implementation uses some undocumented functionality within Windows for
 * monitoring sockets. The Ancillary Function Driver (AFD) is the low level
 * implementation that Winsock2 sits on top of.  Winsock2 unfortunately does
 * not expose the equivalent of epoll() or kqueue(), but it is possible to
 * access AFD directly and use along with IOCP to simulate the functionality.
 * We want to use IOCP if possible as it gives us the ability to monitor more
 * than just sockets (WSAPoll is not an option), and perform arbitrary callbacks
 * which means we can hook in non-socket related events.
 *
 * The information for this implementation was gathered from "wepoll" and
 * "libuv" which both use slight variants on this.  We originally went with
 * an implementation methodology more similar to "libuv", but we had a few
 * user reports of crashes during shutdown and memory leaks due to some
 * events not being delivered for cleanup of closed sockets.
 *
 * Initialization:
 *   1. Dynamically load the NtDeviceIoControlFile, NtCreateFile, and
 *      NtCancelIoFileEx internal symbols from ntdll.dll. (Don't believe
 *      Microsoft's documentation for NtCancelIoFileEx as it documents an
 *      invalid prototype). These functions are to open a reference to the
 *      Ancillary Function Driver (AFD), and to submit and cancel POLL
 *      requests.
 *   2. Create an IO Completion Port base handle via CreateIoCompletionPort()
 *      that all socket events will be delivered through.
 *   3. Create a list of AFD Handles and track the number of poll requests
 *      per AFD handle.  When we exceed a pre-determined limit of poll requests
 *      for a handle (128), we will automatically create a new handle.  The
 *      reason behind this is NtCancelIoFileEx uses a horrible algorithm for
 *      issuing cancellations.  See:
 *      https://github.com/python-trio/trio/issues/52#issuecomment-548215128
 *   4. Create a callback to be used to be able to interrupt waiting for IOCP
 *      events, this may be called for allowing enqueuing of additional socket
 *      events or removing socket events. PostQueuedCompletionStatus() is the
 *      obvious choice.  We can use the same container format, the event
 *      delivered won't have an OVERLAPPED pointer so we can differentiate from
 *      socket events.  Use the container as the completion key.
 *
 * Socket Add:
 *   1. Create/Allocate a container for holding metadata about a socket
 *      including:
 *      - SOCKET base_socket;
 *      - IO_STATUS_BLOCK iosb; -- Used by AFD POLL, returned as OVERLAPPED
 *      - AFD_POLL_INFO afd_poll_info; -- Used by AFD POLL
 *      - afd list node -- for tracking which AFD handle a POLL request was
 *        submitted to.
 *   2. Call WSAIoctl(..., SIO_BASE_HANDLE, ...) to unwrap the SOCKET and get
 *      the "base socket" we can use for polling.  It appears this may fail so
 *      we should call WSAIoctl(..., SIO_BSP_HANDLE_POLL, ...) as a fallback.
 *   3. Submit AFD POLL request (see "AFD POLL Request" section)
 *   4. Record a mapping between the "IO Status Block" and the socket container
 *      so when events are delivered we can dereference.
 *
 * Socket Delete:
 *   1. Call
 *      NtCancelIoFileEx(afd, iosb, &temp_iosb);
 *      to cancel any pending operations.
 *   2. Tag the socket container as being queued for deletion
 *   3. Wait for an event to be delivered for the socket (cancel isn't
 *      immediate, it delivers an event to know its complete). Delete only once
 *      that event has been delivered.  If we don't do this we could try to
 *      access free()'d memory at a later point.
 *
 * Socket Modify:
 *   1. Call
 *      NtCancelIoFileEx(afd, iosb, &temp_iosb)
 *      to cancel any pending operation.
 *   2. When the event comes through that the cancel is complete, enqueue
 *      another "AFD Poll Request" for the desired events.
 *
 * Event Wait:
 *   1. Call GetQueuedCompletionStatusEx() with the base IOCP handle, a
 *      stack allocated array of OVERLAPPED_ENTRY's, and an appropriate
 *      timeout.
 *   2. Iterate across returned events, if the lpOverlapped is NULL, then the
 *      the CompletionKey is a pointer to the container registered via
 *      PostQueuedCompletionStatus(), otherwise it is the "IO Status Block"
 *      registered with the "AFD Poll Request" which needs to be dereferenced
 *      to the "socket container".
 *   3. If it is a "socket container", disassociate it from the afd list node
 *      it was previously submitted to.
 *   4. If it is a "socket container" check to see if we are cleaning up, if so,
 *      clean it up.
 *   5. If it is a "socket container" that is still valid, Submit an
 *      AFD POLL Request (see "AFD POLL Request"). We must re-enable the request
 *      each time we receive a response, it is not persistent.
 *   6. Notify of any events received as indicated in the AFD_POLL_INFO
 *      Handles[0].Events (NOTE: check NumberOfHandles > 0, and the status in
 *      the IO_STATUS_BLOCK.  If we received an AFD_POLL_LOCAL_CLOSE, clean up
 *      the connection like the integrator requested it to be cleaned up.
 *
 * AFD Poll Request:
 *   1. Find an afd poll handle in the list that has fewer pending requests than
 *      the limit.
 *   2. If an afd poll handle was not associated (e.g. due to all being over
 *      limit), create a new afd poll handle by calling NtCreateFile()
 *      with path \Device\Afd , then add the AFD handle to the IO Completion
 *      Port.  We can leave the completion key as blank since events for
 *      multiple sockets will be delivered through this and we need to
 *      differentiate via the OVERLAPPED member returned.  Add the new AFD
 *      handle to the list of handles.
 *   3. Initialize the AFD_POLL_INFO structure:
 *      Exclusive         = FALSE; // allow multiple requests
 *      NumberOfHandles   = 1;
 *      Timeout.QuadPart  = LLONG_MAX;
 *      Handles[0].Handle = (HANDLE)base_socket;
 *      Handles[0].Status = 0;
 *      Handles[0].Events = AFD_POLL_LOCAL_CLOSE + additional events to wait for
 *                          such as AFD_POLL_RECEIVE, etc;
 *   4. Zero out the IO_STATUS_BLOCK structures
 *   5. Set the "Status" member of IO_STATUS_BLOCK to STATUS_PENDING
 *   6. Call
 *      NtDeviceIoControlFile(afd, NULL, NULL, &iosb,
 *                            &iosb, IOCTL_AFD_POLL
 *                            &afd_poll_info, sizeof(afd_poll_info),
 *                            &afd_poll_info, sizeof(afd_poll_info));
 *
 *
 * References:
 *   - https://github.com/piscisaureus/wepoll/
 *   - https://github.com/libuv/libuv/
 */

/* Cap the number of outstanding AFD poll requests per AFD handle due to known
 * slowdowns with large lists and NtCancelIoFileEx() */
#  define AFD_POLL_PER_HANDLE 128

#  include <stdarg.h>

/* #  define CARES_DEBUG 1 */

#  ifdef __GNUC__
#    define CARES_PRINTF_LIKE(fmt, args) \
      __attribute__((format(printf, fmt, args)))
#  else
#    define CARES_PRINTF_LIKE(fmt, args)
#  endif

static void CARES_DEBUG_LOG(const char *fmt, ...) CARES_PRINTF_LIKE(1, 2);

static void CARES_DEBUG_LOG(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
#  ifdef CARES_DEBUG
  vfprintf(stderr, fmt, ap);
  fflush(stderr);
#  endif
  va_end(ap);
}

typedef struct {
  /* Dynamically loaded symbols */
  NtCreateFile_t          NtCreateFile;
  NtDeviceIoControlFile_t NtDeviceIoControlFile;
  NtCancelIoFileEx_t      NtCancelIoFileEx;

  /* Implementation details */
  ares__slist_t          *afd_handles;
  HANDLE                  iocp_handle;

  /* IO_STATUS_BLOCK * -> ares_evsys_win32_eventdata_t * mapping.  There is
   * no completion key passed to IOCP with this method so we have to look
   * up based on the lpOverlapped returned (which is mapped to IO_STATUS_BLOCK)
   */
  ares__htable_vpvp_t    *sockets;

  /* Flag about whether or not we are shutting down */
  ares_bool_t             is_shutdown;
} ares_evsys_win32_t;

typedef enum {
  POLL_STATUS_NONE    = 0,
  POLL_STATUS_PENDING = 1,
  POLL_STATUS_CANCEL  = 2,
  POLL_STATUS_DESTROY = 3
} poll_status_t;

typedef struct {
  /*! Pointer to parent event container */
  ares_event_t         *event;
  /*! Socket passed in to monitor */
  SOCKET                socket;
  /*! Base socket derived from provided socket */
  SOCKET                base_socket;
  /*! Structure for submitting AFD POLL requests (Internals!) */
  AFD_POLL_INFO         afd_poll_info;
  /*! Status of current polling operation */
  poll_status_t         poll_status;
  /*! IO Status Block structure submitted with AFD POLL requests and returned
   *  with IOCP results as lpOverlapped (even though its a different structure)
   */
  IO_STATUS_BLOCK       iosb;
  /*! AFD handle node an outstanding poll request is associated with */
  ares__slist_node_t   *afd_handle_node;
  /* Lock is only for PostQueuedCompletionStatus() to prevent multiple
   * signals. Tracking via POLL_STATUS_PENDING/POLL_STATUS_NONE */
  ares__thread_mutex_t *lock;
} ares_evsys_win32_eventdata_t;

static size_t ares_evsys_win32_wait(ares_event_thread_t *e,
                                    unsigned long        timeout_ms);

static void   ares_iocpevent_signal(const ares_event_t *event)
{
  ares_event_thread_t          *e           = event->e;
  ares_evsys_win32_t           *ew          = e->ev_sys_data;
  ares_evsys_win32_eventdata_t *ed          = event->data;
  ares_bool_t                   queue_event = ARES_FALSE;

  ares__thread_mutex_lock(ed->lock);
  if (ed->poll_status != POLL_STATUS_PENDING) {
    ed->poll_status = POLL_STATUS_PENDING;
    queue_event     = ARES_TRUE;
  }
  ares__thread_mutex_unlock(ed->lock);

  if (!queue_event) {
    return;
  }

  PostQueuedCompletionStatus(ew->iocp_handle, 0, (ULONG_PTR)event->data, NULL);
}

static void ares_iocpevent_cb(ares_event_thread_t *e, ares_socket_t fd,
                              void *data, ares_event_flags_t flags)
{
  ares_evsys_win32_eventdata_t *ed = data;
  (void)e;
  (void)fd;
  (void)flags;
  ares__thread_mutex_lock(ed->lock);
  ed->poll_status = POLL_STATUS_NONE;
  ares__thread_mutex_unlock(ed->lock);
}

static ares_event_t *ares_iocpevent_create(ares_event_thread_t *e)
{
  ares_event_t *event = NULL;
  ares_status_t status;

  status =
    ares_event_update(&event, e, ARES_EVENT_FLAG_OTHER, ares_iocpevent_cb,
                      ARES_SOCKET_BAD, NULL, NULL, ares_iocpevent_signal);
  if (status != ARES_SUCCESS) {
    return NULL;
  }

  return event;
}

static void ares_evsys_win32_destroy(ares_event_thread_t *e)
{
  ares_evsys_win32_t *ew = NULL;

  if (e == NULL) {
    return;
  }

  CARES_DEBUG_LOG("** Win32 Event Destroy\n");

  ew = e->ev_sys_data;
  if (ew == NULL) {
    return;
  }

  ew->is_shutdown = ARES_TRUE;
  CARES_DEBUG_LOG("  ** waiting on %lu remaining sockets to be destroyed\n",
                  (unsigned long)ares__htable_vpvp_num_keys(ew->sockets));
  while (ares__htable_vpvp_num_keys(ew->sockets)) {
    ares_evsys_win32_wait(e, 0);
  }
  CARES_DEBUG_LOG("  ** all sockets cleaned up\n");


  if (ew->iocp_handle != NULL) {
    CloseHandle(ew->iocp_handle);
  }

  ares__slist_destroy(ew->afd_handles);

  ares__htable_vpvp_destroy(ew->sockets);

  ares_free(ew);
  e->ev_sys_data = NULL;
}

typedef struct {
  size_t poll_cnt;
  HANDLE afd_handle;
} ares_afd_handle_t;

static void ares_afd_handle_destroy(void *arg)
{
  ares_afd_handle_t *hnd = arg;
  if (hnd != NULL && hnd->afd_handle != NULL) {
    CloseHandle(hnd->afd_handle);
  }
  ares_free(hnd);
}

static int ares_afd_handle_cmp(const void *data1, const void *data2)
{
  const ares_afd_handle_t *hnd1 = data1;
  const ares_afd_handle_t *hnd2 = data2;

  if (hnd1->poll_cnt > hnd2->poll_cnt) {
    return 1;
  }
  if (hnd1->poll_cnt < hnd2->poll_cnt) {
    return -1;
  }
  return 0;
}

static void fill_object_attributes(OBJECT_ATTRIBUTES *attr,
                                   UNICODE_STRING *name, ULONG attributes)
{
  memset(attr, 0, sizeof(*attr));
  attr->Length     = sizeof(*attr);
  attr->ObjectName = name;
  attr->Attributes = attributes;
}

#  define UNICODE_STRING_CONSTANT(s) \
    { (sizeof(s) - 1) * sizeof(wchar_t), sizeof(s) * sizeof(wchar_t), L##s }

static ares__slist_node_t *ares_afd_handle_create(ares_evsys_win32_t *ew)
{
  UNICODE_STRING     afd_device_name = UNICODE_STRING_CONSTANT("\\Device\\Afd");
  OBJECT_ATTRIBUTES  afd_attributes;
  NTSTATUS           status;
  IO_STATUS_BLOCK    iosb;
  ares_afd_handle_t *afd   = ares_malloc_zero(sizeof(*afd));
  ares__slist_node_t *node = NULL;
  if (afd == NULL) {
    goto fail;
  }

  /* Open a handle to the AFD subsystem */
  fill_object_attributes(&afd_attributes, &afd_device_name, 0);
  memset(&iosb, 0, sizeof(iosb));
  iosb.Status = STATUS_PENDING;
  status      = ew->NtCreateFile(&afd->afd_handle, SYNCHRONIZE, &afd_attributes,
                                 &iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_OPEN, 0, NULL, 0);
  if (status != STATUS_SUCCESS) {
    CARES_DEBUG_LOG("** Failed to create AFD endpoint\n");
    goto fail;
  }

  if (CreateIoCompletionPort(afd->afd_handle, ew->iocp_handle,
                             0 /* CompletionKey */, 0) == NULL) {
    goto fail;
  }

  if (!SetFileCompletionNotificationModes(afd->afd_handle,
                                          FILE_SKIP_SET_EVENT_ON_HANDLE)) {
    goto fail;
  }

  node = ares__slist_insert(ew->afd_handles, afd);
  if (node == NULL) {
    goto fail;
  }

  return node;

fail:

  ares_afd_handle_destroy(afd);
  return NULL;
}

/* Fetch the lowest poll count entry, but if it exceeds the limit, create a
 * new one and return that */
static ares__slist_node_t *ares_afd_handle_fetch(ares_evsys_win32_t *ew)
{
  ares__slist_node_t *node = ares__slist_node_first(ew->afd_handles);
  ares_afd_handle_t  *afd  = ares__slist_node_val(node);

  if (afd != NULL && afd->poll_cnt < AFD_POLL_PER_HANDLE) {
    return node;
  }

  return ares_afd_handle_create(ew);
}

static ares_bool_t ares_evsys_win32_init(ares_event_thread_t *e)
{
  ares_evsys_win32_t *ew = NULL;
  HMODULE             ntdll;

  CARES_DEBUG_LOG("** Win32 Event Init\n");

  ew = ares_malloc_zero(sizeof(*ew));
  if (ew == NULL) {
    return ARES_FALSE;
  }

  e->ev_sys_data = ew;

  /* All apps should have ntdll.dll already loaded, so just get a handle to
   * this */
  ntdll = GetModuleHandleA("ntdll.dll");
  if (ntdll == NULL) {
    goto fail;
  }

#  ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wpedantic"
/* Without the (void *) cast we get:
 *  warning: cast between incompatible function types from 'FARPROC' {aka 'long
 * long int (*)()'} to 'NTSTATUS (*)(...)'} [-Wcast-function-type] but with it
 * we get: warning: ISO C forbids conversion of function pointer to object
 * pointer type [-Wpedantic] look unsolvable short of killing the warning.
 */
#  endif

  /* Load Internal symbols not typically accessible */
  ew->NtCreateFile =
    (NtCreateFile_t)(void *)GetProcAddress(ntdll, "NtCreateFile");
  ew->NtDeviceIoControlFile = (NtDeviceIoControlFile_t)(void *)GetProcAddress(
    ntdll, "NtDeviceIoControlFile");
  ew->NtCancelIoFileEx =
    (NtCancelIoFileEx_t)(void *)GetProcAddress(ntdll, "NtCancelIoFileEx");

#  ifdef __GNUC__
#    pragma GCC diagnostic pop
#  endif

  if (ew->NtCreateFile == NULL || ew->NtCancelIoFileEx == NULL ||
      ew->NtDeviceIoControlFile == NULL) {
    goto fail;
  }

  ew->iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (ew->iocp_handle == NULL) {
    goto fail;
  }

  ew->afd_handles = ares__slist_create(
    e->channel->rand_state, ares_afd_handle_cmp, ares_afd_handle_destroy);
  if (ew->afd_handles == NULL) {
    goto fail;
  }

  /* Create at least the first afd handle, so we know of any critical system
   * issues during startup */
  if (ares_afd_handle_create(ew) == NULL) {
    goto fail;
  }

  e->ev_signal = ares_iocpevent_create(e);
  if (e->ev_signal == NULL) {
    goto fail;
  }

  ew->sockets = ares__htable_vpvp_create(NULL, NULL);
  if (ew->sockets == NULL) {
    goto fail;
  }

  return ARES_TRUE;

fail:
  ares_evsys_win32_destroy(e);
  return ARES_FALSE;
}

static ares_socket_t ares_evsys_win32_basesocket(ares_socket_t socket)
{
  while (1) {
    DWORD         bytes; /* Not used */
    ares_socket_t base_socket = ARES_SOCKET_BAD;
    int           rv;

    rv = WSAIoctl(socket, SIO_BASE_HANDLE, NULL, 0, &base_socket,
                  sizeof(base_socket), &bytes, NULL, NULL);
    if (rv != SOCKET_ERROR && base_socket != ARES_SOCKET_BAD) {
      socket = base_socket;
      break;
    }

    /* If we're here, an error occurred */
    if (GetLastError() == WSAENOTSOCK) {
      /* This is critical, exit */
      return ARES_SOCKET_BAD;
    }

    /* Work around known bug in Komodia based LSPs, use ARES_BSP_HANDLE_POLL
     * to retrieve the underlying socket to then loop and get the base socket:
     *  https://docs.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls
     *  https://www.komodia.com/newwiki/index.php?title=Komodia%27s_Redirector_bug_fixes#Version_2.2.2.6
     */
    base_socket = ARES_SOCKET_BAD;
    rv          = WSAIoctl(socket, SIO_BSP_HANDLE_POLL, NULL, 0, &base_socket,
                           sizeof(base_socket), &bytes, NULL, NULL);

    if (rv != SOCKET_ERROR && base_socket != ARES_SOCKET_BAD &&
        base_socket != socket) {
      socket = base_socket;
      continue; /* loop! */
    }

    return ARES_SOCKET_BAD;
  }

  return socket;
}

static ares_bool_t ares_evsys_win32_afd_enqueue(ares_event_t      *event,
                                                ares_event_flags_t flags)
{
  ares_event_thread_t          *e  = event->e;
  ares_evsys_win32_t           *ew = e->ev_sys_data;
  ares_evsys_win32_eventdata_t *ed = event->data;
  ares_afd_handle_t            *afd;
  NTSTATUS                      status;

  if (e == NULL || ed == NULL || ew == NULL) {
    return ARES_FALSE;
  }

  /* Misuse */
  if (ed->poll_status != POLL_STATUS_NONE) {
    return ARES_FALSE;
  }

  ed->afd_handle_node = ares_afd_handle_fetch(ew);
  /* System resource issue? */
  if (ed->afd_handle_node == NULL) {
    return ARES_FALSE;
  }

  afd = ares__slist_node_val(ed->afd_handle_node);

  /* Enqueue AFD Poll */
  ed->afd_poll_info.Exclusive         = FALSE;
  ed->afd_poll_info.NumberOfHandles   = 1;
  ed->afd_poll_info.Timeout.QuadPart  = LLONG_MAX;
  ed->afd_poll_info.Handles[0].Handle = (HANDLE)ed->base_socket;
  ed->afd_poll_info.Handles[0].Status = 0;
  ed->afd_poll_info.Handles[0].Events = AFD_POLL_LOCAL_CLOSE;

  if (flags & ARES_EVENT_FLAG_READ) {
    ed->afd_poll_info.Handles[0].Events |=
      (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT |
       AFD_POLL_ABORT);
  }
  if (flags & ARES_EVENT_FLAG_WRITE) {
    ed->afd_poll_info.Handles[0].Events |=
      (AFD_POLL_SEND | AFD_POLL_CONNECT_FAIL);
  }
  if (flags == 0) {
    ed->afd_poll_info.Handles[0].Events |= AFD_POLL_DISCONNECT;
  }

  memset(&ed->iosb, 0, sizeof(ed->iosb));
  ed->iosb.Status = STATUS_PENDING;

  status = ew->NtDeviceIoControlFile(
    afd->afd_handle, NULL, NULL, &ed->iosb, &ed->iosb, IOCTL_AFD_POLL,
    &ed->afd_poll_info, sizeof(ed->afd_poll_info), &ed->afd_poll_info,
    sizeof(ed->afd_poll_info));
  if (status != STATUS_SUCCESS && status != STATUS_PENDING) {
    CARES_DEBUG_LOG("** afd_enqueue ed=%p FAILED\n", (void *)ed);
    ed->afd_handle_node = NULL;
    return ARES_FALSE;
  }

  /* Record that we submitted a poll request to this handle and tell it to
   * re-sort the node since we changed its sort value */
  afd->poll_cnt++;
  ares__slist_node_reinsert(ed->afd_handle_node);

  ed->poll_status = POLL_STATUS_PENDING;
  CARES_DEBUG_LOG("++ afd_enqueue ed=%p flags=%X\n", (void *)ed,
                  (unsigned int)flags);
  return ARES_TRUE;
}

static ares_bool_t ares_evsys_win32_afd_cancel(ares_evsys_win32_eventdata_t *ed)
{
  IO_STATUS_BLOCK     cancel_iosb;
  ares_evsys_win32_t *ew;
  NTSTATUS            status;
  ares_afd_handle_t  *afd;

  ew = ed->event->e->ev_sys_data;

  /* Misuse */
  if (ed->poll_status != POLL_STATUS_PENDING) {
    return ARES_FALSE;
  }

  afd = ares__slist_node_val(ed->afd_handle_node);

  /* Misuse */
  if (afd == NULL) {
    return ARES_FALSE;
  }

  ed->poll_status = POLL_STATUS_CANCEL;

  /* Not pending, nothing to do. Most likely that means there is a pending
   * event that hasn't yet been delivered otherwise it would be re-armed
   * already */
  if (ed->iosb.Status != STATUS_PENDING) {
    CARES_DEBUG_LOG("** cancel not needed for ed=%p\n", (void *)ed);
    return ARES_FALSE;
  }

  status = ew->NtCancelIoFileEx(afd->afd_handle, &ed->iosb, &cancel_iosb);

  CARES_DEBUG_LOG("** Enqueued cancel for ed=%p, status = %lX\n", (void *)ed,
                  status);

  /* NtCancelIoFileEx() may return STATUS_NOT_FOUND if the operation completed
   * just before calling NtCancelIoFileEx(), but we have not yet received the
   * notifiction (but it should be queued for the next IOCP event).  */
  if (status == STATUS_SUCCESS || status == STATUS_NOT_FOUND) {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

static void ares_evsys_win32_eventdata_destroy(ares_evsys_win32_t           *ew,
                                               ares_evsys_win32_eventdata_t *ed)
{
  if (ew == NULL || ed == NULL) {
    return;
  }
  CARES_DEBUG_LOG("-- deleting ed=%p (%s)\n", (void *)ed,
                  (ed->socket == ARES_SOCKET_BAD) ? "data" : "socket");
  /* These type of handles are deferred destroy. Update tracking. */
  if (ed->socket != ARES_SOCKET_BAD) {
    ares__htable_vpvp_remove(ew->sockets, &ed->iosb);
  }

  ares__thread_mutex_destroy(ed->lock);

  if (ed->event != NULL) {
    ed->event->data = NULL;
  }

  ares_free(ed);
}

static ares_bool_t ares_evsys_win32_event_add(ares_event_t *event)
{
  ares_event_thread_t          *e  = event->e;
  ares_evsys_win32_t           *ew = e->ev_sys_data;
  ares_evsys_win32_eventdata_t *ed;
  ares_bool_t                   rc = ARES_FALSE;

  ed              = ares_malloc_zero(sizeof(*ed));
  ed->event       = event;
  ed->socket      = event->fd;
  ed->base_socket = ARES_SOCKET_BAD;
  event->data     = ed;

  CARES_DEBUG_LOG("++ add ed=%p (%s) flags=%X\n", (void *)ed,
                  (ed->socket == ARES_SOCKET_BAD) ? "data" : "socket",
                  (unsigned int)event->flags);

  /* Likely a signal event, not something we will directly handle.  We create
   * the ares_evsys_win32_eventdata_t as the placeholder to use as the
   * IOCP Completion Key */
  if (ed->socket == ARES_SOCKET_BAD) {
    ed->lock = ares__thread_mutex_create();
    if (ed->lock == NULL) {
      goto done;
    }
    rc = ARES_TRUE;
    goto done;
  }

  ed->base_socket = ares_evsys_win32_basesocket(ed->socket);
  if (ed->base_socket == ARES_SOCKET_BAD) {
    goto done;
  }

  if (!ares__htable_vpvp_insert(ew->sockets, &ed->iosb, ed)) {
    goto done;
  }

  if (!ares_evsys_win32_afd_enqueue(event, event->flags)) {
    goto done;
  }

  rc = ARES_TRUE;

done:
  if (!rc) {
    ares_evsys_win32_eventdata_destroy(ew, ed);
    event->data = NULL;
  }
  return rc;
}

static void ares_evsys_win32_event_del(ares_event_t *event)
{
  ares_evsys_win32_eventdata_t *ed = event->data;

  /* Already cleaned up, likely a LOCAL_CLOSE */
  if (ed == NULL) {
    return;
  }

  CARES_DEBUG_LOG("-- DELETE requested for ed=%p (%s)\n", (void *)ed,
                  (ed->socket != ARES_SOCKET_BAD) ? "socket" : "data");

  /*
   * Cancel pending AFD Poll operation.
   */
  if (ed->socket != ARES_SOCKET_BAD) {
    ares_evsys_win32_afd_cancel(ed);
    ed->poll_status = POLL_STATUS_DESTROY;
    ed->event       = NULL;
  } else {
    ares_evsys_win32_eventdata_destroy(event->e->ev_sys_data, ed);
  }

  event->data = NULL;
}

static void ares_evsys_win32_event_mod(ares_event_t      *event,
                                       ares_event_flags_t new_flags)
{
  ares_evsys_win32_eventdata_t *ed = event->data;

  /* Not for us */
  if (event->fd == ARES_SOCKET_BAD || ed == NULL) {
    return;
  }

  CARES_DEBUG_LOG("** mod ed=%p new_flags=%X\n", (void *)ed,
                  (unsigned int)new_flags);

  /* All we need to do is cancel the pending operation.  When the event gets
   * delivered for the cancellation, it will automatically re-enqueue a new
   * event */
  ares_evsys_win32_afd_cancel(ed);
}

static ares_bool_t ares_evsys_win32_process_other_event(
  ares_evsys_win32_t *ew, ares_evsys_win32_eventdata_t *ed, size_t i)
{
  ares_event_t *event;

  /* NOTE: do NOT dereference 'ed' if during shutdown as this could be an
   * invalid pointer if the signal handle was cleaned up, but there was still a
   * pending event! */

  if (ew->is_shutdown) {
    CARES_DEBUG_LOG("\t\t** i=%lu, skip non-socket handle during shutdown\n",
                    (unsigned long)i);
    return ARES_FALSE;
  }

  event = ed->event;
  CARES_DEBUG_LOG("\t\t** i=%lu, ed=%p (data)\n", (unsigned long)i, (void *)ed);

  event->cb(event->e, event->fd, event->data, ARES_EVENT_FLAG_OTHER);
  return ARES_TRUE;
}

static ares_bool_t ares_evsys_win32_process_socket_event(
  ares_evsys_win32_t *ew, ares_evsys_win32_eventdata_t *ed, size_t i)
{
  ares_event_flags_t flags = 0;
  ares_event_t      *event = NULL;
  ares_afd_handle_t *afd   = NULL;

  /* Shouldn't be possible */
  if (ed == NULL) {
    CARES_DEBUG_LOG("\t\t** i=%lu, Invalid handle.\n", (unsigned long)i);
    return ARES_FALSE;
  }

  event = ed->event;

  CARES_DEBUG_LOG("\t\t** i=%lu, ed=%p (socket)\n", (unsigned long)i,
                  (void *)ed);

  /* Process events */
  if (ed->poll_status == POLL_STATUS_PENDING &&
      ed->iosb.Status == STATUS_SUCCESS &&
      ed->afd_poll_info.NumberOfHandles > 0) {
    if (ed->afd_poll_info.Handles[0].Events &
        (AFD_POLL_RECEIVE | AFD_POLL_DISCONNECT | AFD_POLL_ACCEPT |
         AFD_POLL_ABORT)) {
      flags |= ARES_EVENT_FLAG_READ;
    }
    if (ed->afd_poll_info.Handles[0].Events &
        (AFD_POLL_SEND | AFD_POLL_CONNECT_FAIL)) {
      flags |= ARES_EVENT_FLAG_WRITE;
    }
    if (ed->afd_poll_info.Handles[0].Events & AFD_POLL_LOCAL_CLOSE) {
      CARES_DEBUG_LOG("\t\t** ed=%p LOCAL CLOSE\n", (void *)ed);
      ed->poll_status = POLL_STATUS_DESTROY;
    }
  }

  CARES_DEBUG_LOG("\t\t** ed=%p, iosb status=%lX, poll_status=%d, flags=%X\n",
                  (void *)ed, (unsigned long)ed->iosb.Status,
                  (int)ed->poll_status, (unsigned int)flags);

  /* Decrement poll count for AFD handle then resort, also disassociate
   * with socket */
  afd = ares__slist_node_val(ed->afd_handle_node);
  afd->poll_cnt--;
  ares__slist_node_reinsert(ed->afd_handle_node);
  ed->afd_handle_node = NULL;

  /* Pending destroy, go ahead and kill it */
  if (ed->poll_status == POLL_STATUS_DESTROY) {
    ares_evsys_win32_eventdata_destroy(ew, ed);
    return ARES_FALSE;
  }

  ed->poll_status = POLL_STATUS_NONE;

  /* Mask flags against current desired flags.  We could have an event
   * queued that is outdated. */
  flags &= event->flags;

  /* Don't actually do anything with the event that was delivered as we are
   * in a shutdown/cleanup process.  Mostly just handling the delayed
   * destruction of sockets */
  if (ew->is_shutdown) {
    return ARES_FALSE;
  }

  /* Re-enqueue so we can get more events on the socket, we either
   * received a real event, or a cancellation notice.  Both cases we
   * re-queue using the current configured event flags.
   *
   * If we can't re-enqueue, that likely means the socket has been
   * closed, so we want to kill our reference to it
   */
  if (!ares_evsys_win32_afd_enqueue(event, event->flags)) {
    ares_evsys_win32_eventdata_destroy(ew, ed);
    return ARES_FALSE;
  }

  /* No events we recognize to deliver */
  if (flags == 0) {
    return ARES_FALSE;
  }

  event->cb(event->e, event->fd, event->data, flags);
  return ARES_TRUE;
}

static size_t ares_evsys_win32_wait(ares_event_thread_t *e,
                                    unsigned long        timeout_ms)
{
  ares_evsys_win32_t *ew = e->ev_sys_data;
  OVERLAPPED_ENTRY    entries[16];
  ULONG               maxentries = sizeof(entries) / sizeof(*entries);
  ULONG               nentries;
  BOOL                status;
  size_t              i;
  size_t              cnt  = 0;
  DWORD               tout = (timeout_ms == 0) ? INFINITE : (DWORD)timeout_ms;

  CARES_DEBUG_LOG("** Wait Enter\n");
  /* Process in a loop for as long as it fills the entire entries buffer, and
   * on subsequent attempts, ensure the timeout is 0 */
  do {
    nentries = maxentries;
    status   = GetQueuedCompletionStatusEx(ew->iocp_handle, entries, nentries,
                                           &nentries, tout, FALSE);

    /* Next loop around, we want to return instantly if there are no events to
     * be processed */
    tout = 0;

    if (!status) {
      break;
    }

    CARES_DEBUG_LOG("\t** GetQueuedCompletionStatusEx returned %lu entries\n",
                    (unsigned long)nentries);
    for (i = 0; i < (size_t)nentries; i++) {
      ares_evsys_win32_eventdata_t *ed = NULL;
      ares_bool_t                   rc;

      /* For things triggered via PostQueuedCompletionStatus() we have an
       * lpCompletionKey we can just use.  Otherwise we need to dereference the
       * pointer returned in lpOverlapped to determine the referenced
       * socket */
      if (entries[i].lpCompletionKey) {
        ed = (ares_evsys_win32_eventdata_t *)entries[i].lpCompletionKey;
        rc = ares_evsys_win32_process_other_event(ew, ed, i);
      } else {
        ed = ares__htable_vpvp_get_direct(ew->sockets, entries[i].lpOverlapped);
        rc = ares_evsys_win32_process_socket_event(ew, ed, i);
      }

      /* We processed actual events */
      if (rc) {
        cnt++;
      }
    }
  } while (nentries == maxentries);

  CARES_DEBUG_LOG("** Wait Exit\n");

  return cnt;
}

const ares_event_sys_t ares_evsys_win32 = { "win32",
                                            ares_evsys_win32_init,
                                            ares_evsys_win32_destroy,
                                            ares_evsys_win32_event_add,
                                            ares_evsys_win32_event_del,
                                            ares_evsys_win32_event_mod,
                                            ares_evsys_win32_wait };
#endif

#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
