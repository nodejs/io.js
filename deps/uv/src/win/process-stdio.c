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

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"


/*
 * The `child_stdio_buffer` buffer has the following layout:
 *   int number_of_fds
 *   unsigned char crt_flags[number_of_fds]
 *   HANDLE os_handle[number_of_fds]
 */
#define CHILD_STDIO_SIZE(count)                     \
    (sizeof(int) +                                  \
     sizeof(unsigned char) * (count) +              \
     sizeof(uintptr_t) * (count))

#define CHILD_STDIO_COUNT(buffer)                   \
    *((unsigned int*) (buffer))

#define CHILD_STDIO_CRT_FLAGS(buffer, fd)           \
    *((unsigned char*) (buffer) + sizeof(int) + fd)

#define CHILD_STDIO_HANDLE(buffer, fd)           \
    ((void*) ((unsigned char*) (buffer) +        \
              sizeof(int) +                      \
              sizeof(unsigned char) *            \
              CHILD_STDIO_COUNT((buffer)) +      \
              sizeof(HANDLE) * (fd)))


/* CRT file descriptor mode flags */
#define FOPEN       0x01
#define FEOFLAG     0x02
#define FCRLF       0x04
#define FPIPE       0x08
#define FNOINHERIT  0x10
#define FAPPEND     0x20
#define FDEV        0x40
#define FTEXT       0x80


/*
 * Clear the HANDLE_FLAG_INHERIT flag from all HANDLEs that were inherited
 * the parent process. Don't check for errors - the stdio handles may not be
 * valid, or may be closed already. There is no guarantee that this function
 * does a perfect job.
 */
void uv_disable_stdio_inheritance(void) {
  HANDLE handle;
  STARTUPINFOW si;

  /* Make the windows stdio handles non-inheritable. */
  handle = GetStdHandle(STD_INPUT_HANDLE);
  if (handle != NULL && handle != INVALID_HANDLE_VALUE)
    SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);

  handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (handle != NULL && handle != INVALID_HANDLE_VALUE)
    SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);

  handle = GetStdHandle(STD_ERROR_HANDLE);
  if (handle != NULL && handle != INVALID_HANDLE_VALUE)
    SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);

  /* Make inherited CRT FDs non-inheritable. */
  GetStartupInfoW(&si);
  if (uv__stdio_verify(si.lpReserved2, si.cbReserved2))
    uv__stdio_noinherit(si.lpReserved2);
}


static int uv__duplicate_handle(uv_loop_t* loop, HANDLE handle, HANDLE* dup) {
  HANDLE current_process;


  /* _get_osfhandle will sometimes return -2 in case of an error. This seems to
   * happen when fd <= 2 and the process' corresponding stdio handle is set to
   * NULL. Unfortunately DuplicateHandle will happily duplicate (HANDLE) -2, so
   * this situation goes unnoticed until someone tries to use the duplicate.
   * Therefore we filter out known-invalid handles here. */
  if (handle == INVALID_HANDLE_VALUE ||
      handle == NULL ||
      handle == (HANDLE) -2) {
    *dup = INVALID_HANDLE_VALUE;
    return ERROR_INVALID_HANDLE;
  }

  current_process = GetCurrentProcess();

  if (!DuplicateHandle(current_process,
                       handle,
                       current_process,
                       dup,
                       0,
                       TRUE,
                       DUPLICATE_SAME_ACCESS)) {
    *dup = INVALID_HANDLE_VALUE;
    return GetLastError();
  }

  return 0;
}


static int uv__duplicate_fd(uv_loop_t* loop, int fd, HANDLE* dup) {
  HANDLE handle;

  if (fd == -1) {
    *dup = INVALID_HANDLE_VALUE;
    return ERROR_INVALID_HANDLE;
  }

  handle = uv__get_osfhandle(fd);
  return uv__duplicate_handle(loop, handle, dup);
}


int uv__create_nul_handle(HANDLE* handle_ptr,
    DWORD access) {
  HANDLE handle;
  SECURITY_ATTRIBUTES sa;

  sa.nLength = sizeof sa;
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;

  handle = CreateFileW(L"NUL",
                       access,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       &sa,
                       OPEN_EXISTING,
                       0,
                       NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    return GetLastError();
  }

  *handle_ptr = handle;
  return 0;
}


