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
#include <stdio.h>
#include <string.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "req-inl.h"


const unsigned int uv_directory_watcher_buffer_size = 4096;


static void uv__fs_event_queue_readdirchanges(uv_loop_t* loop,
    uv_fs_event_t* handle) {
  assert(handle->dir_handle != INVALID_HANDLE_VALUE);
  assert(!handle->req_pending);

  memset(&(handle->req.u.io.overlapped), 0,
         sizeof(handle->req.u.io.overlapped));
  if (!ReadDirectoryChangesW(handle->dir_handle,
                             handle->buffer,
                             uv_directory_watcher_buffer_size,
                             (handle->flags & UV_FS_EVENT_RECURSIVE) ? TRUE : FALSE,
                             FILE_NOTIFY_CHANGE_FILE_NAME      |
                               FILE_NOTIFY_CHANGE_DIR_NAME     |
                               FILE_NOTIFY_CHANGE_ATTRIBUTES   |
                               FILE_NOTIFY_CHANGE_SIZE         |
                               FILE_NOTIFY_CHANGE_LAST_WRITE   |
                               FILE_NOTIFY_CHANGE_LAST_ACCESS  |
                               FILE_NOTIFY_CHANGE_CREATION     |
                               FILE_NOTIFY_CHANGE_SECURITY,
                             NULL,
                             &handle->req.u.io.overlapped,
                             NULL)) {
    /* Make this req pending reporting an error. */
    SET_REQ_ERROR(&handle->req, GetLastError());
    uv__insert_pending_req(loop, (uv_req_t*)&handle->req);
  }

  handle->req_pending = 1;
}

static void uv__relative_path(const WCHAR* filename,
                              const WCHAR* dir,
                              WCHAR** relpath) {
  size_t relpathlen;
  size_t filenamelen = wcslen(filename);
  size_t dirlen = wcslen(dir);
  assert(!_wcsnicmp(filename, dir, dirlen));
  if (dirlen > 0 && dir[dirlen - 1] == '\\')
    dirlen--;
  relpathlen = filenamelen - dirlen - 1;
  *relpath = uv__malloc((relpathlen + 1) * sizeof(WCHAR));
  if (!*relpath)
    uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
  wcsncpy(*relpath, filename + dirlen + 1, relpathlen);
  (*relpath)[relpathlen] = L'\0';
}

static int uv__split_path(const WCHAR* filename, WCHAR** dir,
    WCHAR** file) {
  size_t len, i;
  DWORD dir_len;

  if (filename == NULL) {
    if (dir != NULL)
      *dir = NULL;
    *file = NULL;
    return 0;
  }

  len = wcslen(filename);
  i = len;
  while (i > 0 && filename[--i] != '\\' && filename[i] != '/');

  if (i == 0) {
    if (dir) {
      dir_len = GetCurrentDirectoryW(0, NULL);
      if (dir_len == 0) {
        return -1;
      }
      *dir = (WCHAR*)uv__malloc(dir_len * sizeof(WCHAR));
      if (!*dir) {
        uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
      }

      if (!GetCurrentDirectoryW(dir_len, *dir)) {
        uv__free(*dir);
        *dir = NULL;
        return -1;
      }
    }

    *file = _wcsdup(filename);
  } else {
    if (dir) {
      *dir = (WCHAR*)uv__malloc((i + 2) * sizeof(WCHAR));
      if (!*dir) {
        uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
      }
      wcsncpy(*dir, filename, i + 1);
      (*dir)[i + 1] = L'\0';
    }

    *file = (WCHAR*)uv__malloc((len - i) * sizeof(WCHAR));
    if (!*file) {
      uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
    }
    wcsncpy(*file, filename + i + 1, len - i - 1);
    (*file)[len - i - 1] = L'\0';
  }

  return 0;
}


int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_FS_EVENT);
  handle->dir_handle = INVALID_HANDLE_VALUE;
  handle->buffer = NULL;
  handle->req_pending = 0;
  handle->filew = NULL;
  handle->short_filew = NULL;
  handle->dirw = NULL;

  UV_REQ_INIT(&handle->req, UV_FS_EVENT_REQ);
  handle->req.data = handle;

  return 0;
}


