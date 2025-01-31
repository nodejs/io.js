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
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "handle-inl.h"
#include "internal.h"
#include "req-inl.h"
#include "stream-inl.h"
#include "uv-common.h"
#include "uv.h"

#include <aclapi.h>
#include <accctrl.h>

/* A zero-size buffer for use by uv_pipe_read */
static char uv_zero_[] = "";

/* Null uv_buf_t */
static const uv_buf_t uv_null_buf_ = { 0, NULL };

/* The timeout that the pipe will wait for the remote end to write data when
 * the local ends wants to shut it down. */
static const int64_t eof_timeout = 50; /* ms */

static const int default_pending_pipe_instances = 4;

/* Pipe prefix */
static char pipe_prefix[] = "\\\\?\\pipe";
static const size_t pipe_prefix_len = sizeof(pipe_prefix) - 1;

/* IPC incoming xfer queue item. */
typedef struct {
  uv__ipc_socket_xfer_type_t xfer_type;
  uv__ipc_socket_xfer_info_t xfer_info;
  struct uv__queue member;
} uv__ipc_xfer_queue_item_t;

/* IPC frame header flags. */
/* clang-format off */
enum {
  UV__IPC_FRAME_HAS_DATA                = 0x01,
  UV__IPC_FRAME_HAS_SOCKET_XFER         = 0x02,
  UV__IPC_FRAME_XFER_IS_TCP_CONNECTION  = 0x04,
  /* These are combinations of the flags above. */
  UV__IPC_FRAME_XFER_FLAGS              = 0x06,
  UV__IPC_FRAME_VALID_FLAGS             = 0x07
};
/* clang-format on */

/* IPC frame header. */
typedef struct {
  uint32_t flags;
  uint32_t reserved1;   /* Ignored. */
  uint32_t data_length; /* Must be zero if there is no data. */
  uint32_t reserved2;   /* Must be zero. */
} uv__ipc_frame_header_t;

/* To implement the IPC protocol correctly, these structures must have exactly
 * the right size. */
STATIC_ASSERT(sizeof(uv__ipc_frame_header_t) == 16);
STATIC_ASSERT(sizeof(uv__ipc_socket_xfer_info_t) == 632);

/* Coalesced write request. */
typedef struct {
  uv_write_t req;       /* Internal heap-allocated write request. */
  uv_write_t* user_req; /* Pointer to user-specified uv_write_t. */
} uv__coalesced_write_t;


static void eof_timer_init(uv_pipe_t* pipe);
static void eof_timer_start(uv_pipe_t* pipe);
static void eof_timer_stop(uv_pipe_t* pipe);
static void eof_timer_cb(uv_timer_t* timer);
static void eof_timer_destroy(uv_pipe_t* pipe);
static void eof_timer_close_cb(uv_handle_t* handle);


/* Does the file path contain embedded nul bytes? */
static int includes_nul(const char *s, size_t n) {
  if (n == 0)
    return 0;
  return NULL != memchr(s, '\0', n);
}


static void uv__unique_pipe_name(unsigned long long ptr, char* name, size_t size) {
  snprintf(name, size, "\\\\?\\pipe\\uv\\%llu-%lu", ptr, GetCurrentProcessId());
}


int uv_pipe_init(uv_loop_t* loop, uv_pipe_t* handle, int ipc) {
  uv__stream_init(loop, (uv_stream_t*)handle, UV_NAMED_PIPE);

  handle->reqs_pending = 0;
  handle->handle = INVALID_HANDLE_VALUE;
  handle->name = NULL;
  handle->pipe.conn.ipc_remote_pid = 0;
  handle->pipe.conn.ipc_data_frame.payload_remaining = 0;
  uv__queue_init(&handle->pipe.conn.ipc_xfer_queue);
  handle->pipe.conn.ipc_xfer_queue_length = 0;
  handle->ipc = ipc;
  handle->pipe.conn.non_overlapped_writes_tail = NULL;

  return 0;
}


static void uv__pipe_connection_init(uv_pipe_t* handle) {
  assert(!(handle->flags & UV_HANDLE_PIPESERVER));
  uv__connection_init((uv_stream_t*) handle);
  handle->read_req.data = handle;
  handle->pipe.conn.eof_timer = NULL;
}


static HANDLE open_named_pipe(const WCHAR* name, DWORD* duplex_flags) {
  HANDLE pipeHandle;

  /*
   * Assume that we have a duplex pipe first, so attempt to
   * connect with GENERIC_READ | GENERIC_WRITE.
   */
  pipeHandle = CreateFileW(name,
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED,
                           NULL);
  if (pipeHandle != INVALID_HANDLE_VALUE) {
    *duplex_flags = UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;
    return pipeHandle;
  }

  /*
   * If the pipe is not duplex CreateFileW fails with
   * ERROR_ACCESS_DENIED.  In that case try to connect
   * as a read-only or write-only.
   */
  if (GetLastError() == ERROR_ACCESS_DENIED) {
    pipeHandle = CreateFileW(name,
                             GENERIC_READ | FILE_WRITE_ATTRIBUTES,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED,
                             NULL);

    if (pipeHandle != INVALID_HANDLE_VALUE) {
      *duplex_flags = UV_HANDLE_READABLE;
      return pipeHandle;
    }
  }

  if (GetLastError() == ERROR_ACCESS_DENIED) {
    pipeHandle = CreateFileW(name,
                             GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED,
                             NULL);

    if (pipeHandle != INVALID_HANDLE_VALUE) {
      *duplex_flags = UV_HANDLE_WRITABLE;
      return pipeHandle;
    }
  }

  return INVALID_HANDLE_VALUE;
}


static void close_pipe(uv_pipe_t* pipe) {
  assert(pipe->u.fd == -1 || pipe->u.fd > 2);
  if (pipe->u.fd == -1)
    CloseHandle(pipe->handle);
  else
    _close(pipe->u.fd);

  pipe->u.fd = -1;
  pipe->handle = INVALID_HANDLE_VALUE;
}


static int uv__pipe_server(
    HANDLE* pipeHandle_ptr, DWORD access,
    char* name, size_t nameSize, unsigned long long random) {
  HANDLE pipeHandle;
  int err;

  for (;;) {
    uv__unique_pipe_name(random, name, nameSize);

    pipeHandle = CreateNamedPipeA(name,
      access | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0,
      NULL);

    if (pipeHandle != INVALID_HANDLE_VALUE) {
      /* No name collisions.  We're done. */
      break;
    }

    err = GetLastError();
    if (err != ERROR_PIPE_BUSY && err != ERROR_ACCESS_DENIED) {
      goto error;
    }

    /* Pipe name collision.  Increment the random number and try again. */
    random++;
  }

  *pipeHandle_ptr = pipeHandle;

  return 0;

 error:
  if (pipeHandle != INVALID_HANDLE_VALUE)
    CloseHandle(pipeHandle);

  return err;
}


static int uv__create_pipe_pair(
    HANDLE* server_pipe_ptr, HANDLE* client_pipe_ptr,
    unsigned int server_flags, unsigned int client_flags,
    int inherit_client, unsigned long long random) {
  /* allowed flags are: UV_READABLE_PIPE | UV_WRITABLE_PIPE | UV_NONBLOCK_PIPE */
  char pipe_name[64];
  SECURITY_ATTRIBUTES sa;
  DWORD server_access;
  DWORD client_access;
  HANDLE server_pipe;
  HANDLE client_pipe;
  int err;

  server_pipe = INVALID_HANDLE_VALUE;
  client_pipe = INVALID_HANDLE_VALUE;

  server_access = 0;
  if (server_flags & UV_READABLE_PIPE)
    server_access |= PIPE_ACCESS_INBOUND;
  if (server_flags & UV_WRITABLE_PIPE)
    server_access |= PIPE_ACCESS_OUTBOUND;
  if (server_flags & UV_NONBLOCK_PIPE)
    server_access |= FILE_FLAG_OVERLAPPED;
  server_access |= WRITE_DAC;

  client_access = 0;
  if (client_flags & UV_READABLE_PIPE)
    client_access |= GENERIC_READ;
  else
    client_access |= FILE_READ_ATTRIBUTES;
  if (client_flags & UV_WRITABLE_PIPE)
    client_access |= GENERIC_WRITE;
  else
    client_access |= FILE_WRITE_ATTRIBUTES;
  client_access |= WRITE_DAC;

  /* Create server pipe handle. */
  err = uv__pipe_server(&server_pipe,
                        server_access,
                        pipe_name,
                        sizeof(pipe_name),
                        random);
  if (err)
    goto error;

  /* Create client pipe handle. */
  sa.nLength = sizeof sa;
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = inherit_client;

  client_pipe = CreateFileA(pipe_name,
                            client_access,
                            0,
                            &sa,
                            OPEN_EXISTING,
                            (client_flags & UV_NONBLOCK_PIPE) ? FILE_FLAG_OVERLAPPED : 0,
                            NULL);
  if (client_pipe == INVALID_HANDLE_VALUE) {
    err = GetLastError();
    goto error;
  }

#ifndef NDEBUG
  /* Validate that the pipe was opened in the right mode. */
  {
    DWORD mode;
    BOOL r;
    r = GetNamedPipeHandleState(client_pipe, &mode, NULL, NULL, NULL, NULL, 0);
    if (r == TRUE) {
      assert(mode == (PIPE_READMODE_BYTE | PIPE_WAIT));
    } else {
      fprintf(stderr, "libuv assertion failure: GetNamedPipeHandleState failed\n");
    }
  }
#endif

  /* Do a blocking ConnectNamedPipe.  This should not block because we have
   * both ends of the pipe created. */
  if (!ConnectNamedPipe(server_pipe, NULL)) {
    if (GetLastError() != ERROR_PIPE_CONNECTED) {
      err = GetLastError();
      goto error;
    }
  }

  *client_pipe_ptr = client_pipe;
  *server_pipe_ptr = server_pipe;
  return 0;

 error:
  if (server_pipe != INVALID_HANDLE_VALUE)
    CloseHandle(server_pipe);

  if (client_pipe != INVALID_HANDLE_VALUE)
    CloseHandle(client_pipe);

  return err;
}


int uv_pipe(uv_file fds[2], int read_flags, int write_flags) {
  uv_file temp[2];
  int err;
  HANDLE readh;
  HANDLE writeh;

  /* Make the server side the inbound (read) end, */
  /* so that both ends will have FILE_READ_ATTRIBUTES permission. */
  /* TODO: better source of local randomness than &fds? */
  read_flags |= UV_READABLE_PIPE;
  write_flags |= UV_WRITABLE_PIPE;
  err = uv__create_pipe_pair(&readh,
                             &writeh,
                             read_flags,
                             write_flags,
                             0,
                             (uintptr_t) &fds[0]);
  if (err != 0)
    return err;
  temp[0] = _open_osfhandle((intptr_t) readh, 0);
  if (temp[0] == -1) {
    if (errno == UV_EMFILE)
      err = UV_EMFILE;
    else
      err = UV_UNKNOWN;
    CloseHandle(readh);
    CloseHandle(writeh);
    return err;
  }
  temp[1] = _open_osfhandle((intptr_t) writeh, 0);
  if (temp[1] == -1) {
    if (errno == UV_EMFILE)
      err = UV_EMFILE;
    else
      err = UV_UNKNOWN;
    _close(temp[0]);
    CloseHandle(writeh);
    return err;
  }
  fds[0] = temp[0];
  fds[1] = temp[1];
  return 0;
}