int uv__stdio_create(uv_loop_t* loop,
                     const uv_process_options_t* options,
                     BYTE** buffer_ptr) {
  BYTE* buffer;
  int count, i;
  int err;

  count = options->stdio_count;

  if (count < 0 || count > 255) {
    /* Only support FDs 0-255 */
    return ERROR_NOT_SUPPORTED;
  } else if (count < 3) {
    /* There should always be at least 3 stdio handles. */
    count = 3;
  }

  /* Allocate the child stdio buffer */
  buffer = (BYTE*) uv__malloc(CHILD_STDIO_SIZE(count));
  if (buffer == NULL) {
    return ERROR_OUTOFMEMORY;
  }

  /* Prepopulate the buffer with INVALID_HANDLE_VALUE handles so we can clean
   * up on failure. */
  CHILD_STDIO_COUNT(buffer) = count;
  for (i = 0; i < count; i++) {
    CHILD_STDIO_CRT_FLAGS(buffer, i) = 0;
    memset(CHILD_STDIO_HANDLE(buffer, i), 0xFF, sizeof(HANDLE));
  }

  for (i = 0; i < count; i++) {
    uv_stdio_container_t fdopt;
    if (i < options->stdio_count) {
      fdopt = options->stdio[i];
    } else {
      fdopt.flags = UV_IGNORE;
    }

    switch (fdopt.flags & (UV_IGNORE | UV_CREATE_PIPE | UV_INHERIT_FD |
            UV_INHERIT_STREAM)) {
      case UV_IGNORE:
        /* Starting a process with no stdin/stout/stderr can confuse it. So no
         * matter what the user specified, we make sure the first three FDs are
         * always open in their typical modes, e. g. stdin be readable and
         * stdout/err should be writable. For FDs > 2, don't do anything - all
         * handles in the stdio buffer are initialized with.
         * INVALID_HANDLE_VALUE, which should be okay. */
        if (i <= 2) {
          HANDLE nul;
          DWORD access = (i == 0) ? FILE_GENERIC_READ :
                                    FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES;

          err = uv__create_nul_handle(&nul, access);
          if (err)
            goto error;

		  memcpy(CHILD_STDIO_HANDLE(buffer, i), &nul, sizeof(HANDLE));
          CHILD_STDIO_CRT_FLAGS(buffer, i) = FOPEN | FDEV;
        }
        break;

      case UV_CREATE_PIPE: {
        /* Create a pair of two connected pipe ends; one end is turned into an
         * uv_pipe_t for use by the parent. The other one is given to the
         * child. */
        uv_pipe_t* parent_pipe = (uv_pipe_t*) fdopt.data.stream;
        HANDLE child_pipe = INVALID_HANDLE_VALUE;

        /* Create a new, connected pipe pair. stdio[i]. stream should point to
         * an uninitialized, but not connected pipe handle. */
        assert(fdopt.data.stream->type == UV_NAMED_PIPE);
        assert(!(fdopt.data.stream->flags & UV_HANDLE_CONNECTION));
        assert(!(fdopt.data.stream->flags & UV_HANDLE_PIPESERVER));

        err = uv__create_stdio_pipe_pair(loop,
                                         parent_pipe,
                                         &child_pipe,
                                         fdopt.flags);
        if (err)
          goto error;

		memcpy(CHILD_STDIO_HANDLE(buffer, i), &child_pipe, sizeof(HANDLE));
        CHILD_STDIO_CRT_FLAGS(buffer, i) = FOPEN | FPIPE;
        break;
      }

      case UV_INHERIT_FD: {
        /* Inherit a raw FD. */
        HANDLE child_handle;

        /* Make an inheritable duplicate of the handle. */
        err = uv__duplicate_fd(loop, fdopt.data.fd, &child_handle);
        if (err) {
          /* If fdopt. data. fd is not valid and fd <= 2, then ignore the
           * error. */
          if (fdopt.data.fd <= 2 && err == ERROR_INVALID_HANDLE) {
            CHILD_STDIO_CRT_FLAGS(buffer, i) = 0;
            memset(CHILD_STDIO_HANDLE(buffer, i), 0xFF, sizeof(HANDLE));
            break;
          }
          goto error;
        }

        /* Figure out what the type is. */
        switch (GetFileType(child_handle)) {
          case FILE_TYPE_DISK:
            CHILD_STDIO_CRT_FLAGS(buffer, i) = FOPEN;
            break;

          case FILE_TYPE_PIPE:
            CHILD_STDIO_CRT_FLAGS(buffer, i) = FOPEN | FPIPE;
            break;

          case FILE_TYPE_CHAR:
          case FILE_TYPE_REMOTE:
            CHILD_STDIO_CRT_FLAGS(buffer, i) = FOPEN | FDEV;
            break;

          case FILE_TYPE_UNKNOWN:
            if (GetLastError() != 0) {
              err = GetLastError();
              CloseHandle(child_handle);
              goto error;
            }
            CHILD_STDIO_CRT_FLAGS(buffer, i) = FOPEN | FDEV;
            break;

          default:
            assert(0);
            return -1;
        }

		memcpy(CHILD_STDIO_HANDLE(buffer, i), &child_handle, sizeof(HANDLE));
        break;
      }

      case UV_INHERIT_STREAM: {
        /* Use an existing stream as the stdio handle for the child. */
        HANDLE stream_handle, child_handle;
        unsigned char crt_flags;
        uv_stream_t* stream = fdopt.data.stream;

        /* Leech the handle out of the stream. */
        if (stream->type == UV_TTY) {
          stream_handle = ((uv_tty_t*) stream)->handle;
          crt_flags = FOPEN | FDEV;
        } else if (stream->type == UV_NAMED_PIPE &&
                   stream->flags & UV_HANDLE_CONNECTION) {
          stream_handle = ((uv_pipe_t*) stream)->handle;
          crt_flags = FOPEN | FPIPE;
        } else {
          stream_handle = INVALID_HANDLE_VALUE;
          crt_flags = 0;
        }

        if (stream_handle == NULL ||
            stream_handle == INVALID_HANDLE_VALUE) {
          /* The handle is already closed, or not yet created, or the stream
           * type is not supported. */
          err = ERROR_NOT_SUPPORTED;
          goto error;
        }

        /* Make an inheritable copy of the handle. */
        err = uv__duplicate_handle(loop, stream_handle, &child_handle);
        if (err)
          goto error;

		memcpy(CHILD_STDIO_HANDLE(buffer, i), &child_handle, sizeof(HANDLE));
        CHILD_STDIO_CRT_FLAGS(buffer, i) = crt_flags;
        break;
      }

      default:
        assert(0);
        return -1;
    }
  }

  *buffer_ptr  = buffer;
  return 0;

 error:
  uv__stdio_destroy(buffer);
  return err;
}