int uv_fs_event_start(uv_fs_event_t* handle,
                      uv_fs_event_cb cb,
                      const char* path,
                      unsigned int flags) {
  int is_path_dir;
  size_t size;
  DWORD attr, last_error;
  WCHAR* dir = NULL, *dir_to_watch, *pathw = NULL;
  DWORD short_path_buffer_len;
  WCHAR *short_path_buffer;
  WCHAR* short_path, *long_path;

  short_path = NULL;
  if (uv__is_active(handle))
    return UV_EINVAL;

  handle->cb = cb;
  handle->path = uv__strdup(path);
  if (!handle->path) {
    uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
  }

  uv__handle_start(handle);

  last_error = uv__convert_utf8_to_utf16(path, &pathw);
  if (last_error)
    goto error_uv;

  /* Determine whether path is a file or a directory. */
  attr = GetFileAttributesW(pathw);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    last_error = GetLastError();
    goto error;
  }

  is_path_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

  if (is_path_dir) {
     /* path is a directory, so that's the directory that we will watch. */

    /* Convert to long path. */
    size = GetLongPathNameW(pathw, NULL, 0);

    if (size) {
      long_path = (WCHAR*)uv__malloc(size * sizeof(WCHAR));
      if (!long_path) {
        uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
      }

      size = GetLongPathNameW(pathw, long_path, size);
      if (size) {
        long_path[size] = '\0';
      } else {
        uv__free(long_path);
        long_path = NULL;
      }

      if (long_path) {
        uv__free(pathw);
        pathw = long_path;
      }
    }

    dir_to_watch = pathw;
  } else {
    /*
     * path is a file.  So we split path into dir & file parts, and
     * watch the dir directory.
     */

    /* Convert to short path. */
    short_path_buffer = NULL;
    short_path_buffer_len = GetShortPathNameW(pathw, NULL, 0);
    if (short_path_buffer_len == 0) {
      goto short_path_done;
    }
    short_path_buffer = uv__malloc(short_path_buffer_len * sizeof(WCHAR));
    if (short_path_buffer == NULL) {
      goto short_path_done;
    }
    if (GetShortPathNameW(pathw,
                          short_path_buffer,
                          short_path_buffer_len) == 0) {
      uv__free(short_path_buffer);
      short_path_buffer = NULL;
    }
short_path_done:
    short_path = short_path_buffer;

    if (uv__split_path(pathw, &dir, &handle->filew) != 0) {
      last_error = GetLastError();
      goto error;
    }

    if (uv__split_path(short_path, NULL, &handle->short_filew) != 0) {
      last_error = GetLastError();
      goto error;
    }

    dir_to_watch = dir;
    uv__free(pathw);
    pathw = NULL;
  }

  handle->dir_handle = CreateFileW(dir_to_watch,
                                   FILE_LIST_DIRECTORY,
                                   FILE_SHARE_READ | FILE_SHARE_DELETE |
                                     FILE_SHARE_WRITE,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS |
                                     FILE_FLAG_OVERLAPPED,
                                   NULL);

  if (dir) {
    uv__free(dir);
    dir = NULL;
  }

  if (handle->dir_handle == INVALID_HANDLE_VALUE) {
    last_error = GetLastError();
    goto error;
  }

  if (CreateIoCompletionPort(handle->dir_handle,
                             handle->loop->iocp,
                             (ULONG_PTR)handle,
                             0) == NULL) {
    last_error = GetLastError();
    goto error;
  }

  if (!handle->buffer) {
    handle->buffer = (char*)uv__malloc(uv_directory_watcher_buffer_size);
  }
  if (!handle->buffer) {
    uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
  }

  memset(&(handle->req.u.io.overlapped), 0,
         sizeof(handle->req.u.io.overlapped));

  if (!ReadDirectoryChangesW(handle->dir_handle,
                             handle->buffer,
                             uv_directory_watcher_buffer_size,
                             (flags & UV_FS_EVENT_RECURSIVE) ? TRUE : FALSE,
                             FILE_NOTIFY_CHANGE_FILE_NAME      |
                               FILE_NOTIFY_CHANGE_DIR_NAME     |
                               FILE_NOTIFY_CHANGE_ATTRIBUTES   |
                               FILE_NOTIFY_CHANGE_SIZE         |
                               FILE_NOTIFY_CHANGE_LAST_WRITE   |
                               FILE_NOTIFY_CHANGE_LAST_ACCESS  |
                               FILE_NOTIFY_CHANGE_CREATION     |
                               FILE_NOTIFY_CHANGE_SECURITY,
                             NULL,
                             &handle->req.u.io.overlapped,
                             NULL)) {
    last_error = GetLastError();
    goto error;
  }

  assert(is_path_dir ? pathw != NULL : pathw == NULL);
  handle->dirw = pathw;
  handle->req_pending = 1;
  return 0;