int uv__create_stdio_pipe_pair(uv_loop_t* loop,
    uv_pipe_t* parent_pipe, HANDLE* child_pipe_ptr, unsigned int flags) {
  /* The parent_pipe is always the server_pipe and kept by libuv.
   * The child_pipe is always the client_pipe and is passed to the child.
   * The flags are specified with respect to their usage in the child. */
  HANDLE server_pipe;
  HANDLE client_pipe;
  unsigned int server_flags;
  unsigned int client_flags;
  int err;

  uv__pipe_connection_init(parent_pipe);

  server_pipe = INVALID_HANDLE_VALUE;
  client_pipe = INVALID_HANDLE_VALUE;

  server_flags = 0;
  client_flags = 0;
  if (flags & UV_READABLE_PIPE) {
    /* The server needs inbound (read) access too, otherwise CreateNamedPipe()
     * won't give us the FILE_READ_ATTRIBUTES permission. We need that to probe
     * the state of the write buffer when we're trying to shutdown the pipe. */
    server_flags |= UV_READABLE_PIPE | UV_WRITABLE_PIPE;
    client_flags |= UV_READABLE_PIPE;
  }
  if (flags & UV_WRITABLE_PIPE) {
    server_flags |= UV_READABLE_PIPE;
    client_flags |= UV_WRITABLE_PIPE;
  }
  server_flags |= UV_NONBLOCK_PIPE;
  if (flags & UV_NONBLOCK_PIPE || parent_pipe->ipc) {
    client_flags |= UV_NONBLOCK_PIPE;
  }

  err = uv__create_pipe_pair(&server_pipe, &client_pipe,
          server_flags, client_flags, 1, (uintptr_t) server_pipe);
  if (err)
    goto error;

  if (CreateIoCompletionPort(server_pipe,
                             loop->iocp,
                             (ULONG_PTR) parent_pipe,
                             0) == NULL) {
    err = GetLastError();
    goto error;
  }

  parent_pipe->handle = server_pipe;
  *child_pipe_ptr = client_pipe;

  /* The server end is now readable and/or writable. */
  if (flags & UV_READABLE_PIPE)
    parent_pipe->flags |= UV_HANDLE_WRITABLE;
  if (flags & UV_WRITABLE_PIPE)
    parent_pipe->flags |= UV_HANDLE_READABLE;

  return 0;

 error:
  if (server_pipe != INVALID_HANDLE_VALUE)
    CloseHandle(server_pipe);

  if (client_pipe != INVALID_HANDLE_VALUE)
    CloseHandle(client_pipe);

  return err;
}


static int uv__set_pipe_handle(uv_loop_t* loop,
                               uv_pipe_t* handle,
                               HANDLE pipeHandle,
                               int fd,
                               DWORD duplex_flags) {
  NTSTATUS nt_status;
  IO_STATUS_BLOCK io_status;
  FILE_MODE_INFORMATION mode_info;
  DWORD mode = PIPE_READMODE_BYTE | PIPE_WAIT;
  DWORD current_mode = 0;
  DWORD err = 0;

  assert(handle->flags & UV_HANDLE_CONNECTION);
  assert(!(handle->flags & UV_HANDLE_PIPESERVER));
  if (handle->flags & UV_HANDLE_CLOSING)
    return UV_EINVAL;
  if (handle->handle != INVALID_HANDLE_VALUE)
    return UV_EBUSY;

  if (!SetNamedPipeHandleState(pipeHandle, &mode, NULL, NULL)) {
    err = GetLastError();
    if (err == ERROR_ACCESS_DENIED) {
      /*
       * SetNamedPipeHandleState can fail if the handle doesn't have either
       * GENERIC_WRITE  or FILE_WRITE_ATTRIBUTES.
       * But if the handle already has the desired wait and blocking modes
       * we can continue.
       */
      if (!GetNamedPipeHandleState(pipeHandle, &current_mode, NULL, NULL,
                                   NULL, NULL, 0)) {
        return uv_translate_sys_error(GetLastError());
      } else if (current_mode & PIPE_NOWAIT) {
        return UV_EACCES;
      }
    } else {
      /* If this returns ERROR_INVALID_PARAMETER we probably opened
       * something that is not a pipe. */
      if (err == ERROR_INVALID_PARAMETER) {
        return UV_ENOTSOCK;
      }
      return uv_translate_sys_error(err);
    }
  }

  /* Check if the pipe was created with FILE_FLAG_OVERLAPPED. */
  nt_status = pNtQueryInformationFile(pipeHandle,
                                      &io_status,
                                      &mode_info,
                                      sizeof(mode_info),
                                      FileModeInformation);
  if (nt_status != STATUS_SUCCESS) {
    return uv_translate_sys_error(err);
  }

  if (mode_info.Mode & FILE_SYNCHRONOUS_IO_ALERT ||
      mode_info.Mode & FILE_SYNCHRONOUS_IO_NONALERT) {
    /* Non-overlapped pipe. */
    handle->flags |= UV_HANDLE_NON_OVERLAPPED_PIPE;
    handle->pipe.conn.readfile_thread_handle = NULL;
    InitializeCriticalSection(&handle->pipe.conn.readfile_thread_lock);
  } else {
    /* Overlapped pipe.  Try to associate with IOCP. */
    if (CreateIoCompletionPort(pipeHandle,
                               loop->iocp,
                               (ULONG_PTR) handle,
                               0) == NULL) {
      handle->flags |= UV_HANDLE_EMULATE_IOCP;
    }
  }

  handle->handle = pipeHandle;
  handle->u.fd = fd;
  handle->flags |= duplex_flags;

  return 0;
}


static int pipe_alloc_accept(uv_loop_t* loop, uv_pipe_t* handle,
                             uv_pipe_accept_t* req, BOOL firstInstance) {
  assert(req->pipeHandle == INVALID_HANDLE_VALUE);

  req->pipeHandle =
      CreateNamedPipeW(handle->name,
                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | WRITE_DAC |
                         (firstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                       PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, NULL);

  if (req->pipeHandle == INVALID_HANDLE_VALUE) {
    return 0;
  }

  /* Associate it with IOCP so we can get events. */
  if (CreateIoCompletionPort(req->pipeHandle,
                             loop->iocp,
                             (ULONG_PTR) handle,
                             0) == NULL) {
    uv_fatal_error(GetLastError(), "CreateIoCompletionPort");
  }

  /* Stash a handle in the server object for use from places such as
   * getsockname and chmod. As we transfer ownership of these to client
   * objects, we'll allocate new ones here. */
  handle->handle = req->pipeHandle;

  return 1;
}


static DWORD WINAPI pipe_shutdown_thread_proc(void* parameter) {
  uv_loop_t* loop;
  uv_pipe_t* handle;
  uv_shutdown_t* req;

  req = (uv_shutdown_t*) parameter;
  assert(req);
  handle = (uv_pipe_t*) req->handle;
  assert(handle);
  loop = handle->loop;
  assert(loop);

  FlushFileBuffers(handle->handle);

  /* Post completed */
  POST_COMPLETION_FOR_REQ(loop, req);

  return 0;
}


void uv__pipe_shutdown(uv_loop_t* loop, uv_pipe_t* handle, uv_shutdown_t *req) {
  DWORD result;
  NTSTATUS nt_status;
  IO_STATUS_BLOCK io_status;
  FILE_PIPE_LOCAL_INFORMATION pipe_info;

  assert(handle->flags & UV_HANDLE_CONNECTION);
  assert(req != NULL);
  assert(handle->stream.conn.write_reqs_pending == 0);
  SET_REQ_SUCCESS(req);

  if (handle->flags & UV_HANDLE_CLOSING) {
    uv__insert_pending_req(loop, (uv_req_t*) req);
    return;
  }

  /* Try to avoid flushing the pipe buffer in the thread pool. */
  nt_status = pNtQueryInformationFile(handle->handle,
                                      &io_status,
                                      &pipe_info,
                                      sizeof pipe_info,
                                      FilePipeLocalInformation);

  if (nt_status != STATUS_SUCCESS) {
    SET_REQ_ERROR(req, pRtlNtStatusToDosError(nt_status));
    handle->flags |= UV_HANDLE_WRITABLE; /* Questionable. */
    uv__insert_pending_req(loop, (uv_req_t*) req);
    return;
  }

  if (pipe_info.OutboundQuota == pipe_info.WriteQuotaAvailable) {
    /* Short-circuit, no need to call FlushFileBuffers:
     * all writes have been read. */
    uv__insert_pending_req(loop, (uv_req_t*) req);
    return;
  }

  /* Run FlushFileBuffers in the thread pool. */
  result = QueueUserWorkItem(pipe_shutdown_thread_proc,
                             req,
                             WT_EXECUTELONGFUNCTION);
  if (!result) {
    SET_REQ_ERROR(req, GetLastError());
    handle->flags |= UV_HANDLE_WRITABLE; /* Questionable. */
    uv__insert_pending_req(loop, (uv_req_t*) req);
    return;
  }
}


void uv__pipe_endgame(uv_loop_t* loop, uv_pipe_t* handle) {
  uv__ipc_xfer_queue_item_t* xfer_queue_item;

  assert(handle->reqs_pending == 0);
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));

  if (handle->flags & UV_HANDLE_CONNECTION) {
    /* Free pending sockets */
    while (!uv__queue_empty(&handle->pipe.conn.ipc_xfer_queue)) {
      struct uv__queue* q;
      SOCKET socket;

      q = uv__queue_head(&handle->pipe.conn.ipc_xfer_queue);
      uv__queue_remove(q);
      xfer_queue_item = uv__queue_data(q, uv__ipc_xfer_queue_item_t, member);

      /* Materialize socket and close it */
      socket = WSASocketW(FROM_PROTOCOL_INFO,
                          FROM_PROTOCOL_INFO,
                          FROM_PROTOCOL_INFO,
                          &xfer_queue_item->xfer_info.socket_info,
                          0,
                          WSA_FLAG_OVERLAPPED);
      uv__free(xfer_queue_item);

      if (socket != INVALID_SOCKET)
        closesocket(socket);
    }
    handle->pipe.conn.ipc_xfer_queue_length = 0;

    assert(handle->read_req.wait_handle == INVALID_HANDLE_VALUE);
    if (handle->read_req.event_handle != NULL) {
      CloseHandle(handle->read_req.event_handle);
      handle->read_req.event_handle = NULL;
    }

    if (handle->flags & UV_HANDLE_NON_OVERLAPPED_PIPE)
      DeleteCriticalSection(&handle->pipe.conn.readfile_thread_lock);
  }

  if (handle->flags & UV_HANDLE_PIPESERVER) {
    assert(handle->pipe.serv.accept_reqs);
    uv__free(handle->pipe.serv.accept_reqs);
    handle->pipe.serv.accept_reqs = NULL;
  }

  uv__handle_close(handle);
}


void uv_pipe_pending_instances(uv_pipe_t* handle, int count) {
  if (handle->flags & UV_HANDLE_BOUND)
    return;
  handle->pipe.serv.pending_instances = count;
  handle->flags |= UV_HANDLE_PIPESERVER;
}