void uv__stdio_destroy(BYTE* buffer) {
  int i, count;

  count = CHILD_STDIO_COUNT(buffer);
  for (i = 0; i < count; i++) {
    HANDLE handle = uv__stdio_handle(buffer, i);
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }

  uv__free(buffer);
}


void uv__stdio_noinherit(BYTE* buffer) {
  int i, count;

  count = CHILD_STDIO_COUNT(buffer);
  for (i = 0; i < count; i++) {
    HANDLE handle = uv__stdio_handle(buffer, i);
    if (handle != INVALID_HANDLE_VALUE) {
      SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);
    }
  }
}


int uv__stdio_verify(BYTE* buffer, WORD size) {
  unsigned int count;

  /* Check the buffer pointer. */
  if (buffer == NULL)
    return 0;

  /* Verify that the buffer is at least big enough to hold the count. */
  if (size < CHILD_STDIO_SIZE(0))
    return 0;

  /* Verify if the count is within range. */
  count = CHILD_STDIO_COUNT(buffer);
  if (count > 256)
    return 0;

  /* Verify that the buffer size is big enough to hold info for N FDs. */
  if (size < CHILD_STDIO_SIZE(count))
    return 0;

  return 1;
}


WORD uv__stdio_size(BYTE* buffer) {
  return (WORD) CHILD_STDIO_SIZE(CHILD_STDIO_COUNT((buffer)));
}


HANDLE uv__stdio_handle(BYTE* buffer, int fd) {
  HANDLE handle;
  memcpy(&handle, CHILD_STDIO_HANDLE(buffer, fd), sizeof(HANDLE));
  return handle;
}