error:
  last_error = uv_translate_sys_error(last_error);

error_uv:
  if (handle->path) {
    uv__free(handle->path);
    handle->path = NULL;
  }

  if (handle->filew) {
    uv__free(handle->filew);
    handle->filew = NULL;
  }

  if (handle->short_filew) {
    uv__free(handle->short_filew);
    handle->short_filew = NULL;
  }

  uv__free(pathw);

  if (handle->dir_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle->dir_handle);
    handle->dir_handle = INVALID_HANDLE_VALUE;
  }

  if (handle->buffer) {
    uv__free(handle->buffer);
    handle->buffer = NULL;
  }

  if (uv__is_active(handle))
    uv__handle_stop(handle);

  uv__free(short_path);

  return last_error;
}


int uv_fs_event_stop(uv_fs_event_t* handle) {
  if (!uv__is_active(handle))
    return 0;

  if (handle->dir_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle->dir_handle);
    handle->dir_handle = INVALID_HANDLE_VALUE;
  }

  uv__handle_stop(handle);

  if (handle->filew) {
    uv__free(handle->filew);
    handle->filew = NULL;
  }

  if (handle->short_filew) {
    uv__free(handle->short_filew);
    handle->short_filew = NULL;
  }

  if (handle->path) {
    uv__free(handle->path);
    handle->path = NULL;
  }

  if (handle->dirw) {
    uv__free(handle->dirw);
    handle->dirw = NULL;
  }

  return 0;
}


static int file_info_cmp(WCHAR* str, WCHAR* file_name, size_t file_name_len) {
  size_t str_len;

  if (str == NULL)
    return -1;

  str_len = wcslen(str);

  /*
    Since we only care about equality, return early if the strings
    aren't the same length
  */
  if (str_len != (file_name_len / sizeof(WCHAR)))
    return -1;

  return _wcsnicmp(str, file_name, str_len);
}