/* Creates a pipe server. */
int uv_pipe_bind(uv_pipe_t* handle, const char* name) {
  return uv_pipe_bind2(handle, name, strlen(name), 0);
}


int uv_pipe_bind2(uv_pipe_t* handle,
                  const char* name,
                  size_t namelen,
                  unsigned int flags) {
  uv_loop_t* loop = handle->loop;
  int i, err;
  uv_pipe_accept_t* req;
  char* name_copy;

  if (flags & ~UV_PIPE_NO_TRUNCATE) {
    return UV_EINVAL;
  }

  if (name == NULL) {
    return UV_EINVAL;
  }

  if (namelen == 0) {
    return UV_EINVAL;
  }

  if (includes_nul(name, namelen)) {
    return UV_EINVAL;
  }

  if (handle->flags & UV_HANDLE_BOUND) {
    return UV_EINVAL;
  }

  if (uv__is_closing(handle)) {
    return UV_EINVAL;
  }

  name_copy = uv__malloc(namelen + 1);
  if (name_copy == NULL) {
    return UV_ENOMEM;
  }

  memcpy(name_copy, name, namelen);
  name_copy[namelen] = '\0';

  if (!(handle->flags & UV_HANDLE_PIPESERVER)) {
    handle->pipe.serv.pending_instances = default_pending_pipe_instances;
  }

  err = UV_ENOMEM;
  handle->pipe.serv.accept_reqs = (uv_pipe_accept_t*)
    uv__malloc(sizeof(uv_pipe_accept_t) * handle->pipe.serv.pending_instances);
  if (handle->pipe.serv.accept_reqs == NULL) {
    goto error;
  }

  for (i = 0; i < handle->pipe.serv.pending_instances; i++) {
    req = &handle->pipe.serv.accept_reqs[i];
    UV_REQ_INIT(req, UV_ACCEPT);
    req->data = handle;
    req->pipeHandle = INVALID_HANDLE_VALUE;
    req->next_pending = NULL;
  }

  /* TODO(bnoordhuis) Add converters that take a |length| parameter. */
  err = uv__convert_utf8_to_utf16(name_copy, &handle->name);
  uv__free(name_copy);
  name_copy = NULL;

  if (err) {
    goto error;
  }

  /*
   * Attempt to create the first pipe with FILE_FLAG_FIRST_PIPE_INSTANCE.
   * If this fails then there's already a pipe server for the given pipe name.
   */
  if (!pipe_alloc_accept(loop,
                         handle,
                         &handle->pipe.serv.accept_reqs[0],
                         TRUE)) {
    err = GetLastError();
    if (err == ERROR_ACCESS_DENIED) {
      err = UV_EADDRINUSE;
    } else if (err == ERROR_PATH_NOT_FOUND || err == ERROR_INVALID_NAME) {
      err = UV_EACCES;
    } else {
      err = uv_translate_sys_error(err);
    }
    goto error;
  }

  handle->pipe.serv.pending_accepts = NULL;
  handle->flags |= UV_HANDLE_PIPESERVER;
  handle->flags |= UV_HANDLE_BOUND;

  return 0;

error:
  uv__free(handle->pipe.serv.accept_reqs);
  uv__free(handle->name);
  uv__free(name_copy);
  handle->pipe.serv.accept_reqs = NULL;
  handle->name = NULL;

  return err;
}


static DWORD WINAPI pipe_connect_thread_proc(void* parameter) {
  uv_loop_t* loop;
  uv_pipe_t* handle;
  uv_connect_t* req;
  HANDLE pipeHandle = INVALID_HANDLE_VALUE;
  DWORD duplex_flags;

  req = (uv_connect_t*) parameter;
  assert(req);
  handle = (uv_pipe_t*) req->handle;
  assert(handle);
  loop = handle->loop;
  assert(loop);

  /* We're here because CreateFile on a pipe returned ERROR_PIPE_BUSY. We wait
   * up to 30 seconds for the pipe to become available with WaitNamedPipe. */
  while (WaitNamedPipeW(req->u.connect.name, 30000)) {
    /* The pipe is now available, try to connect. */
    pipeHandle = open_named_pipe(req->u.connect.name, &duplex_flags);
    if (pipeHandle != INVALID_HANDLE_VALUE)
      break;

    SwitchToThread();
  }

  uv__free(req->u.connect.name);
  req->u.connect.name = NULL;
  if (pipeHandle != INVALID_HANDLE_VALUE) {
    SET_REQ_SUCCESS(req);
    req->u.connect.pipeHandle = pipeHandle;
    req->u.connect.duplex_flags = duplex_flags;
  } else {
    SET_REQ_ERROR(req, GetLastError());
  }

  /* Post completed */
  POST_COMPLETION_FOR_REQ(loop, req);

  return 0;
}


void uv_pipe_connect(uv_connect_t* req,
                    uv_pipe_t* handle,
                    const char* name,
                    uv_connect_cb cb) {
  uv_loop_t* loop;
  int err;

  err = uv_pipe_connect2(req, handle, name, strlen(name), 0, cb);

  if (err) {
    loop = handle->loop;
    /* Make this req pending reporting an error. */
    SET_REQ_ERROR(req, err);
    uv__insert_pending_req(loop, (uv_req_t*) req);
    handle->reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle);
  }
}


int uv_pipe_connect2(uv_connect_t* req,
                     uv_pipe_t* handle,
                     const char* name,
                     size_t namelen,
                     unsigned int flags,
                     uv_connect_cb cb) {
  uv_loop_t* loop;
  int err;
  size_t nameSize;
  HANDLE pipeHandle = INVALID_HANDLE_VALUE;
  DWORD duplex_flags;
  char* name_copy;

  loop = handle->loop;
  UV_REQ_INIT(req, UV_CONNECT);
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;
  req->u.connect.pipeHandle = INVALID_HANDLE_VALUE;
  req->u.connect.duplex_flags = 0;
  req->u.connect.name = NULL;

  if (flags & ~UV_PIPE_NO_TRUNCATE) {
    return UV_EINVAL;
  }

  if (name == NULL) {
    return UV_EINVAL;
  }

  if (namelen == 0) {
    return UV_EINVAL;
  }

  if (includes_nul(name, namelen)) {
    return UV_EINVAL;
  }

  name_copy = uv__malloc(namelen + 1);
  if (name_copy == NULL) {
    return UV_ENOMEM;
  }

  memcpy(name_copy, name, namelen);
  name_copy[namelen] = '\0';

  if (handle->flags & UV_HANDLE_PIPESERVER) {
    err = ERROR_INVALID_PARAMETER;
    goto error;
  }
  if (handle->flags & UV_HANDLE_CONNECTION) {
    err = ERROR_PIPE_BUSY;
    goto error;
  }
  uv__pipe_connection_init(handle);

  /* TODO(bnoordhuis) Add converters that take a |length| parameter. */
  err = uv__convert_utf8_to_utf16(name_copy, &handle->name);
  uv__free(name_copy);
  name_copy = NULL;

  if (err) {
    err = ERROR_NO_UNICODE_TRANSLATION;
    goto error;
  }

  pipeHandle = open_named_pipe(handle->name, &duplex_flags);
  if (pipeHandle == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_PIPE_BUSY) {
      nameSize = (wcslen(handle->name) + 1) * sizeof(WCHAR);
      req->u.connect.name = uv__malloc(nameSize);
      if (!req->u.connect.name) {
        uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
      }

      memcpy(req->u.connect.name, handle->name, nameSize);

      /* Wait for the server to make a pipe instance available. */
      if (!QueueUserWorkItem(&pipe_connect_thread_proc,
                             req,
                             WT_EXECUTELONGFUNCTION)) {
        uv__free(req->u.connect.name);
        req->u.connect.name = NULL;
        err = GetLastError();
        goto error;
      }

      REGISTER_HANDLE_REQ(loop, handle);
      handle->reqs_pending++;

      return 0;
    }

    err = GetLastError();
    goto error;
  }

  req->u.connect.pipeHandle = pipeHandle;
  req->u.connect.duplex_flags = duplex_flags;
  SET_REQ_SUCCESS(req);
  uv__insert_pending_req(loop, (uv_req_t*) req);
  handle->reqs_pending++;
  REGISTER_HANDLE_REQ(loop, handle);
  return 0;

error:
  uv__free(name_copy);

  if (handle->name) {
    uv__free(handle->name);
    handle->name = NULL;
  }

  if (pipeHandle != INVALID_HANDLE_VALUE)
    CloseHandle(pipeHandle);

  /* Make this req pending reporting an error. */
  SET_REQ_ERROR(req, err);
  uv__insert_pending_req(loop, (uv_req_t*) req);
  handle->reqs_pending++;
  REGISTER_HANDLE_REQ(loop, handle);
  return 0;
}


void uv__pipe_interrupt_read(uv_pipe_t* handle) {
  BOOL r;

  if (!(handle->flags & UV_HANDLE_READ_PENDING))
    return; /* No pending reads. */
  if (handle->flags & UV_HANDLE_CANCELLATION_PENDING)
    return; /* Already cancelled. */
  if (handle->handle == INVALID_HANDLE_VALUE)
    return; /* Pipe handle closed. */

  if (!(handle->flags & UV_HANDLE_NON_OVERLAPPED_PIPE)) {
    /* Cancel asynchronous read. */
    r = CancelIoEx(handle->handle, &handle->read_req.u.io.overlapped);
    assert(r || GetLastError() == ERROR_NOT_FOUND);
    (void) r;
  } else {
    /* Cancel synchronous read (which is happening in the thread pool). */
    HANDLE thread;
    volatile HANDLE* thread_ptr = &handle->pipe.conn.readfile_thread_handle;

    EnterCriticalSection(&handle->pipe.conn.readfile_thread_lock);

    thread = *thread_ptr;
    if (thread == NULL) {
      /* The thread pool thread has not yet reached the point of blocking, we
       * can pre-empt it by setting thread_handle to INVALID_HANDLE_VALUE. */
      *thread_ptr = INVALID_HANDLE_VALUE;

    } else {
      /* Spin until the thread has acknowledged (by setting the thread to
       * INVALID_HANDLE_VALUE) that it is past the point of blocking. */
      while (thread != INVALID_HANDLE_VALUE) {
        r = CancelSynchronousIo(thread);
        assert(r || GetLastError() == ERROR_NOT_FOUND);
        SwitchToThread(); /* Yield thread. */
        thread = *thread_ptr;
      }
    }

    LeaveCriticalSection(&handle->pipe.conn.readfile_thread_lock);
  }

  /* Set flag to indicate that read has been cancelled. */
  handle->flags |= UV_HANDLE_CANCELLATION_PENDING;
}


void uv__pipe_read_stop(uv_pipe_t* handle) {
  handle->flags &= ~UV_HANDLE_READING;
  DECREASE_ACTIVE_COUNT(handle->loop, handle);
  uv__pipe_interrupt_read(handle);
}


/* Cleans up uv_pipe_t (server or connection) and all resources associated with
 * it. */
void uv__pipe_close(uv_loop_t* loop, uv_pipe_t* handle) {
  int i;
  HANDLE pipeHandle;

  if (handle->flags & UV_HANDLE_READING) {
    handle->flags &= ~UV_HANDLE_READING;
    DECREASE_ACTIVE_COUNT(loop, handle);
  }

  if (handle->flags & UV_HANDLE_LISTENING) {
    handle->flags &= ~UV_HANDLE_LISTENING;
    DECREASE_ACTIVE_COUNT(loop, handle);
  }

  handle->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);

  uv__handle_closing(handle);

  uv__pipe_interrupt_read(handle);

  if (handle->name) {
    uv__free(handle->name);
    handle->name = NULL;
  }

  if (handle->flags & UV_HANDLE_PIPESERVER) {
    for (i = 0; i < handle->pipe.serv.pending_instances; i++) {
      pipeHandle = handle->pipe.serv.accept_reqs[i].pipeHandle;
      if (pipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipeHandle);
        handle->pipe.serv.accept_reqs[i].pipeHandle = INVALID_HANDLE_VALUE;
      }
    }
    handle->handle = INVALID_HANDLE_VALUE;
  }

  if (handle->flags & UV_HANDLE_CONNECTION) {
    eof_timer_destroy(handle);
  }

  if ((handle->flags & UV_HANDLE_CONNECTION)
      && handle->handle != INVALID_HANDLE_VALUE) {
    /* This will eventually destroy the write queue for us too. */
    close_pipe(handle);
  }

  if (handle->reqs_pending == 0)
    uv__want_endgame(loop, (uv_handle_t*) handle);
}


static void uv__pipe_queue_accept(uv_loop_t* loop, uv_pipe_t* handle,
    uv_pipe_accept_t* req, BOOL firstInstance) {
  assert(handle->flags & UV_HANDLE_LISTENING);

  if (!firstInstance && !pipe_alloc_accept(loop, handle, req, FALSE)) {
    SET_REQ_ERROR(req, GetLastError());
    uv__insert_pending_req(loop, (uv_req_t*) req);
    handle->reqs_pending++;
    return;
  }

  assert(req->pipeHandle != INVALID_HANDLE_VALUE);

  /* Prepare the overlapped structure. */
  memset(&(req->u.io.overlapped), 0, sizeof(req->u.io.overlapped));

  if (!ConnectNamedPipe(req->pipeHandle, &req->u.io.overlapped) &&
      GetLastError() != ERROR_IO_PENDING) {
    if (GetLastError() == ERROR_PIPE_CONNECTED) {
      SET_REQ_SUCCESS(req);
    } else {
      CloseHandle(req->pipeHandle);
      req->pipeHandle = INVALID_HANDLE_VALUE;
      /* Make this req pending reporting an error. */
      SET_REQ_ERROR(req, GetLastError());
    }
    uv__insert_pending_req(loop, (uv_req_t*) req);
    handle->reqs_pending++;
    return;
  }

  /* Wait for completion via IOCP */
  handle->reqs_pending++;
}


int uv__pipe_accept(uv_pipe_t* server, uv_stream_t* client) {
  uv_loop_t* loop = server->loop;
  uv_pipe_t* pipe_client;
  uv_pipe_accept_t* req;
  struct uv__queue* q;
  uv__ipc_xfer_queue_item_t* item;
  int err;

  if (server->ipc) {
    if (uv__queue_empty(&server->pipe.conn.ipc_xfer_queue)) {
      /* No valid pending sockets. */
      return WSAEWOULDBLOCK;
    }

    q = uv__queue_head(&server->pipe.conn.ipc_xfer_queue);
    uv__queue_remove(q);
    server->pipe.conn.ipc_xfer_queue_length--;
    item = uv__queue_data(q, uv__ipc_xfer_queue_item_t, member);

    err = uv__tcp_xfer_import(
        (uv_tcp_t*) client, item->xfer_type, &item->xfer_info);

    uv__free(item);

    if (err != 0)
      return err;

  } else {
    pipe_client = (uv_pipe_t*) client;
    uv__pipe_connection_init(pipe_client);

    /* Find a connection instance that has been connected, but not yet
     * accepted. */
    req = server->pipe.serv.pending_accepts;

    if (!req) {
      /* No valid connections found, so we error out. */
      return WSAEWOULDBLOCK;
    }

    /* Initialize the client handle and copy the pipeHandle to the client */
    pipe_client->handle = req->pipeHandle;
    pipe_client->flags |= UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;

    /* Prepare the req to pick up a new connection */
    server->pipe.serv.pending_accepts = req->next_pending;
    req->next_pending = NULL;
    req->pipeHandle = INVALID_HANDLE_VALUE;

    server->handle = INVALID_HANDLE_VALUE;
    if (!(server->flags & UV_HANDLE_CLOSING)) {
      uv__pipe_queue_accept(loop, server, req, FALSE);
    }
  }

  return 0;
}


/* Starts listening for connections for the given pipe. */
int uv__pipe_listen(uv_pipe_t* handle, int backlog, uv_connection_cb cb) {
  uv_loop_t* loop = handle->loop;
  int i;

  if (handle->flags & UV_HANDLE_LISTENING) {
    handle->stream.serv.connection_cb = cb;
  }

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    return WSAEINVAL;
  }

  if (handle->flags & UV_HANDLE_READING) {
    return WSAEISCONN;
  }

  if (!(handle->flags & UV_HANDLE_PIPESERVER)) {
    return ERROR_NOT_SUPPORTED;
  }

  if (handle->ipc) {
    return WSAEINVAL;
  }

  handle->flags |= UV_HANDLE_LISTENING;
  INCREASE_ACTIVE_COUNT(loop, handle);
  handle->stream.serv.connection_cb = cb;

  /* First pipe handle should have already been created in uv_pipe_bind */
  assert(handle->pipe.serv.accept_reqs[0].pipeHandle != INVALID_HANDLE_VALUE);

  for (i = 0; i < handle->pipe.serv.pending_instances; i++) {
    uv__pipe_queue_accept(loop, handle, &handle->pipe.serv.accept_reqs[i], i == 0);
  }

  return 0;
}


static DWORD WINAPI uv_pipe_zero_readfile_thread_proc(void* arg) {
  uv_read_t* req = (uv_read_t*) arg;
  uv_pipe_t* handle = (uv_pipe_t*) req->data;
  uv_loop_t* loop = handle->loop;
  volatile HANDLE* thread_ptr = &handle->pipe.conn.readfile_thread_handle;
  CRITICAL_SECTION* lock = &handle->pipe.conn.readfile_thread_lock;
  HANDLE thread;
  DWORD bytes;
  DWORD err;

  assert(req->type == UV_READ);
  assert(handle->type == UV_NAMED_PIPE);

  err = 0;

  /* Create a handle to the current thread. */
  if (!DuplicateHandle(GetCurrentProcess(),
                       GetCurrentThread(),
                       GetCurrentProcess(),
                       &thread,
                       0,
                       FALSE,
                       DUPLICATE_SAME_ACCESS)) {
    err = GetLastError();
    goto out1;
  }

  /* The lock needs to be held when thread handle is modified. */
  EnterCriticalSection(lock);
  if (*thread_ptr == INVALID_HANDLE_VALUE) {
    /* uv__pipe_interrupt_read() cancelled reading before we got here. */
    err = ERROR_OPERATION_ABORTED;
  } else {
    /* Let main thread know which worker thread is doing the blocking read. */
    assert(*thread_ptr == NULL);
    *thread_ptr = thread;
  }
  LeaveCriticalSection(lock);

  if (err)
    goto out2;

  /* Block the thread until data is available on the pipe, or the read is
   * cancelled. */
  if (!ReadFile(handle->handle, &uv_zero_, 0, &bytes, NULL))
    err = GetLastError();

  /* Let the main thread know the worker is past the point of blocking. */
  assert(thread == *thread_ptr);
  *thread_ptr = INVALID_HANDLE_VALUE;

  /* Briefly acquire the mutex. Since the main thread holds the lock while it
   * is spinning trying to cancel this thread's I/O, we will block here until
   * it stops doing that. */
  EnterCriticalSection(lock);
  LeaveCriticalSection(lock);

out2:
  /* Close the handle to the current thread. */
  CloseHandle(thread);

out1:
  /* Set request status and post a completion record to the IOCP. */
  if (err)
    SET_REQ_ERROR(req, err);
  else
    SET_REQ_SUCCESS(req);
  POST_COMPLETION_FOR_REQ(loop, req);

  return 0;
}


static DWORD WINAPI uv_pipe_writefile_thread_proc(void* parameter) {
  int result;
  DWORD bytes;
  uv_write_t* req = (uv_write_t*) parameter;
  uv_pipe_t* handle = (uv_pipe_t*) req->handle;
  uv_loop_t* loop = handle->loop;

  assert(req != NULL);
  assert(req->type == UV_WRITE);
  assert(handle->type == UV_NAMED_PIPE);

  result = WriteFile(handle->handle,
                     req->write_buffer.base,
                     req->write_buffer.len,
                     &bytes,
                     NULL);

  if (!result) {
    SET_REQ_ERROR(req, GetLastError());
  }

  POST_COMPLETION_FOR_REQ(loop, req);
  return 0;
}


static void CALLBACK post_completion_read_wait(void* context, BOOLEAN timed_out) {
  uv_read_t* req;
  uv_tcp_t* handle;

  req = (uv_read_t*) context;
  assert(req != NULL);
  handle = (uv_tcp_t*)req->data;
  assert(handle != NULL);
  assert(!timed_out);

  if (!PostQueuedCompletionStatus(handle->loop->iocp,
                                  req->u.io.overlapped.InternalHigh,
                                  0,
                                  &req->u.io.overlapped)) {
    uv_fatal_error(GetLastError(), "PostQueuedCompletionStatus");
  }
}


static void CALLBACK post_completion_write_wait(void* context, BOOLEAN timed_out) {
  uv_write_t* req;
  uv_tcp_t* handle;

  req = (uv_write_t*) context;
  assert(req != NULL);
  handle = (uv_tcp_t*)req->handle;
  assert(handle != NULL);
  assert(!timed_out);

  if (!PostQueuedCompletionStatus(handle->loop->iocp,
                                  req->u.io.overlapped.InternalHigh,
                                  0,
                                  &req->u.io.overlapped)) {
    uv_fatal_error(GetLastError(), "PostQueuedCompletionStatus");
  }
}


static void uv__pipe_queue_read(uv_loop_t* loop, uv_pipe_t* handle) {
  uv_read_t* req;
  int result;

  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));

  assert(handle->handle != INVALID_HANDLE_VALUE);

  req = &handle->read_req;

  if (handle->flags & UV_HANDLE_NON_OVERLAPPED_PIPE) {
    handle->pipe.conn.readfile_thread_handle = NULL; /* Reset cancellation. */
    if (!QueueUserWorkItem(&uv_pipe_zero_readfile_thread_proc,
                           req,
                           WT_EXECUTELONGFUNCTION)) {
      /* Make this req pending reporting an error. */
      SET_REQ_ERROR(req, GetLastError());
      goto error;
    }
  } else {
    memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));
    if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
      assert(req->event_handle != NULL);
      req->u.io.overlapped.hEvent = (HANDLE) ((uintptr_t) req->event_handle | 1);
    }

    /* Do 0-read */
    result = ReadFile(handle->handle,
                      &uv_zero_,
                      0,
                      NULL,
                      &req->u.io.overlapped);

    if (!result && GetLastError() != ERROR_IO_PENDING) {
      /* Make this req pending reporting an error. */
      SET_REQ_ERROR(req, GetLastError());
      goto error;
    }

    if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
      assert(req->wait_handle == INVALID_HANDLE_VALUE);
      if (!RegisterWaitForSingleObject(&req->wait_handle,
          req->event_handle, post_completion_read_wait, (void*) req,
          INFINITE, WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE)) {
        SET_REQ_ERROR(req, GetLastError());
        goto error;
      }
    }
  }

  /* Start the eof timer if there is one */
  eof_timer_start(handle);
  handle->flags |= UV_HANDLE_READ_PENDING;
  handle->reqs_pending++;
  return;