void uv__process_fs_event_req(uv_loop_t* loop, uv_req_t* req,
    uv_fs_event_t* handle) {
  FILE_NOTIFY_INFORMATION* file_info;
  int err, sizew, size;
  char* filename = NULL;
  WCHAR* filenamew = NULL;
  WCHAR* long_filenamew = NULL;
  DWORD offset = 0;

  assert(req->type == UV_FS_EVENT_REQ);
  assert(handle->req_pending);
  handle->req_pending = 0;

  /* Don't report any callbacks if:
   * - We're closing, just push the handle onto the endgame queue
   * - We are not active, just ignore the callback
   */
  if (!uv__is_active(handle)) {
    if (handle->flags & UV_HANDLE_CLOSING) {
      uv__want_endgame(loop, (uv_handle_t*) handle);
    }
    return;
  }

  file_info = (FILE_NOTIFY_INFORMATION*)(handle->buffer + offset);

  if (REQ_SUCCESS(req)) {
    if (req->u.io.overlapped.InternalHigh > 0) {
      do {
        file_info = (FILE_NOTIFY_INFORMATION*)((char*)file_info + offset);
        assert(!filename);
        assert(!filenamew);
        assert(!long_filenamew);

        /*
         * Fire the event only if we were asked to watch a directory,
         * or if the filename filter matches.
         */
        if (handle->dirw ||
            file_info_cmp(handle->filew,
                          file_info->FileName,
                          file_info->FileNameLength) == 0 ||
            file_info_cmp(handle->short_filew,
                          file_info->FileName,
                          file_info->FileNameLength) == 0) {

          if (handle->dirw) {
            /*
             * We attempt to resolve the long form of the file name explicitly.
             * We only do this for file names that might still exist on disk.
             * If this fails, we use the name given by ReadDirectoryChangesW.
             * This may be the long form or the 8.3 short name in some cases.
             */
            if (file_info->Action != FILE_ACTION_REMOVED &&
              file_info->Action != FILE_ACTION_RENAMED_OLD_NAME) {
              /* Construct a full path to the file. */
              size = wcslen(handle->dirw) +
                file_info->FileNameLength / sizeof(WCHAR) + 2;

              filenamew = (WCHAR*)uv__malloc(size * sizeof(WCHAR));
              if (!filenamew) {
                uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
              }

              _snwprintf(filenamew, size, L"%s\\%.*s", handle->dirw,
                file_info->FileNameLength / (DWORD)sizeof(WCHAR),
                file_info->FileName);

              filenamew[size - 1] = L'\0';

              /* Convert to long name. */
              size = GetLongPathNameW(filenamew, NULL, 0);

              if (size) {
                long_filenamew = (WCHAR*)uv__malloc(size * sizeof(WCHAR));
                if (!long_filenamew) {
                  uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
                }

                size = GetLongPathNameW(filenamew, long_filenamew, size);
                if (size) {
                  long_filenamew[size] = '\0';
                } else {
                  uv__free(long_filenamew);
                  long_filenamew = NULL;
                }
              }

              uv__free(filenamew);

              if (long_filenamew) {
                /* Get the file name out of the long path. */
                uv__relative_path(long_filenamew,
                                  handle->dirw,
                                  &filenamew);
                uv__free(long_filenamew);
                long_filenamew = filenamew;
                sizew = -1;
              } else {
                /* We couldn't get the long filename, use the one reported. */
                filenamew = file_info->FileName;
                sizew = file_info->FileNameLength / sizeof(WCHAR);
              }
            } else {
              /*
               * Removed or renamed events cannot be resolved to the long form.
               * We therefore use the name given by ReadDirectoryChangesW.
               * This may be the long form or the 8.3 short name in some cases.
               */
              filenamew = file_info->FileName;
              sizew = file_info->FileNameLength / sizeof(WCHAR);
            }
          } else {
            /* We already have the long name of the file, so just use it. */
            filenamew = handle->filew;
            sizew = -1;
          }

          /* Convert the filename to utf8. */
          uv__convert_utf16_to_utf8(filenamew, sizew, &filename);

          switch (file_info->Action) {
            case FILE_ACTION_ADDED:
            case FILE_ACTION_REMOVED:
            case FILE_ACTION_RENAMED_OLD_NAME:
            case FILE_ACTION_RENAMED_NEW_NAME:
              handle->cb(handle, filename, UV_RENAME, 0);
              break;

            case FILE_ACTION_MODIFIED:
              handle->cb(handle, filename, UV_CHANGE, 0);
              break;
          }

          uv__free(filename);
          filename = NULL;
          uv__free(long_filenamew);
          long_filenamew = NULL;
          filenamew = NULL;
        }

        offset = file_info->NextEntryOffset;
      } while (offset && !(handle->flags & UV_HANDLE_CLOSING));
    } else {
      handle->cb(handle, NULL, UV_CHANGE, 0);
    }
  } else {
    err = GET_REQ_ERROR(req);
    handle->cb(handle, NULL, 0, uv_translate_sys_error(err));
  }

  if (handle->flags & UV_HANDLE_CLOSING) {
    uv__want_endgame(loop, (uv_handle_t*)handle);
  } else if (uv__is_active(handle)) {
    uv__fs_event_queue_readdirchanges(loop, handle);
  }
}


void uv__fs_event_close(uv_loop_t* loop, uv_fs_event_t* handle) {
  uv_fs_event_stop(handle);

  uv__handle_closing(handle);

  if (!handle->req_pending) {
    uv__want_endgame(loop, (uv_handle_t*)handle);
  }

}


void uv__fs_event_endgame(uv_loop_t* loop, uv_fs_event_t* handle) {
  if ((handle->flags & UV_HANDLE_CLOSING) && !handle->req_pending) {
    assert(!(handle->flags & UV_HANDLE_CLOSED));

    if (handle->buffer) {
      uv__free(handle->buffer);
      handle->buffer = NULL;
    }

    uv__handle_close(handle);
  }
}