error:
  uv__insert_pending_req(loop, (uv_req_t*)req);
  handle->flags |= UV_HANDLE_READ_PENDING;
  handle->reqs_pending++;
}


int uv__pipe_read_start(uv_pipe_t* handle,
                        uv_alloc_cb alloc_cb,
                        uv_read_cb read_cb) {
  uv_loop_t* loop = handle->loop;

  handle->flags |= UV_HANDLE_READING;
  INCREASE_ACTIVE_COUNT(loop, handle);
  handle->read_cb = read_cb;
  handle->alloc_cb = alloc_cb;

  if (handle->read_req.event_handle == NULL) {
    handle->read_req.event_handle = CreateEvent(NULL, 0, 0, NULL);
    if (handle->read_req.event_handle == NULL) {
      uv_fatal_error(GetLastError(), "CreateEvent");
    }
  }

  /* If reading was stopped and then started again, there could still be a read
   * request pending. */
  if (!(handle->flags & UV_HANDLE_READ_PENDING)) {
    uv__pipe_queue_read(loop, handle);
  }

  return 0;
}


static void uv__insert_non_overlapped_write_req(uv_pipe_t* handle,
    uv_write_t* req) {
  req->next_req = NULL;
  if (handle->pipe.conn.non_overlapped_writes_tail) {
    req->next_req =
      handle->pipe.conn.non_overlapped_writes_tail->next_req;
    handle->pipe.conn.non_overlapped_writes_tail->next_req = (uv_req_t*)req;
    handle->pipe.conn.non_overlapped_writes_tail = req;
  } else {
    req->next_req = (uv_req_t*)req;
    handle->pipe.conn.non_overlapped_writes_tail = req;
  }
}


static uv_write_t* uv_remove_non_overlapped_write_req(uv_pipe_t* handle) {
  uv_write_t* req;

  if (handle->pipe.conn.non_overlapped_writes_tail) {
    req = (uv_write_t*)handle->pipe.conn.non_overlapped_writes_tail->next_req;

    if (req == handle->pipe.conn.non_overlapped_writes_tail) {
      handle->pipe.conn.non_overlapped_writes_tail = NULL;
    } else {
      handle->pipe.conn.non_overlapped_writes_tail->next_req =
        req->next_req;
    }

    return req;
  } else {
    /* queue empty */
    return NULL;
  }
}


static void uv__queue_non_overlapped_write(uv_pipe_t* handle) {
  uv_write_t* req = uv_remove_non_overlapped_write_req(handle);
  if (req) {
    if (!QueueUserWorkItem(&uv_pipe_writefile_thread_proc,
                           req,
                           WT_EXECUTELONGFUNCTION)) {
      uv_fatal_error(GetLastError(), "QueueUserWorkItem");
    }
  }
}


static int uv__build_coalesced_write_req(uv_write_t* user_req,
                                         const uv_buf_t bufs[],
                                         size_t nbufs,
                                         uv_write_t** req_out,
                                         uv_buf_t* write_buf_out) {
  /* Pack into a single heap-allocated buffer:
   *   (a) a uv_write_t structure where libuv stores the actual state.
   *   (b) a pointer to the original uv_write_t.
   *   (c) data from all `bufs` entries.
   */
  char* heap_buffer;
  size_t heap_buffer_length, heap_buffer_offset;
  uv__coalesced_write_t* coalesced_write_req; /* (a) + (b) */
  char* data_start;                           /* (c) */
  size_t data_length;
  unsigned int i;

  /* Compute combined size of all combined buffers from `bufs`. */
  data_length = 0;
  for (i = 0; i < nbufs; i++)
    data_length += bufs[i].len;

  /* The total combined size of data buffers should not exceed UINT32_MAX,
   * because WriteFile() won't accept buffers larger than that. */
  if (data_length > UINT32_MAX)
    return WSAENOBUFS; /* Maps to UV_ENOBUFS. */

  /* Compute heap buffer size. */
  heap_buffer_length = sizeof *coalesced_write_req + /* (a) + (b) */
                       data_length;                  /* (c) */

  /* Allocate buffer. */
  heap_buffer = uv__malloc(heap_buffer_length);
  if (heap_buffer == NULL)
    return ERROR_NOT_ENOUGH_MEMORY; /* Maps to UV_ENOMEM. */

  /* Copy uv_write_t information to the buffer. */
  coalesced_write_req = (uv__coalesced_write_t*) heap_buffer;
  coalesced_write_req->req = *user_req; /* copy (a) */
  coalesced_write_req->req.coalesced = 1;
  coalesced_write_req->user_req = user_req;         /* copy (b) */
  heap_buffer_offset = sizeof *coalesced_write_req; /* offset (a) + (b) */

  /* Copy data buffers to the heap buffer. */
  data_start = &heap_buffer[heap_buffer_offset];
  for (i = 0; i < nbufs; i++) {
    memcpy(&heap_buffer[heap_buffer_offset],
           bufs[i].base,
           bufs[i].len);               /* copy (c) */
    heap_buffer_offset += bufs[i].len; /* offset (c) */
  }
  assert(heap_buffer_offset == heap_buffer_length);

  /* Set out arguments and return. */
  *req_out = &coalesced_write_req->req;
  *write_buf_out = uv_buf_init(data_start, (unsigned int) data_length);
  return 0;
}


static int uv__pipe_write_data(uv_loop_t* loop,
                               uv_write_t* req,
                               uv_pipe_t* handle,
                               const uv_buf_t bufs[],
                               size_t nbufs,
                               uv_write_cb cb,
                               int copy_always) {
  int err;
  int result;
  uv_buf_t write_buf;

  assert(handle->handle != INVALID_HANDLE_VALUE);

  UV_REQ_INIT(req, UV_WRITE);
  req->handle = (uv_stream_t*) handle;
  req->send_handle = NULL;
  req->cb = cb;
  /* Private fields. */
  req->coalesced = 0;
  req->event_handle = NULL;
  req->wait_handle = INVALID_HANDLE_VALUE;

  /* Prepare the overlapped structure. */
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));
  if (handle->flags & (UV_HANDLE_EMULATE_IOCP | UV_HANDLE_BLOCKING_WRITES)) {
    req->event_handle = CreateEvent(NULL, 0, 0, NULL);
    if (req->event_handle == NULL) {
      uv_fatal_error(GetLastError(), "CreateEvent");
    }
    req->u.io.overlapped.hEvent = (HANDLE) ((uintptr_t) req->event_handle | 1);
  }
  req->write_buffer = uv_null_buf_;

  if (nbufs == 0) {
    /* Write empty buffer. */
    write_buf = uv_null_buf_;
  } else if (nbufs == 1 && !copy_always) {
    /* Write directly from bufs[0]. */
    write_buf = bufs[0];
  } else {
    /* Coalesce all `bufs` into one big buffer. This also creates a new
     * write-request structure that replaces the old one. */
    err = uv__build_coalesced_write_req(req, bufs, nbufs, &req, &write_buf);
    if (err != 0)
      return err;
  }

  if ((handle->flags &
      (UV_HANDLE_BLOCKING_WRITES | UV_HANDLE_NON_OVERLAPPED_PIPE)) ==
      (UV_HANDLE_BLOCKING_WRITES | UV_HANDLE_NON_OVERLAPPED_PIPE)) {
    DWORD bytes;
    result =
        WriteFile(handle->handle, write_buf.base, write_buf.len, &bytes, NULL);

    if (!result) {
      err = GetLastError();
      return err;
    } else {
      /* Request completed immediately. */
      req->u.io.queued_bytes = 0;
    }

    REGISTER_HANDLE_REQ(loop, handle);
    handle->reqs_pending++;
    handle->stream.conn.write_reqs_pending++;
    POST_COMPLETION_FOR_REQ(loop, req);
    return 0;
  } else if (handle->flags & UV_HANDLE_NON_OVERLAPPED_PIPE) {
    req->write_buffer = write_buf;
    uv__insert_non_overlapped_write_req(handle, req);
    if (handle->stream.conn.write_reqs_pending == 0) {
      uv__queue_non_overlapped_write(handle);
    }

    /* Request queued by the kernel. */
    req->u.io.queued_bytes = write_buf.len;
    handle->write_queue_size += req->u.io.queued_bytes;
  } else if (handle->flags & UV_HANDLE_BLOCKING_WRITES) {
    /* Using overlapped IO, but wait for completion before returning */
    result = WriteFile(handle->handle,
                       write_buf.base,
                       write_buf.len,
                       NULL,
                       &req->u.io.overlapped);

    if (!result && GetLastError() != ERROR_IO_PENDING) {
      err = GetLastError();
      CloseHandle(req->event_handle);
      req->event_handle = NULL;
      return err;
    }

    if (result) {
      /* Request completed immediately. */
      req->u.io.queued_bytes = 0;
    } else {
      /* Request queued by the kernel. */
      req->u.io.queued_bytes = write_buf.len;
      handle->write_queue_size += req->u.io.queued_bytes;
      if (WaitForSingleObject(req->event_handle, INFINITE) !=
          WAIT_OBJECT_0) {
        err = GetLastError();
        CloseHandle(req->event_handle);
        req->event_handle = NULL;
        return err;
      }
    }
    CloseHandle(req->event_handle);
    req->event_handle = NULL;

    REGISTER_HANDLE_REQ(loop, handle);
    handle->reqs_pending++;
    handle->stream.conn.write_reqs_pending++;
    return 0;
  } else {
    result = WriteFile(handle->handle,
                       write_buf.base,
                       write_buf.len,
                       NULL,
                       &req->u.io.overlapped);

    if (!result && GetLastError() != ERROR_IO_PENDING) {
      return GetLastError();
    }

    if (result) {
      /* Request completed immediately. */
      req->u.io.queued_bytes = 0;
    } else {
      /* Request queued by the kernel. */
      req->u.io.queued_bytes = write_buf.len;
      handle->write_queue_size += req->u.io.queued_bytes;
    }

    if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
      if (!RegisterWaitForSingleObject(&req->wait_handle,
          req->event_handle, post_completion_write_wait, (void*) req,
          INFINITE, WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE)) {
        return GetLastError();
      }
    }
  }

  REGISTER_HANDLE_REQ(loop, handle);
  handle->reqs_pending++;
  handle->stream.conn.write_reqs_pending++;

  return 0;
}


static DWORD uv__pipe_get_ipc_remote_pid(uv_pipe_t* handle) {
  DWORD* pid = &handle->pipe.conn.ipc_remote_pid;

  /* If the both ends of the IPC pipe are owned by the same process,
   * the remote end pid may not yet be set. If so, do it here.
   * TODO: this is weird; it'd probably better to use a handshake. */
  if (*pid == 0) {
    GetNamedPipeClientProcessId(handle->handle, pid);
    if (*pid == GetCurrentProcessId()) {
      GetNamedPipeServerProcessId(handle->handle, pid);
    }
  }

  return *pid;
}


int uv__pipe_write_ipc(uv_loop_t* loop,
                       uv_write_t* req,
                       uv_pipe_t* handle,
                       const uv_buf_t data_bufs[],
                       size_t data_buf_count,
                       uv_stream_t* send_handle,
                       uv_write_cb cb) {
  uv_buf_t stack_bufs[6];
  uv_buf_t* bufs;
  size_t buf_count, buf_index;
  uv__ipc_frame_header_t frame_header;
  uv__ipc_socket_xfer_type_t xfer_type = UV__IPC_SOCKET_XFER_NONE;
  uv__ipc_socket_xfer_info_t xfer_info;
  uint64_t data_length;
  size_t i;
  int err;

  /* Compute the combined size of data buffers. */
  data_length = 0;
  for (i = 0; i < data_buf_count; i++)
    data_length += data_bufs[i].len;
  if (data_length > UINT32_MAX)
    return WSAENOBUFS; /* Maps to UV_ENOBUFS. */

  /* Prepare the frame's socket xfer payload. */
  if (send_handle != NULL) {
    uv_tcp_t* send_tcp_handle = (uv_tcp_t*) send_handle;

    /* Verify that `send_handle` it is indeed a tcp handle. */
    if (send_tcp_handle->type != UV_TCP)
      return ERROR_NOT_SUPPORTED;

    /* Export the tcp handle. */
    err = uv__tcp_xfer_export(send_tcp_handle,
                              uv__pipe_get_ipc_remote_pid(handle),
                              &xfer_type,
                              &xfer_info);
    if (err != 0)
      return err;
  }

  /* Compute the number of uv_buf_t's required. */
  buf_count = 1 + data_buf_count; /* Frame header and data buffers. */
  if (send_handle != NULL)
    buf_count += 1; /* One extra for the socket xfer information. */

  /* Use the on-stack buffer array if it is big enough; otherwise allocate
   * space for it on the heap. */
  if (buf_count < ARRAY_SIZE(stack_bufs)) {
    /* Use on-stack buffer array. */
    bufs = stack_bufs;
  } else {
    /* Use heap-allocated buffer array. */
    bufs = uv__calloc(buf_count, sizeof(uv_buf_t));
    if (bufs == NULL)
      return ERROR_NOT_ENOUGH_MEMORY; /* Maps to UV_ENOMEM. */
  }
  buf_index = 0;

  /* Initialize frame header and add it to the buffers list. */
  memset(&frame_header, 0, sizeof frame_header);
  bufs[buf_index++] = uv_buf_init((char*) &frame_header, sizeof frame_header);

  if (send_handle != NULL) {
    /* Add frame header flags. */
    switch (xfer_type) {
      case UV__IPC_SOCKET_XFER_TCP_CONNECTION:
        frame_header.flags |= UV__IPC_FRAME_HAS_SOCKET_XFER |
                              UV__IPC_FRAME_XFER_IS_TCP_CONNECTION;
        break;
      case UV__IPC_SOCKET_XFER_TCP_SERVER:
        frame_header.flags |= UV__IPC_FRAME_HAS_SOCKET_XFER;
        break;
      default:
        assert(0);  /* Unreachable. */
    }
    /* Add xfer info buffer. */
    bufs[buf_index++] = uv_buf_init((char*) &xfer_info, sizeof xfer_info);
  }

  if (data_length > 0) {
    /* Update frame header. */
    frame_header.flags |= UV__IPC_FRAME_HAS_DATA;
    frame_header.data_length = (uint32_t) data_length;
    /* Add data buffers to buffers list. */
    for (i = 0; i < data_buf_count; i++)
      bufs[buf_index++] = data_bufs[i];
  }

  /* Write buffers. We set the `always_copy` flag, so it is not a problem that
   * some of the written data lives on the stack. */
  err = uv__pipe_write_data(loop, req, handle, bufs, buf_count, cb, 1);

  /* If we had to heap-allocate the bufs array, free it now. */
  if (bufs != stack_bufs) {
    uv__free(bufs);
  }

  return err;
}


int uv__pipe_write(uv_loop_t* loop,
                   uv_write_t* req,
                   uv_pipe_t* handle,
                   const uv_buf_t bufs[],
                   size_t nbufs,
                   uv_stream_t* send_handle,
                   uv_write_cb cb) {
  if (handle->ipc) {
    /* IPC pipe write: use framing protocol. */
    return uv__pipe_write_ipc(loop, req, handle, bufs, nbufs, send_handle, cb);
  } else {
    /* Non-IPC pipe write: put data on the wire directly. */
    assert(send_handle == NULL);
    return uv__pipe_write_data(loop, req, handle, bufs, nbufs, cb, 0);
  }
}


static void uv__pipe_read_eof(uv_loop_t* loop, uv_pipe_t* handle,
    uv_buf_t buf) {
  /* If there is an eof timer running, we don't need it any more, so discard
   * it. */
  eof_timer_destroy(handle);

  uv_read_stop((uv_stream_t*) handle);

  handle->read_cb((uv_stream_t*) handle, UV_EOF, &buf);
}


static void uv__pipe_read_error(uv_loop_t* loop, uv_pipe_t* handle, int error,
    uv_buf_t buf) {
  /* If there is an eof timer running, we don't need it any more, so discard
   * it. */
  eof_timer_destroy(handle);

  uv_read_stop((uv_stream_t*) handle);

  handle->read_cb((uv_stream_t*)handle, uv_translate_sys_error(error), &buf);
}


static void uv__pipe_read_error_or_eof(uv_loop_t* loop, uv_pipe_t* handle,
    DWORD error, uv_buf_t buf) {
  if (error == ERROR_BROKEN_PIPE) {
    uv__pipe_read_eof(loop, handle, buf);
  } else {
    uv__pipe_read_error(loop, handle, error, buf);
  }
}


static void uv__pipe_queue_ipc_xfer_info(
    uv_pipe_t* handle,
    uv__ipc_socket_xfer_type_t xfer_type,
    uv__ipc_socket_xfer_info_t* xfer_info) {
  uv__ipc_xfer_queue_item_t* item;

  item = (uv__ipc_xfer_queue_item_t*) uv__malloc(sizeof(*item));
  if (item == NULL)
    uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");

  item->xfer_type = xfer_type;
  item->xfer_info = *xfer_info;

  uv__queue_insert_tail(&handle->pipe.conn.ipc_xfer_queue, &item->member);
  handle->pipe.conn.ipc_xfer_queue_length++;
}


/* Read an exact number of bytes from a pipe. If an error or end-of-file is
 * encountered before the requested number of bytes are read, an error is
 * returned. */
static DWORD uv__pipe_read_exactly(uv_pipe_t* handle, void* buffer, DWORD count) {
  uv_read_t* req;
  DWORD bytes_read;
  DWORD bytes_read_now;

  bytes_read = 0;
  while (bytes_read < count) {
    req = &handle->read_req;
    memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));
    req->u.io.overlapped.hEvent = (HANDLE) ((uintptr_t) req->event_handle | 1);
    if (!ReadFile(handle->handle,
                  (char*) buffer + bytes_read,
                  count - bytes_read,
                  &bytes_read_now,
                  &req->u.io.overlapped)) {
      if (GetLastError() != ERROR_IO_PENDING)
        return GetLastError();
      if (!GetOverlappedResult(handle->handle, &req->u.io.overlapped, &bytes_read_now, TRUE))
        return GetLastError();
    }

    bytes_read += bytes_read_now;
  }

  assert(bytes_read == count);
  return 0;
}


static int uv__pipe_read_data(uv_loop_t* loop,
                              uv_pipe_t* handle,
                              DWORD* bytes_read, /* inout argument */
                              DWORD max_bytes) {
  uv_buf_t buf;
  uv_read_t* req;
  DWORD r;
  DWORD bytes_available;
  int more;

  /* Ask the user for a buffer to read data into. */
  buf = uv_buf_init(NULL, 0);
  handle->alloc_cb((uv_handle_t*) handle, *bytes_read, &buf);
  if (buf.base == NULL || buf.len == 0) {
    handle->read_cb((uv_stream_t*) handle, UV_ENOBUFS, &buf);
    return 0; /* Break out of read loop. */
  }

  /* Ensure we read at most the smaller of:
   *   (a) the length of the user-allocated buffer.
   *   (b) the maximum data length as specified by the `max_bytes` argument.
   *   (c) the amount of data that can be read non-blocking
   */
  if (max_bytes > buf.len)
    max_bytes = buf.len;

  if (handle->flags & UV_HANDLE_NON_OVERLAPPED_PIPE) {
    /* The user failed to supply a pipe that can be used non-blocking or with
     * threads. Try to estimate the amount of data that is safe to read without
     * blocking, in a race-y way however. */
    bytes_available = 0;
    if (!PeekNamedPipe(handle->handle, NULL, 0, NULL, &bytes_available, NULL)) {
      r = GetLastError();
    } else {
      if (max_bytes > bytes_available)
        max_bytes = bytes_available;
      *bytes_read = 0;
      if (max_bytes == 0 || ReadFile(handle->handle, buf.base, max_bytes, bytes_read, NULL))
        r = ERROR_SUCCESS;
      else
        r = GetLastError();
    }
    more = max_bytes < bytes_available;
  } else {
    /* Read into the user buffer.
     * Prepare an Event so that we can cancel if it doesn't complete immediately.
     */
    req = &handle->read_req;
    memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));
    req->u.io.overlapped.hEvent = (HANDLE) ((uintptr_t) req->event_handle | 1);
    if (ReadFile(handle->handle, buf.base, max_bytes, bytes_read, &req->u.io.overlapped)) {
      r = ERROR_SUCCESS;
    } else {
      r = GetLastError();
      *bytes_read = 0;
      if (r == ERROR_IO_PENDING) {
        r = CancelIoEx(handle->handle, &req->u.io.overlapped);
        assert(r || GetLastError() == ERROR_NOT_FOUND);
        if (GetOverlappedResult(handle->handle, &req->u.io.overlapped, bytes_read, TRUE)) {
          r = ERROR_SUCCESS;
        } else {
          r = GetLastError();
          *bytes_read = 0;
        }
      }
    }
    more = *bytes_read == max_bytes;
  }

  /* Call the read callback. */
  if (r == ERROR_SUCCESS || r == ERROR_OPERATION_ABORTED)
    handle->read_cb((uv_stream_t*) handle, *bytes_read, &buf);
  else
    uv__pipe_read_error_or_eof(loop, handle, r, buf);

  return more;
}


static int uv__pipe_read_ipc(uv_loop_t* loop, uv_pipe_t* handle) {
  uint32_t* data_remaining;
  DWORD err;
  DWORD more;
  DWORD bytes_read;

  data_remaining = &handle->pipe.conn.ipc_data_frame.payload_remaining;

  if (*data_remaining > 0) {
    /* Read frame data payload. */
    bytes_read = *data_remaining;
    more = uv__pipe_read_data(loop, handle, &bytes_read, bytes_read);
    *data_remaining -= bytes_read;

  } else {
    /* Start of a new IPC frame. */
    uv__ipc_frame_header_t frame_header;
    uint32_t xfer_flags;
    uv__ipc_socket_xfer_type_t xfer_type;
    uv__ipc_socket_xfer_info_t xfer_info;

    /* Read the IPC frame header. */
    err = uv__pipe_read_exactly(
        handle, &frame_header, sizeof frame_header);
    if (err)
      goto error;

    /* Validate that flags are valid. */
    if ((frame_header.flags & ~UV__IPC_FRAME_VALID_FLAGS) != 0)
      goto invalid;
    /* Validate that reserved2 is zero. */
    if (frame_header.reserved2 != 0)
      goto invalid;

    /* Parse xfer flags. */
    xfer_flags = frame_header.flags & UV__IPC_FRAME_XFER_FLAGS;
    if (xfer_flags & UV__IPC_FRAME_HAS_SOCKET_XFER) {
      /* Socket coming -- determine the type. */
      xfer_type = xfer_flags & UV__IPC_FRAME_XFER_IS_TCP_CONNECTION
                      ? UV__IPC_SOCKET_XFER_TCP_CONNECTION
                      : UV__IPC_SOCKET_XFER_TCP_SERVER;
    } else if (xfer_flags == 0) {
      /* No socket. */
      xfer_type = UV__IPC_SOCKET_XFER_NONE;
    } else {
      /* Invalid flags. */
      goto invalid;
    }

    /* Parse data frame information. */
    if (frame_header.flags & UV__IPC_FRAME_HAS_DATA) {
      *data_remaining = frame_header.data_length;
    } else if (frame_header.data_length != 0) {
      /* Data length greater than zero but data flag not set -- invalid. */
      goto invalid;
    }

    /* If no socket xfer info follows, return here. Data will be read in a
     * subsequent invocation of uv__pipe_read_ipc(). */
    if (xfer_type != UV__IPC_SOCKET_XFER_NONE) {
      /* Read transferred socket information. */
      err = uv__pipe_read_exactly(handle, &xfer_info, sizeof xfer_info);
      if (err)
        goto error;

      /* Store the pending socket info. */
      uv__pipe_queue_ipc_xfer_info(handle, xfer_type, &xfer_info);
    }
  }

  /* Return whether the caller should immediately try another read call to get
   * more data. Calling uv__pipe_read_exactly will hang if there isn't data
   * available, so we cannot do this unless we are guaranteed not to reach that.
   */
  more = *data_remaining > 0;
  return more;

invalid:
  /* Invalid frame. */
  err = WSAECONNABORTED; /* Maps to UV_ECONNABORTED. */

error:
  uv__pipe_read_error_or_eof(loop, handle, err, uv_null_buf_);
  return 0; /* Break out of read loop. */
}


void uv__process_pipe_read_req(uv_loop_t* loop,
                               uv_pipe_t* handle,
                               uv_req_t* req) {
  DWORD err;
  DWORD more;
  DWORD bytes_requested;
  assert(handle->type == UV_NAMED_PIPE);

  handle->flags &= ~(UV_HANDLE_READ_PENDING | UV_HANDLE_CANCELLATION_PENDING);
  DECREASE_PENDING_REQ_COUNT(handle);
  eof_timer_stop(handle);

  if (handle->read_req.wait_handle != INVALID_HANDLE_VALUE) {
    UnregisterWait(handle->read_req.wait_handle);
    handle->read_req.wait_handle = INVALID_HANDLE_VALUE;
  }

  /* At this point, we're done with bookkeeping. If the user has stopped
   * reading the pipe in the meantime, there is nothing left to do, since there
   * is no callback that we can call. */
  if (!(handle->flags & UV_HANDLE_READING))
    return;

  if (!REQ_SUCCESS(req)) {
    /* An error occurred doing the zero-read. */
    err = GET_REQ_ERROR(req);

    /* If the read was cancelled by uv__pipe_interrupt_read(), the request may
     * indicate an ERROR_OPERATION_ABORTED error. This error isn't relevant to
     * the user; we'll start a new zero-read at the end of this function. */
    if (err != ERROR_OPERATION_ABORTED)
      uv__pipe_read_error_or_eof(loop, handle, err, uv_null_buf_);

  } else {
    /* The zero-read completed without error, indicating there is data
     * available in the kernel buffer. */
    while (handle->flags & UV_HANDLE_READING) {
      bytes_requested = 65536;
      /* Depending on the type of pipe, read either IPC frames or raw data. */
      if (handle->ipc)
          more = uv__pipe_read_ipc(loop, handle);
      else
          more = uv__pipe_read_data(loop, handle, &bytes_requested, INT32_MAX);

      /* If no bytes were read, treat this as an indication that an error
       * occurred, and break out of the read loop. */
      if (more == 0)
        break;
    }
  }

  /* Start another zero-read request if necessary. */
  if ((handle->flags & UV_HANDLE_READING) &&
      !(handle->flags & UV_HANDLE_READ_PENDING)) {
    uv__pipe_queue_read(loop, handle);
  }
}


void uv__process_pipe_write_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_write_t* req) {
  int err;

  assert(handle->type == UV_NAMED_PIPE);

  assert(handle->write_queue_size >= req->u.io.queued_bytes);
  handle->write_queue_size -= req->u.io.queued_bytes;

  UNREGISTER_HANDLE_REQ(loop, handle);

  if (req->wait_handle != INVALID_HANDLE_VALUE) {
    UnregisterWait(req->wait_handle);
    req->wait_handle = INVALID_HANDLE_VALUE;
  }
  if (req->event_handle) {
    CloseHandle(req->event_handle);
    req->event_handle = NULL;
  }

  err = GET_REQ_ERROR(req);

  /* If this was a coalesced write, extract pointer to the user_provided
   * uv_write_t structure so we can pass the expected pointer to the callback,
   * then free the heap-allocated write req. */
  if (req->coalesced) {
    uv__coalesced_write_t* coalesced_write =
        container_of(req, uv__coalesced_write_t, req);
    req = coalesced_write->user_req;
    uv__free(coalesced_write);
  }
  if (req->cb) {
    req->cb(req, uv_translate_sys_error(err));
  }

  handle->stream.conn.write_reqs_pending--;

  if (handle->flags & UV_HANDLE_NON_OVERLAPPED_PIPE &&
      handle->pipe.conn.non_overlapped_writes_tail) {
    assert(handle->stream.conn.write_reqs_pending > 0);
    uv__queue_non_overlapped_write(handle);
  }

  if (handle->stream.conn.write_reqs_pending == 0 &&
      uv__is_stream_shutting(handle))
    uv__pipe_shutdown(loop, handle, handle->stream.conn.shutdown_req);

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_pipe_accept_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_req_t* raw_req) {
  uv_pipe_accept_t* req = (uv_pipe_accept_t*) raw_req;

  assert(handle->type == UV_NAMED_PIPE);

  if (handle->flags & UV_HANDLE_CLOSING) {
    /* The req->pipeHandle should be freed already in uv__pipe_close(). */
    assert(req->pipeHandle == INVALID_HANDLE_VALUE);
    DECREASE_PENDING_REQ_COUNT(handle);
    return;
  }

  if (REQ_SUCCESS(req)) {
    assert(req->pipeHandle != INVALID_HANDLE_VALUE);
    req->next_pending = handle->pipe.serv.pending_accepts;
    handle->pipe.serv.pending_accepts = req;

    if (handle->stream.serv.connection_cb) {
      handle->stream.serv.connection_cb((uv_stream_t*)handle, 0);
    }
  } else {
    if (req->pipeHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(req->pipeHandle);
      req->pipeHandle = INVALID_HANDLE_VALUE;
    }
    if (!(handle->flags & UV_HANDLE_CLOSING)) {
      uv__pipe_queue_accept(loop, handle, req, FALSE);
    }
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_pipe_connect_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_connect_t* req) {
  HANDLE pipeHandle;
  DWORD duplex_flags;
  int err;

  assert(handle->type == UV_NAMED_PIPE);

  UNREGISTER_HANDLE_REQ(loop, handle);

  err = 0;
  if (REQ_SUCCESS(req)) {
    pipeHandle = req->u.connect.pipeHandle;
    duplex_flags = req->u.connect.duplex_flags;
    if (handle->flags & UV_HANDLE_CLOSING)
      err = UV_ECANCELED;
    else
      err = uv__set_pipe_handle(loop, handle, pipeHandle, -1, duplex_flags);
    if (err)
      CloseHandle(pipeHandle);
  } else {
    err = uv_translate_sys_error(GET_REQ_ERROR(req));
  }

  if (req->cb)
    req->cb(req, err);

  DECREASE_PENDING_REQ_COUNT(handle);
}



void uv__process_pipe_shutdown_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_shutdown_t* req) {
  int err;

  assert(handle->type == UV_NAMED_PIPE);

  /* Clear the shutdown_req field so we don't go here again. */
  handle->stream.conn.shutdown_req = NULL;
  UNREGISTER_HANDLE_REQ(loop, handle);

  if (handle->flags & UV_HANDLE_CLOSING) {
    /* Already closing. Cancel the shutdown. */
    err = UV_ECANCELED;
  } else if (!REQ_SUCCESS(req)) {
    /* An error occurred in trying to shutdown gracefully. */
    err = uv_translate_sys_error(GET_REQ_ERROR(req));
  } else {
    if (handle->flags & UV_HANDLE_READABLE) {
      /* Initialize and optionally start the eof timer. Only do this if the pipe
       * is readable and we haven't seen EOF come in ourselves. */
      eof_timer_init(handle);

      /* If reading start the timer right now. Otherwise uv__pipe_queue_read will
       * start it. */
      if (handle->flags & UV_HANDLE_READ_PENDING) {
        eof_timer_start(handle);
      }

    } else {
      /* This pipe is not readable. We can just close it to let the other end
       * know that we're done writing. */
      close_pipe(handle);
    }
    err = 0;
  }

  if (req->cb)
    req->cb(req, err);

  DECREASE_PENDING_REQ_COUNT(handle);
}


static void eof_timer_init(uv_pipe_t* pipe) {
  int r;

  assert(pipe->pipe.conn.eof_timer == NULL);
  assert(pipe->flags & UV_HANDLE_CONNECTION);

  pipe->pipe.conn.eof_timer = (uv_timer_t*) uv__malloc(sizeof *pipe->pipe.conn.eof_timer);

  r = uv_timer_init(pipe->loop, pipe->pipe.conn.eof_timer);
  assert(r == 0);  /* timers can't fail */
  (void) r;
  pipe->pipe.conn.eof_timer->data = pipe;
  uv_unref((uv_handle_t*) pipe->pipe.conn.eof_timer);
}


static void eof_timer_start(uv_pipe_t* pipe) {
  assert(pipe->flags & UV_HANDLE_CONNECTION);

  if (pipe->pipe.conn.eof_timer != NULL) {
    uv_timer_start(pipe->pipe.conn.eof_timer, eof_timer_cb, eof_timeout, 0);
  }
}


static void eof_timer_stop(uv_pipe_t* pipe) {
  assert(pipe->flags & UV_HANDLE_CONNECTION);

  if (pipe->pipe.conn.eof_timer != NULL) {
    uv_timer_stop(pipe->pipe.conn.eof_timer);
  }
}


static void eof_timer_cb(uv_timer_t* timer) {
  uv_pipe_t* pipe = (uv_pipe_t*) timer->data;
  uv_loop_t* loop = timer->loop;

  assert(pipe->type == UV_NAMED_PIPE);

  /* This should always be true, since we start the timer only in
   * uv__pipe_queue_read after successfully calling ReadFile, or in
   * uv__process_pipe_shutdown_req if a read is pending, and we always
   * immediately stop the timer in uv__process_pipe_read_req. */
  assert(pipe->flags & UV_HANDLE_READ_PENDING);

  /* If there are many packets coming off the iocp then the timer callback may
   * be called before the read request is coming off the queue. Therefore we
   * check here if the read request has completed but will be processed later.
   */
  if ((pipe->flags & UV_HANDLE_READ_PENDING) &&
      HasOverlappedIoCompleted(&pipe->read_req.u.io.overlapped)) {
    return;
  }

  /* Force both ends off the pipe. */
  close_pipe(pipe);

  /* Stop reading, so the pending read that is going to fail will not be
   * reported to the user. */
  uv_read_stop((uv_stream_t*) pipe);

  /* Report the eof and update flags. This will get reported even if the user
   * stopped reading in the meantime. TODO: is that okay? */
  uv__pipe_read_eof(loop, pipe, uv_null_buf_);
}


static void eof_timer_destroy(uv_pipe_t* pipe) {
  assert(pipe->flags & UV_HANDLE_CONNECTION);

  if (pipe->pipe.conn.eof_timer) {
    uv_close((uv_handle_t*) pipe->pipe.conn.eof_timer, eof_timer_close_cb);
    pipe->pipe.conn.eof_timer = NULL;
  }
}


static void eof_timer_close_cb(uv_handle_t* handle) {
  assert(handle->type == UV_TIMER);
  uv__free(handle);
}


int uv_pipe_open(uv_pipe_t* pipe, uv_file file) {
  HANDLE os_handle = uv__get_osfhandle(file);
  NTSTATUS nt_status;
  IO_STATUS_BLOCK io_status;
  FILE_ACCESS_INFORMATION access;
  DWORD duplex_flags = 0;
  int err;

  if (os_handle == INVALID_HANDLE_VALUE)
    return UV_EBADF;
  if (pipe->flags & UV_HANDLE_PIPESERVER)
    return UV_EINVAL;
  if (pipe->flags & UV_HANDLE_CONNECTION)
    return UV_EBUSY;

  uv__pipe_connection_init(pipe);
  uv__once_init();
  /* In order to avoid closing a stdio file descriptor 0-2, duplicate the
   * underlying OS handle and forget about the original fd.
   * We could also opt to use the original OS handle and just never close it,
   * but then there would be no reliable way to cancel pending read operations
   * upon close.
   */
  if (file <= 2) {
    if (!DuplicateHandle(INVALID_HANDLE_VALUE,
                         os_handle,
                         INVALID_HANDLE_VALUE,
                         &os_handle,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
      return uv_translate_sys_error(GetLastError());
    assert(os_handle != INVALID_HANDLE_VALUE);
    file = -1;
  }

  /* Determine what kind of permissions we have on this handle.
   * Cygwin opens the pipe in message mode, but we can support it,
   * just query the access flags and set the stream flags accordingly.
   */
  nt_status = pNtQueryInformationFile(os_handle,
                                      &io_status,
                                      &access,
                                      sizeof(access),
                                      FileAccessInformation);
  if (nt_status != STATUS_SUCCESS)
    return UV_EINVAL;

  if (pipe->ipc) {
    if (!(access.AccessFlags & FILE_WRITE_DATA) ||
        !(access.AccessFlags & FILE_READ_DATA)) {
      return UV_EINVAL;
    }
  }

  if (access.AccessFlags & FILE_WRITE_DATA)
    duplex_flags |= UV_HANDLE_WRITABLE;
  if (access.AccessFlags & FILE_READ_DATA)
    duplex_flags |= UV_HANDLE_READABLE;

  err = uv__set_pipe_handle(pipe->loop,
                            pipe,
                            os_handle,
                            file,
                            duplex_flags);
  if (err) {
    if (file == -1)
      CloseHandle(os_handle);
    return err;
  }

  if (pipe->ipc) {
    assert(!(pipe->flags & UV_HANDLE_NON_OVERLAPPED_PIPE));
    GetNamedPipeClientProcessId(os_handle, &pipe->pipe.conn.ipc_remote_pid);
    if (pipe->pipe.conn.ipc_remote_pid == GetCurrentProcessId()) {
      GetNamedPipeServerProcessId(os_handle, &pipe->pipe.conn.ipc_remote_pid);
    }
    assert(pipe->pipe.conn.ipc_remote_pid != (DWORD)(uv_pid_t) -1);
  }
  return 0;
}


static int uv__pipe_getname(const uv_pipe_t* handle, char* buffer, size_t* size) {
  NTSTATUS nt_status;
  IO_STATUS_BLOCK io_status;
  FILE_NAME_INFORMATION tmp_name_info;
  FILE_NAME_INFORMATION* name_info;
  WCHAR* name_buf;
  unsigned int name_size;
  unsigned int name_len;
  int err;

  uv__once_init();
  name_info = NULL;

  if (handle->name != NULL) {
    /* The user might try to query the name before we are connected,
     * and this is just easier to return the cached value if we have it. */
    return uv__copy_utf16_to_utf8(handle->name, -1, buffer, size);
  }

  if (handle->handle == INVALID_HANDLE_VALUE) {
    *size = 0;
    return UV_EINVAL;
  }

  /* NtQueryInformationFile will block if another thread is performing a
   * blocking operation on the queried handle. If the pipe handle is
   * synchronous, there may be a worker thread currently calling ReadFile() on
   * the pipe handle, which could cause a deadlock. To avoid this, interrupt
   * the read. */
  if (handle->flags & UV_HANDLE_CONNECTION &&
      handle->flags & UV_HANDLE_NON_OVERLAPPED_PIPE) {
    uv__pipe_interrupt_read((uv_pipe_t*) handle); /* cast away const warning */
  }

  nt_status = pNtQueryInformationFile(handle->handle,
                                      &io_status,
                                      &tmp_name_info,
                                      sizeof tmp_name_info,
                                      FileNameInformation);
  if (nt_status == STATUS_BUFFER_OVERFLOW) {
    name_size = sizeof(*name_info) + tmp_name_info.FileNameLength;
    name_info = uv__malloc(name_size);
    if (!name_info) {
      *size = 0;
      return UV_ENOMEM;
    }

    nt_status = pNtQueryInformationFile(handle->handle,
                                        &io_status,
                                        name_info,
                                        name_size,
                                        FileNameInformation);
  }

  if (nt_status != STATUS_SUCCESS) {
    *size = 0;
    err = uv_translate_sys_error(pRtlNtStatusToDosError(nt_status));
    goto error;
  }

  if (!name_info) {
    /* the struct on stack was used */
    name_buf = tmp_name_info.FileName;
    name_len = tmp_name_info.FileNameLength;
  } else {
    name_buf = name_info->FileName;
    name_len = name_info->FileNameLength;
  }

  if (name_len == 0) {
    *size = 0;
    err = 0;
    goto error;
  }

  name_len /= sizeof(WCHAR);

  /* "\\\\.\\pipe" + name */
  if (*size < pipe_prefix_len) {
    *size = 0;
  }
  else {
    memcpy(buffer, pipe_prefix, pipe_prefix_len);
    *size -= pipe_prefix_len;
  }
  err = uv__copy_utf16_to_utf8(name_buf, name_len, buffer+pipe_prefix_len, size);
  *size += pipe_prefix_len;

error:
  uv__free(name_info);
  return err;
}


int uv_pipe_pending_count(uv_pipe_t* handle) {
  if (!handle->ipc)
    return 0;
  return handle->pipe.conn.ipc_xfer_queue_length;
}


int uv_pipe_getsockname(const uv_pipe_t* handle, char* buffer, size_t* size) {
  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  if (handle->flags & UV_HANDLE_BOUND)
    return uv__pipe_getname(handle, buffer, size);

  if (handle->flags & UV_HANDLE_CONNECTION ||
      handle->handle != INVALID_HANDLE_VALUE) {
    *size = 0;
    return 0;
  }

  return UV_EBADF;
}


int uv_pipe_getpeername(const uv_pipe_t* handle, char* buffer, size_t* size) {
  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  /* emulate unix behaviour */
  if (handle->flags & UV_HANDLE_BOUND)
    return UV_ENOTCONN;

  if (handle->handle != INVALID_HANDLE_VALUE)
    return uv__pipe_getname(handle, buffer, size);

  if (handle->flags & UV_HANDLE_CONNECTION) {
    if (handle->name != NULL)
      return uv__pipe_getname(handle, buffer, size);
  }

  return UV_EBADF;
}


uv_handle_type uv_pipe_pending_type(uv_pipe_t* handle) {
  if (!handle->ipc)
    return UV_UNKNOWN_HANDLE;
  if (handle->pipe.conn.ipc_xfer_queue_length == 0)
    return UV_UNKNOWN_HANDLE;
  else
    return UV_TCP;
}

int uv_pipe_chmod(uv_pipe_t* handle, int mode) {
  SID_IDENTIFIER_AUTHORITY sid_world = { SECURITY_WORLD_SID_AUTHORITY };
  PACL old_dacl, new_dacl;
  PSECURITY_DESCRIPTOR sd;
  EXPLICIT_ACCESS ea;
  PSID everyone;
  int error;

  if (handle == NULL || handle->handle == INVALID_HANDLE_VALUE)
    return UV_EBADF;

  if (mode != UV_READABLE &&
      mode != UV_WRITABLE &&
      mode != (UV_WRITABLE | UV_READABLE))
    return UV_EINVAL;

  if (!AllocateAndInitializeSid(&sid_world,
                                1,
                                SECURITY_WORLD_RID,
                                0, 0, 0, 0, 0, 0, 0,
                                &everyone)) {
    error = GetLastError();
    goto done;
  }

  if (GetSecurityInfo(handle->handle,
                      SE_KERNEL_OBJECT,
                      DACL_SECURITY_INFORMATION,
                      NULL,
                      NULL,
                      &old_dacl,
                      NULL,
                      &sd)) {
    error = GetLastError();
    goto clean_sid;
  }

  memset(&ea, 0, sizeof(EXPLICIT_ACCESS));
  if (mode & UV_READABLE)
    ea.grfAccessPermissions |= GENERIC_READ | FILE_WRITE_ATTRIBUTES;
  if (mode & UV_WRITABLE)
    ea.grfAccessPermissions |= GENERIC_WRITE | FILE_READ_ATTRIBUTES;
  ea.grfAccessPermissions |= SYNCHRONIZE;
  ea.grfAccessMode = SET_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea.Trustee.ptstrName = (LPTSTR)everyone;

  if (SetEntriesInAcl(1, &ea, old_dacl, &new_dacl)) {
    error = GetLastError();
    goto clean_sd;
  }

  if (SetSecurityInfo(handle->handle,
                      SE_KERNEL_OBJECT,
                      DACL_SECURITY_INFORMATION,
                      NULL,
                      NULL,
                      new_dacl,
                      NULL)) {
    error = GetLastError();
    goto clean_dacl;
  }

  error = 0;

clean_dacl:
  LocalFree((HLOCAL) new_dacl);
clean_sd:
  LocalFree((HLOCAL) sd);
clean_sid:
  FreeSid(everyone);
done:
  return uv_translate_sys_error(error);
}
