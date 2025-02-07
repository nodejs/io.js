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
#include <stdlib.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <stdio.h>

#include "uv.h"

/* <winioctl.h> requires <windows.h>, included via "uv.h" above, but needs to
   be included before our "winapi.h", included via "internal.h" below. */
#include <winioctl.h>

#include "internal.h"
#include "req-inl.h"
#include "handle-inl.h"
#include "fs-fd-hash-inl.h"


#define UV_FS_FREE_PATHS         0x0002
#define UV_FS_FREE_PTR           0x0008
#define UV_FS_CLEANEDUP          0x0010

#ifndef FILE_DISPOSITION_DELETE
#define FILE_DISPOSITION_DELETE                     0x0001
#endif  /* FILE_DISPOSITION_DELETE */

#ifndef FILE_DISPOSITION_POSIX_SEMANTICS
#define FILE_DISPOSITION_POSIX_SEMANTICS            0x0002
#endif  /* FILE_DISPOSITION_POSIX_SEMANTICS */

#ifndef FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE  0x0010
#endif  /* FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE */

NTSTATUS uv__RtlUnicodeStringInit(
  PUNICODE_STRING DestinationString,
  PWSTR SourceString,
  size_t SourceStringLen
) {
  if (SourceStringLen > 0x7FFF)
    return STATUS_INVALID_PARAMETER;
  DestinationString->MaximumLength = DestinationString->Length =
    SourceStringLen * sizeof(SourceString[0]);
  DestinationString->Buffer = SourceString;
  return STATUS_SUCCESS;
}

#define INIT(subtype)                                                         \
  do {                                                                        \
    if (req == NULL)                                                          \
      return UV_EINVAL;                                                       \
    uv__fs_req_init(loop, req, subtype, cb);                                  \
  }                                                                           \
  while (0)

#define POST                                                                  \
  do {                                                                        \
    if (cb != NULL) {                                                         \
      uv__req_register(loop);                                                 \
      uv__work_submit(loop,                                                   \
                      &req->work_req,                                         \
                      UV__WORK_FAST_IO,                                       \
                      uv__fs_work,                                            \
                      uv__fs_done);                                           \
      return 0;                                                               \
    } else {                                                                  \
      uv__fs_work(&req->work_req);                                            \
      return req->result;                                                     \
    }                                                                         \
  }                                                                           \
  while (0)

#define SET_REQ_RESULT(req, result_value)                                   \
  do {                                                                      \
    req->result = (result_value);                                           \
    assert(req->result != -1);                                              \
  } while (0)

#define SET_REQ_WIN32_ERROR(req, sys_errno)                                 \
  do {                                                                      \
    req->sys_errno_ = (sys_errno);                                          \
    req->result = uv_translate_sys_error(req->sys_errno_);                  \
  } while (0)

#define SET_REQ_UV_ERROR(req, uv_errno, sys_errno)                          \
  do {                                                                      \
    req->result = (uv_errno);                                               \
    req->sys_errno_ = (sys_errno);                                          \
  } while (0)

#define VERIFY_FD(fd, req)                                                  \
  if (fd == -1) {                                                           \
    req->result = UV_EBADF;                                                 \
    req->sys_errno_ = ERROR_INVALID_HANDLE;                                 \
    return;                                                                 \
  }

#define NSEC_PER_TICK 100
#define TICKS_PER_SEC ((int64_t) 1e9 / NSEC_PER_TICK)
static const int64_t WIN_TO_UNIX_TICK_OFFSET = 11644473600 * TICKS_PER_SEC;

static void uv__filetime_to_timespec(uv_timespec_t *ts, int64_t filetime) {
  filetime -= WIN_TO_UNIX_TICK_OFFSET;
  ts->tv_sec = filetime / TICKS_PER_SEC;
  ts->tv_nsec = (filetime % TICKS_PER_SEC) * NSEC_PER_TICK;
  if (ts->tv_nsec < 0) {
    ts->tv_sec -= 1;
    ts->tv_nsec += 1e9;
  }
}

#define TIME_T_TO_FILETIME(time, filetime_ptr)                              \
  do {                                                                      \
    int64_t bigtime = ((time) * TICKS_PER_SEC + WIN_TO_UNIX_TICK_OFFSET);   \
    (filetime_ptr)->dwLowDateTime = (uint64_t) bigtime & 0xFFFFFFFF;        \
    (filetime_ptr)->dwHighDateTime = (uint64_t) bigtime >> 32;              \
  } while(0)

#define IS_SLASH(c) ((c) == L'\\' || (c) == L'/')
#define IS_LETTER(c) (((c) >= L'a' && (c) <= L'z') || \
  ((c) >= L'A' && (c) <= L'Z'))

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

const WCHAR JUNCTION_PREFIX[] = L"\\??\\";
const WCHAR JUNCTION_PREFIX_LEN = 4;

const WCHAR LONG_PATH_PREFIX[] = L"\\\\?\\";
const WCHAR LONG_PATH_PREFIX_LEN = 4;

const WCHAR UNC_PATH_PREFIX[] = L"\\\\?\\UNC\\";
const WCHAR UNC_PATH_PREFIX_LEN = 8;

static int uv__file_symlink_usermode_flag = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;

static DWORD uv__allocation_granularity;

typedef enum {
  FS__STAT_PATH_SUCCESS,
  FS__STAT_PATH_ERROR,
  FS__STAT_PATH_TRY_SLOW
} fs__stat_path_return_t;

INLINE static void fs__stat_assign_statbuf_null(uv_stat_t* statbuf);
INLINE static void fs__stat_assign_statbuf(uv_stat_t* statbuf,
    FILE_STAT_BASIC_INFORMATION stat_info, int do_lstat);


void uv__fs_init(void) {
  SYSTEM_INFO system_info;

  GetSystemInfo(&system_info);
  uv__allocation_granularity = system_info.dwAllocationGranularity;

  uv__fd_hash_init();
}


INLINE static int fs__readlink_handle(HANDLE handle,
                                      char** target_ptr,
                                      size_t* target_len_ptr) {
  char buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
  REPARSE_DATA_BUFFER* reparse_data = (REPARSE_DATA_BUFFER*) buffer;
  WCHAR* w_target;
  DWORD w_target_len;
  DWORD bytes;
  size_t i;
  size_t len;

  if (!DeviceIoControl(handle,
                       FSCTL_GET_REPARSE_POINT,
                       NULL,
                       0,
                       buffer,
                       sizeof buffer,
                       &bytes,
                       NULL)) {
    return -1;
  }

  if (reparse_data->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
    /* Real symlink */
    w_target = reparse_data->SymbolicLinkReparseBuffer.PathBuffer +
        (reparse_data->SymbolicLinkReparseBuffer.SubstituteNameOffset /
        sizeof(WCHAR));
    w_target_len =
        reparse_data->SymbolicLinkReparseBuffer.SubstituteNameLength /
        sizeof(WCHAR);

    /* Real symlinks can contain pretty much everything, but the only thing we
     * really care about is undoing the implicit conversion to an NT namespaced
     * path that CreateSymbolicLink will perform on absolute paths. If the path
     * is win32-namespaced then the user must have explicitly made it so, and
     * we better just return the unmodified reparse data. */
    if (w_target_len >= 4 &&
        w_target[0] == L'\\' &&
        w_target[1] == L'?' &&
        w_target[2] == L'?' &&
        w_target[3] == L'\\') {
      /* Starts with \??\ */
      if (w_target_len >= 6 &&
          ((w_target[4] >= L'A' && w_target[4] <= L'Z') ||
           (w_target[4] >= L'a' && w_target[4] <= L'z')) &&
          w_target[5] == L':' &&
          (w_target_len == 6 || w_target[6] == L'\\')) {
        /* \??\<drive>:\ */
        w_target += 4;
        w_target_len -= 4;

      } else if (w_target_len >= 8 &&
                 (w_target[4] == L'U' || w_target[4] == L'u') &&
                 (w_target[5] == L'N' || w_target[5] == L'n') &&
                 (w_target[6] == L'C' || w_target[6] == L'c') &&
                 w_target[7] == L'\\') {
        /* \??\UNC\<server>\<share>\ - make sure the final path looks like
         * \\<server>\<share>\ */
        w_target += 6;
        w_target[0] = L'\\';
        w_target_len -= 6;
      }
    }

  } else if (reparse_data->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
    /* Junction. */
    w_target = reparse_data->MountPointReparseBuffer.PathBuffer +
        (reparse_data->MountPointReparseBuffer.SubstituteNameOffset /
        sizeof(WCHAR));
    w_target_len = reparse_data->MountPointReparseBuffer.SubstituteNameLength /
        sizeof(WCHAR);

    /* Only treat junctions that look like \??\<drive>:\ as symlink. Junctions
     * can also be used as mount points, like \??\Volume{<guid>}, but that's
     * confusing for programs since they wouldn't be able to actually
     * understand such a path when returned by uv_readlink(). UNC paths are
     * never valid for junctions so we don't care about them. */
    if (!(w_target_len >= 6 &&
          w_target[0] == L'\\' &&
          w_target[1] == L'?' &&
          w_target[2] == L'?' &&
          w_target[3] == L'\\' &&
          ((w_target[4] >= L'A' && w_target[4] <= L'Z') ||
           (w_target[4] >= L'a' && w_target[4] <= L'z')) &&
          w_target[5] == L':' &&
          (w_target_len == 6 || w_target[6] == L'\\'))) {
      SetLastError(ERROR_SYMLINK_NOT_SUPPORTED);
      return -1;
    }

    /* Remove leading \??\ */
    w_target += 4;
    w_target_len -= 4;

  } else if (reparse_data->ReparseTag == IO_REPARSE_TAG_APPEXECLINK) {
    /* String #3 in the list has the target filename. */
    if (reparse_data->AppExecLinkReparseBuffer.StringCount < 3) {
      SetLastError(ERROR_SYMLINK_NOT_SUPPORTED);
      return -1;
    }
    w_target = reparse_data->AppExecLinkReparseBuffer.StringList;
    /* The StringList buffer contains a list of strings separated by "\0",   */
    /* with "\0\0" terminating the list. Move to the 3rd string in the list: */
    for (i = 0; i < 2; ++i) {
      len = wcslen(w_target);
      if (len == 0) {
        SetLastError(ERROR_SYMLINK_NOT_SUPPORTED);
        return -1;
      }
      w_target += len + 1;
    }
    w_target_len = wcslen(w_target);
    if (w_target_len == 0) {
      SetLastError(ERROR_SYMLINK_NOT_SUPPORTED);
      return -1;
    }
    /* Make sure it is an absolute path. */
    if (!(w_target_len >= 3 &&
         ((w_target[0] >= L'a' && w_target[0] <= L'z') ||
          (w_target[0] >= L'A' && w_target[0] <= L'Z')) &&
         w_target[1] == L':' &&
         w_target[2] == L'\\')) {
      SetLastError(ERROR_SYMLINK_NOT_SUPPORTED);
      return -1;
    }

  } else {
    /* Reparse tag does not indicate a symlink. */
    SetLastError(ERROR_SYMLINK_NOT_SUPPORTED);
    return -1;
  }

  assert(target_ptr == NULL || *target_ptr == NULL);
  return uv_utf16_to_wtf8(w_target, w_target_len, target_ptr, target_len_ptr);
}


INLINE static int fs__capture_path(uv_fs_t* req, const char* path,
    const char* new_path, const int copy_path) {
  WCHAR* buf;
  WCHAR* pos;
  size_t buf_sz = 0;
  size_t path_len = 0;
  ssize_t pathw_len = 0;
  ssize_t new_pathw_len = 0;

  /* new_path can only be set if path is also set. */
  assert(new_path == NULL || path != NULL);

  if (path != NULL) {
    pathw_len = uv_wtf8_length_as_utf16(path);
    if (pathw_len < 0)
      return ERROR_INVALID_NAME;
    buf_sz += pathw_len * sizeof(WCHAR);
  }

  if (path != NULL && copy_path) {
    path_len = 1 + strlen(path);
    buf_sz += path_len;
  }

  if (new_path != NULL) {
    new_pathw_len = uv_wtf8_length_as_utf16(new_path);
    if (new_pathw_len < 0)
      return ERROR_INVALID_NAME;
    buf_sz += new_pathw_len * sizeof(WCHAR);
  }


  if (buf_sz == 0) {
    req->file.pathw = NULL;
    req->fs.info.new_pathw = NULL;
    req->path = NULL;
    return 0;
  }

  buf = uv__malloc(buf_sz);
  if (buf == NULL) {
    return ERROR_OUTOFMEMORY;
  }

  pos = buf;

  if (path != NULL) {
    uv_wtf8_to_utf16(path, pos, pathw_len);
    req->file.pathw = pos;
    pos += pathw_len;
  } else {
    req->file.pathw = NULL;
  }

  if (new_path != NULL) {
    uv_wtf8_to_utf16(new_path, pos, new_pathw_len);
    req->fs.info.new_pathw = pos;
    pos += new_pathw_len;
  } else {
    req->fs.info.new_pathw = NULL;
  }

  req->path = path;
  if (path != NULL && copy_path) {
    memcpy(pos, path, path_len);
    assert(path_len == buf_sz - (pos - buf) * sizeof(WCHAR));
    req->path = (char*) pos;
  }

  req->flags |= UV_FS_FREE_PATHS;

  return 0;
}


INLINE static void uv__fs_req_init(uv_loop_t* loop, uv_fs_t* req,
    uv_fs_type fs_type, const uv_fs_cb cb) {
  uv__once_init();
  UV_REQ_INIT(req, UV_FS);
  req->loop = loop;
  req->flags = 0;
  req->fs_type = fs_type;
  req->sys_errno_ = 0;
  req->result = 0;
  req->ptr = NULL;
  req->path = NULL;
  req->cb = cb;
  memset(&req->fs, 0, sizeof(req->fs));
}


void fs__open(uv_fs_t* req) {
  DWORD access;
  DWORD share;
  DWORD disposition;
  DWORD attributes = 0;
  HANDLE file;
  int fd, current_umask;
  int flags = req->fs.info.file_flags;
  struct uv__fd_info_s fd_info;

  /* Adjust flags to be compatible with the memory file mapping. Save the
   * original flags to emulate the correct behavior. */
  if (flags & UV_FS_O_FILEMAP) {
    fd_info.flags = flags;
    fd_info.current_pos.QuadPart = 0;

    if ((flags & (UV_FS_O_RDONLY | UV_FS_O_WRONLY | UV_FS_O_RDWR)) ==
        UV_FS_O_WRONLY) {
      /* CreateFileMapping always needs read access */
      flags = (flags & ~UV_FS_O_WRONLY) | UV_FS_O_RDWR;
    }

    if (flags & UV_FS_O_APPEND) {
      /* Clear the append flag and ensure RDRW mode */
      flags &= ~UV_FS_O_APPEND;
      flags &= ~(UV_FS_O_RDONLY | UV_FS_O_WRONLY | UV_FS_O_RDWR);
      flags |= UV_FS_O_RDWR;
    }
  }

  /* Obtain the active umask. umask() never fails and returns the previous
   * umask. */
  current_umask = _umask(0);
  _umask(current_umask);

  /* convert flags and mode to CreateFile parameters */
  switch (flags & (UV_FS_O_RDONLY | UV_FS_O_WRONLY | UV_FS_O_RDWR)) {
  case UV_FS_O_RDONLY:
    access = FILE_GENERIC_READ;
    break;
  case UV_FS_O_WRONLY:
    access = FILE_GENERIC_WRITE;
    break;
  case UV_FS_O_RDWR:
    access = FILE_GENERIC_READ | FILE_GENERIC_WRITE;
    break;
  default:
    goto einval;
  }

  if (flags & UV_FS_O_APPEND) {
    access &= ~FILE_WRITE_DATA;
    access |= FILE_APPEND_DATA;
  }

  /*
   * Here is where we deviate significantly from what CRT's _open()
   * does. We indiscriminately use all the sharing modes, to match
   * UNIX semantics. In particular, this ensures that the file can
   * be deleted even whilst it's open, fixing issue
   * https://github.com/nodejs/node-v0.x-archive/issues/1449.
   * We still support exclusive sharing mode, since it is necessary
   * for opening raw block devices, otherwise Windows will prevent
   * any attempt to write past the master boot record.
   */
  if (flags & UV_FS_O_EXLOCK) {
    share = 0;
  } else {
    share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  }

  switch (flags & (UV_FS_O_CREAT | UV_FS_O_EXCL | UV_FS_O_TRUNC)) {
  case 0:
  case UV_FS_O_EXCL:
    disposition = OPEN_EXISTING;
    break;
  case UV_FS_O_CREAT:
    disposition = OPEN_ALWAYS;
    break;
  case UV_FS_O_CREAT | UV_FS_O_EXCL:
  case UV_FS_O_CREAT | UV_FS_O_TRUNC | UV_FS_O_EXCL:
    disposition = CREATE_NEW;
    break;
  case UV_FS_O_TRUNC:
  case UV_FS_O_TRUNC | UV_FS_O_EXCL:
    disposition = TRUNCATE_EXISTING;
    break;
  case UV_FS_O_CREAT | UV_FS_O_TRUNC:
    disposition = CREATE_ALWAYS;
    break;
  default:
    goto einval;
  }

  attributes |= FILE_ATTRIBUTE_NORMAL;
  if (flags & UV_FS_O_CREAT) {
    if (!((req->fs.info.mode & ~current_umask) & _S_IWRITE)) {
      attributes |= FILE_ATTRIBUTE_READONLY;
    }
  }

  if (flags & UV_FS_O_TEMPORARY ) {
    attributes |= FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY;
    access |= DELETE;
  }

  if (flags & UV_FS_O_SHORT_LIVED) {
    attributes |= FILE_ATTRIBUTE_TEMPORARY;
  }

  switch (flags & (UV_FS_O_SEQUENTIAL | UV_FS_O_RANDOM)) {
  case 0:
    break;
  case UV_FS_O_SEQUENTIAL:
    attributes |= FILE_FLAG_SEQUENTIAL_SCAN;
    break;
  case UV_FS_O_RANDOM:
    attributes |= FILE_FLAG_RANDOM_ACCESS;
    break;
  default:
    goto einval;
  }

  if (flags & UV_FS_O_DIRECT) {
    /*
     * FILE_APPEND_DATA and FILE_FLAG_NO_BUFFERING are mutually exclusive.
     * Windows returns 87, ERROR_INVALID_PARAMETER if these are combined.
     *
     * FILE_APPEND_DATA is included in FILE_GENERIC_WRITE:
     *
     * FILE_GENERIC_WRITE = STANDARD_RIGHTS_WRITE |
     *                      FILE_WRITE_DATA |
     *                      FILE_WRITE_ATTRIBUTES |
     *                      FILE_WRITE_EA |
     *                      FILE_APPEND_DATA |
     *                      SYNCHRONIZE
     *
     * Note: Appends are also permitted by FILE_WRITE_DATA.
     *
     * In order for direct writes and direct appends to succeed, we therefore
     * exclude FILE_APPEND_DATA if FILE_WRITE_DATA is specified, and otherwise
     * fail if the user's sole permission is a direct append, since this
     * particular combination is invalid.
     */
    if (access & FILE_APPEND_DATA) {
      if (access & FILE_WRITE_DATA) {
        access &= ~FILE_APPEND_DATA;
      } else {
        goto einval;
      }
    }
    attributes |= FILE_FLAG_NO_BUFFERING;
  }

  switch (flags & (UV_FS_O_DSYNC | UV_FS_O_SYNC)) {
  case 0:
    break;
  case UV_FS_O_DSYNC:
  case UV_FS_O_SYNC:
    attributes |= FILE_FLAG_WRITE_THROUGH;
    break;
  default:
    goto einval;
  }

  /* Setting this flag makes it possible to open a directory. */
  attributes |= FILE_FLAG_BACKUP_SEMANTICS;

  file = CreateFileW(req->file.pathw,
                     access,
                     share,
                     NULL,
                     disposition,
                     attributes,
                     NULL);
  if (file == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    if (error == ERROR_FILE_EXISTS && (flags & UV_FS_O_CREAT) &&
        !(flags & UV_FS_O_EXCL)) {
      /* Special case: when ERROR_FILE_EXISTS happens and UV_FS_O_CREAT was
       * specified, it means the path referred to a directory. */
      SET_REQ_UV_ERROR(req, UV_EISDIR, error);
    } else {
      SET_REQ_WIN32_ERROR(req, GetLastError());
    }
    return;
  }

  fd = _open_osfhandle((intptr_t) file, flags);
  if (fd < 0) {
    /* The only known failure mode for _open_osfhandle() is EMFILE, in which
     * case GetLastError() will return zero. However we'll try to handle other
     * errors as well, should they ever occur.
     */
    if (errno == EMFILE)
      SET_REQ_UV_ERROR(req, UV_EMFILE, ERROR_TOO_MANY_OPEN_FILES);
    else if (GetLastError() != ERROR_SUCCESS)
      SET_REQ_WIN32_ERROR(req, GetLastError());
    else
      SET_REQ_WIN32_ERROR(req, (DWORD) UV_UNKNOWN);
    CloseHandle(file);
    return;
  }

  if (flags & UV_FS_O_FILEMAP) {
    FILE_STANDARD_INFO file_info;
    if (!GetFileInformationByHandleEx(file,
                                      FileStandardInfo,
                                      &file_info,
                                      sizeof file_info)) {
      SET_REQ_WIN32_ERROR(req, GetLastError());
      CloseHandle(file);
      return;
    }
    fd_info.is_directory = file_info.Directory;

    if (fd_info.is_directory) {
      fd_info.size.QuadPart = 0;
      fd_info.mapping = INVALID_HANDLE_VALUE;
    } else {
      if (!GetFileSizeEx(file, &fd_info.size)) {
        SET_REQ_WIN32_ERROR(req, GetLastError());
        CloseHandle(file);
        return;
      }

      if (fd_info.size.QuadPart == 0) {
        fd_info.mapping = INVALID_HANDLE_VALUE;
      } else {
        DWORD flProtect = (fd_info.flags & (UV_FS_O_RDONLY | UV_FS_O_WRONLY |
          UV_FS_O_RDWR)) == UV_FS_O_RDONLY ? PAGE_READONLY : PAGE_READWRITE;
        fd_info.mapping = CreateFileMapping(file,
                                            NULL,
                                            flProtect,
                                            fd_info.size.HighPart,
                                            fd_info.size.LowPart,
                                            NULL);
        if (fd_info.mapping == NULL) {
          SET_REQ_WIN32_ERROR(req, GetLastError());
          CloseHandle(file);
          return;
        }
      }
    }

    uv__fd_hash_add(fd, &fd_info);
  }

  SET_REQ_RESULT(req, fd);
  return;

 einval:
  SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
}

void fs__close(uv_fs_t* req) {
  int fd = req->file.fd;
  int result;
  struct uv__fd_info_s fd_info;

  VERIFY_FD(fd, req);

  if (uv__fd_hash_remove(fd, &fd_info)) {
    if (fd_info.mapping != INVALID_HANDLE_VALUE) {
      CloseHandle(fd_info.mapping);
    }
  }

  if (fd > 2)
    result = _close(fd);
  else
    result = 0;

  /* _close doesn't set _doserrno on failure, but it does always set errno
   * to EBADF on failure.
   */
  if (result == -1) {
    assert(errno == EBADF);
    SET_REQ_UV_ERROR(req, UV_EBADF, ERROR_INVALID_HANDLE);
  } else {
    SET_REQ_RESULT(req, 0);
  }
}


LONG fs__filemap_ex_filter(LONG excode, PEXCEPTION_POINTERS pep,
                           int* perror) {
  if (excode != (LONG)EXCEPTION_IN_PAGE_ERROR) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  assert(perror != NULL);
  if (pep != NULL && pep->ExceptionRecord != NULL &&
      pep->ExceptionRecord->NumberParameters >= 3) {
    NTSTATUS status = (NTSTATUS)pep->ExceptionRecord->ExceptionInformation[3];
    *perror = pRtlNtStatusToDosError(status);
    if (*perror != ERROR_SUCCESS) {
      return EXCEPTION_EXECUTE_HANDLER;
    }
  }
  *perror = UV_UNKNOWN;
  return EXCEPTION_EXECUTE_HANDLER;
}


void fs__read_filemap(uv_fs_t* req, struct uv__fd_info_s* fd_info) {
  int fd = req->file.fd; /* VERIFY_FD done in fs__read */
  int rw_flags = fd_info->flags &
    (UV_FS_O_RDONLY | UV_FS_O_WRONLY | UV_FS_O_RDWR);
  size_t read_size, done_read;
  unsigned int index;
  LARGE_INTEGER pos, end_pos;
  size_t view_offset;
  LARGE_INTEGER view_base;
  void* view;

  if (rw_flags == UV_FS_O_WRONLY) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_FLAGS);
    return;
  }
  if (fd_info->is_directory) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_FUNCTION);
    return;
  }

  if (req->fs.info.offset == -1) {
    pos = fd_info->current_pos;
  } else {
    pos.QuadPart = req->fs.info.offset;
  }

  /* Make sure we wont read past EOF. */
  if (pos.QuadPart >= fd_info->size.QuadPart) {
    SET_REQ_RESULT(req, 0);
    return;
  }

  read_size = 0;
  for (index = 0; index < req->fs.info.nbufs; ++index) {
    read_size += req->fs.info.bufs[index].len;
  }
  read_size = (size_t) MIN((LONGLONG) read_size,
                           fd_info->size.QuadPart - pos.QuadPart);
  if (read_size == 0) {
    SET_REQ_RESULT(req, 0);
    return;
  }

  end_pos.QuadPart = pos.QuadPart + read_size;

  view_offset = pos.QuadPart % uv__allocation_granularity;
  view_base.QuadPart = pos.QuadPart - view_offset;
  view = MapViewOfFile(fd_info->mapping,
                       FILE_MAP_READ,
                       view_base.HighPart,
                       view_base.LowPart,
                       view_offset + read_size);
  if (view == NULL) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  done_read = 0;
  for (index = 0;
       index < req->fs.info.nbufs && done_read < read_size;
       ++index) {
    size_t this_read_size = MIN(req->fs.info.bufs[index].len,
                                read_size - done_read);
#ifdef _MSC_VER
    int err = 0;
    __try {
#endif
      memcpy(req->fs.info.bufs[index].base,
             (char*)view + view_offset + done_read,
             this_read_size);
#ifdef _MSC_VER
    }
    __except (fs__filemap_ex_filter(GetExceptionCode(),
                                    GetExceptionInformation(), &err)) {
      SET_REQ_WIN32_ERROR(req, err);
      UnmapViewOfFile(view);
      return;
    }
#endif
    done_read += this_read_size;
  }
  assert(done_read == read_size);

  if (!UnmapViewOfFile(view)) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  if (req->fs.info.offset == -1) {
    fd_info->current_pos = end_pos;
    uv__fd_hash_add(fd, fd_info);
  }

  SET_REQ_RESULT(req, read_size);
  return;
}

void fs__read(uv_fs_t* req) {
  int fd = req->file.fd;
  int64_t offset = req->fs.info.offset;
  HANDLE handle;
  OVERLAPPED overlapped, *overlapped_ptr;
  LARGE_INTEGER offset_;
  DWORD bytes;
  DWORD error;
  int result;
  unsigned int index;
  LARGE_INTEGER original_position;
  LARGE_INTEGER zero_offset;
  int restore_position;
  struct uv__fd_info_s fd_info;

  VERIFY_FD(fd, req);

  if (uv__fd_hash_get(fd, &fd_info)) {
    fs__read_filemap(req, &fd_info);
    return;
  }

  zero_offset.QuadPart = 0;
  restore_position = 0;
  handle = uv__get_osfhandle(fd);

  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_HANDLE);
    return;
  }

  if (offset != -1) {
    memset(&overlapped, 0, sizeof overlapped);
    overlapped_ptr = &overlapped;
    if (SetFilePointerEx(handle, zero_offset, &original_position,
                         FILE_CURRENT)) {
      restore_position = 1;
    }
  } else {
    overlapped_ptr = NULL;
  }

  index = 0;
  bytes = 0;
  do {
    DWORD incremental_bytes;

    if (offset != -1) {
      offset_.QuadPart = offset + bytes;
      overlapped.Offset = offset_.LowPart;
      overlapped.OffsetHigh = offset_.HighPart;
    }

    result = ReadFile(handle,
                      req->fs.info.bufs[index].base,
                      req->fs.info.bufs[index].len,
                      &incremental_bytes,
                      overlapped_ptr);
    bytes += incremental_bytes;
    ++index;
  } while (result && index < req->fs.info.nbufs);

  if (restore_position)
    SetFilePointerEx(handle, original_position, NULL, FILE_BEGIN);

  if (result || bytes > 0) {
    SET_REQ_RESULT(req, bytes);
  } else {
    error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) {
      error = ERROR_INVALID_FLAGS;
    }

    if (error == ERROR_HANDLE_EOF || error == ERROR_BROKEN_PIPE) {
      SET_REQ_RESULT(req, bytes);
    } else {
      SET_REQ_WIN32_ERROR(req, error);
    }
  }
}


void fs__write_filemap(uv_fs_t* req, HANDLE file,
                       struct uv__fd_info_s* fd_info) {
  int fd = req->file.fd; /* VERIFY_FD done in fs__write */
  int force_append = fd_info->flags & UV_FS_O_APPEND;
  int rw_flags = fd_info->flags &
    (UV_FS_O_RDONLY | UV_FS_O_WRONLY | UV_FS_O_RDWR);
  size_t write_size, done_write;
  unsigned int index;
  LARGE_INTEGER pos, end_pos;
  size_t view_offset;
  LARGE_INTEGER view_base;
  void* view;
  FILETIME ft;

  if (rw_flags == UV_FS_O_RDONLY) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_FLAGS);
    return;
  }
  if (fd_info->is_directory) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_FUNCTION);
    return;
  }

  write_size = 0;
  for (index = 0; index < req->fs.info.nbufs; ++index) {
    write_size += req->fs.info.bufs[index].len;
  }

  if (write_size == 0) {
    SET_REQ_RESULT(req, 0);
    return;
  }

  if (force_append) {
    pos = fd_info->size;
  } else if (req->fs.info.offset == -1) {
    pos = fd_info->current_pos;
  } else {
    pos.QuadPart = req->fs.info.offset;
  }

  end_pos.QuadPart = pos.QuadPart + write_size;

  /* Recreate the mapping to enlarge the file if needed */
  if (end_pos.QuadPart > fd_info->size.QuadPart) {
    if (fd_info->mapping != INVALID_HANDLE_VALUE) {
      CloseHandle(fd_info->mapping);
    }

    fd_info->mapping = CreateFileMapping(file,
                                         NULL,
                                         PAGE_READWRITE,
                                         end_pos.HighPart,
                                         end_pos.LowPart,
                                         NULL);
    if (fd_info->mapping == NULL) {
      SET_REQ_WIN32_ERROR(req, GetLastError());
      CloseHandle(file);
      fd_info->mapping = INVALID_HANDLE_VALUE;
      fd_info->size.QuadPart = 0;
      fd_info->current_pos.QuadPart = 0;
      uv__fd_hash_add(fd, fd_info);
      return;
    }

    fd_info->size = end_pos;
    uv__fd_hash_add(fd, fd_info);
  }

  view_offset = pos.QuadPart % uv__allocation_granularity;
  view_base.QuadPart = pos.QuadPart - view_offset;
  view = MapViewOfFile(fd_info->mapping,
                       FILE_MAP_WRITE,
                       view_base.HighPart,
                       view_base.LowPart,
                       view_offset + write_size);
  if (view == NULL) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  done_write = 0;
  for (index = 0; index < req->fs.info.nbufs; ++index) {
#ifdef _MSC_VER
    int err = 0;
    __try {
#endif
      memcpy((char*)view + view_offset + done_write,
             req->fs.info.bufs[index].base,
             req->fs.info.bufs[index].len);
#ifdef _MSC_VER
    }
    __except (fs__filemap_ex_filter(GetExceptionCode(),
                                    GetExceptionInformation(), &err)) {
      SET_REQ_WIN32_ERROR(req, err);
      UnmapViewOfFile(view);
      return;
    }
#endif
    done_write += req->fs.info.bufs[index].len;
  }
  assert(done_write == write_size);

  if (!FlushViewOfFile(view, 0)) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    UnmapViewOfFile(view);
    return;
  }
  if (!UnmapViewOfFile(view)) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  if (req->fs.info.offset == -1) {
    fd_info->current_pos = end_pos;
    uv__fd_hash_add(fd, fd_info);
  }

  GetSystemTimeAsFileTime(&ft);
  SetFileTime(file, NULL, NULL, &ft);

  SET_REQ_RESULT(req, done_write);
}

void fs__write(uv_fs_t* req) {
  int fd = req->file.fd;
  int64_t offset = req->fs.info.offset;
  HANDLE handle;
  OVERLAPPED overlapped, *overlapped_ptr;
  LARGE_INTEGER offset_;
  DWORD bytes;
  DWORD error;
  int result;
  unsigned int index;
  LARGE_INTEGER original_position;
  LARGE_INTEGER zero_offset;
  int restore_position;
  struct uv__fd_info_s fd_info;

  VERIFY_FD(fd, req);

  zero_offset.QuadPart = 0;
  restore_position = 0;
  handle = uv__get_osfhandle(fd);
  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_HANDLE);
    return;
  }

  if (uv__fd_hash_get(fd, &fd_info)) {
    fs__write_filemap(req, handle, &fd_info);
    return;
  }

  if (offset != -1) {
    memset(&overlapped, 0, sizeof overlapped);
    overlapped_ptr = &overlapped;
    if (SetFilePointerEx(handle, zero_offset, &original_position,
                         FILE_CURRENT)) {
      restore_position = 1;
    }
  } else {
    overlapped_ptr = NULL;
  }

  index = 0;
  bytes = 0;
  do {
    DWORD incremental_bytes;

    if (offset != -1) {
      offset_.QuadPart = offset + bytes;
      overlapped.Offset = offset_.LowPart;
      overlapped.OffsetHigh = offset_.HighPart;
    }

    result = WriteFile(handle,
                       req->fs.info.bufs[index].base,
                       req->fs.info.bufs[index].len,
                       &incremental_bytes,
                       overlapped_ptr);
    bytes += incremental_bytes;
    ++index;
  } while (result && index < req->fs.info.nbufs);

  if (restore_position)
    SetFilePointerEx(handle, original_position, NULL, FILE_BEGIN);

  if (result || bytes > 0) {
    SET_REQ_RESULT(req, bytes);
  } else {
    error = GetLastError();

    if (error == ERROR_ACCESS_DENIED) {
      error = ERROR_INVALID_FLAGS;
    }

    SET_REQ_UV_ERROR(req, uv_translate_write_sys_error(error), error);
  }
}


static void fs__unlink_rmdir(uv_fs_t* req, BOOL isrmdir) {
  const WCHAR* pathw = req->file.pathw;
  HANDLE handle;
  BY_HANDLE_FILE_INFORMATION info;
  FILE_DISPOSITION_INFORMATION disposition;
  FILE_DISPOSITION_INFORMATION_EX disposition_ex;
  IO_STATUS_BLOCK iosb;
  NTSTATUS status;
  DWORD error;

  handle = CreateFileW(pathw,
                       FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | DELETE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);

  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  if (!GetFileInformationByHandle(handle, &info)) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    CloseHandle(handle);
    return;
  }

  if (isrmdir && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    /* Error if we're in rmdir mode but it is not a dir.
     * TODO: change it to UV_NOTDIR in v2. */
    SET_REQ_UV_ERROR(req, UV_ENOENT, ERROR_DIRECTORY);
    CloseHandle(handle);
    return;
  }

  if (!isrmdir && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    /* If not explicitly allowed, do not allow deletion of directories, unless
     * it is a symlink. When the path refers to a non-symlink directory, report
     * EPERM as mandated by POSIX.1. */

    /* Check if it is a reparse point. If it's not, it's a normal directory. */
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      SET_REQ_WIN32_ERROR(req, ERROR_ACCESS_DENIED);
      CloseHandle(handle);
      return;
    }

    /* Read the reparse point and check if it is a valid symlink. If not, don't
     * unlink. */
    if (fs__readlink_handle(handle, NULL, NULL) < 0) {
      error = GetLastError();
      if (error == ERROR_SYMLINK_NOT_SUPPORTED)
        error = ERROR_ACCESS_DENIED;
      SET_REQ_WIN32_ERROR(req, error);
      CloseHandle(handle);
      return;
    }
  }

  /* Try posix delete first */
  disposition_ex.Flags = FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS |
                          FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE;

  status = pNtSetInformationFile(handle,
                                 &iosb,
                                 &disposition_ex,
                                 sizeof disposition_ex,
                                 FileDispositionInformationEx);
  if (NT_SUCCESS(status)) {
    SET_REQ_SUCCESS(req);
  } else {
    /* If status == STATUS_CANNOT_DELETE here, given we set
     * FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE, STATUS_CANNOT_DELETE can only mean
     * that there is an existing mapped view to the file, preventing delete.
     * STATUS_CANNOT_DELETE maps to UV_EACCES so it's not specifically worth handling  */
    error = pRtlNtStatusToDosError(status);
    if (error == ERROR_NOT_SUPPORTED /* filesystem does not support posix deletion */ ||
        error == ERROR_INVALID_PARAMETER /* pre Windows 10 error */ ||
        error == ERROR_INVALID_FUNCTION /* pre Windows 10 1607 error */) {
      /* posix delete not supported so try fallback */
      if (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
        /* Remove read-only attribute */
        FILE_BASIC_INFORMATION basic = { 0 };

        basic.FileAttributes = (info.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY) |
                              FILE_ATTRIBUTE_ARCHIVE;

        status = pNtSetInformationFile(handle,
                                      &iosb,
                                      &basic,
                                      sizeof basic,
                                      FileBasicInformation);
        if (!NT_SUCCESS(status)) {
          SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(status));
          CloseHandle(handle);
          return;
        }
      }

      /* Try to set the delete flag. */
      disposition.DeleteFile = TRUE;
      status = pNtSetInformationFile(handle,
                                    &iosb,
                                    &disposition,
                                    sizeof disposition,
                                    FileDispositionInformation);
      if (NT_SUCCESS(status)) {
        SET_REQ_SUCCESS(req);
      } else {
        SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(status));
      }
    } else {
      SET_REQ_WIN32_ERROR(req, error);
    }
  }

  CloseHandle(handle);
}


static void fs__rmdir(uv_fs_t* req) {
  fs__unlink_rmdir(req, /*isrmdir*/1);
}


static void fs__unlink(uv_fs_t* req) {
  fs__unlink_rmdir(req, /*isrmdir*/0);
}


void fs__mkdir(uv_fs_t* req) {
  /* TODO: use req->mode. */
  if (CreateDirectoryW(req->file.pathw, NULL)) {
    SET_REQ_RESULT(req, 0);
  } else {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    if (req->sys_errno_ == ERROR_INVALID_NAME ||
        req->sys_errno_ == ERROR_DIRECTORY)
      req->result = UV_EINVAL;
  }
}

typedef int (*uv__fs_mktemp_func)(uv_fs_t* req);

/* OpenBSD original: lib/libc/stdio/mktemp.c */
void fs__mktemp(uv_fs_t* req, uv__fs_mktemp_func func) {
  static const WCHAR *tempchars =
    L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  static const size_t num_chars = 62;
  static const size_t num_x = 6;
  WCHAR *cp, *ep;
  unsigned int tries, i;
  size_t len;
  uint64_t v;
  char* path;

  path = (char*)req->path;
  len = wcslen(req->file.pathw);
  ep = req->file.pathw + len;
  if (len < num_x || wcsncmp(ep - num_x, L"XXXXXX", num_x)) {
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
    goto clobber;
  }

  tries = TMP_MAX;
  do {
    if (uv__random_rtlgenrandom((void *)&v, sizeof(v)) < 0) {
      SET_REQ_UV_ERROR(req, UV_EIO, ERROR_IO_DEVICE);
      goto clobber;
    }

    cp = ep - num_x;
    for (i = 0; i < num_x; i++) {
      *cp++ = tempchars[v % num_chars];
      v /= num_chars;
    }

    if (func(req)) {
      if (req->result >= 0) {
        len = strlen(path);
        wcstombs(path + len - num_x, ep - num_x, num_x);
      }
      return;
    }
  } while (--tries);

  SET_REQ_WIN32_ERROR(req, GetLastError());

clobber:
  path[0] = '\0';
}


static int fs__mkdtemp_func(uv_fs_t* req) {
  DWORD error;
  if (CreateDirectoryW(req->file.pathw, NULL)) {
    SET_REQ_RESULT(req, 0);
    return 1;
  }
  error = GetLastError();
  if (error != ERROR_ALREADY_EXISTS) {
    SET_REQ_WIN32_ERROR(req, error);
    return 1;
  }

  return 0;
}


void fs__mkdtemp(uv_fs_t* req) {
  fs__mktemp(req, fs__mkdtemp_func);
}


static int fs__mkstemp_func(uv_fs_t* req) {
  HANDLE file;
  int fd;

  file = CreateFileW(req->file.pathw,
                     GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL,
                     CREATE_NEW,
                     FILE_ATTRIBUTE_NORMAL,
                     NULL);

  if (file == INVALID_HANDLE_VALUE) {
    DWORD error;
    error = GetLastError();

    /* If the file exists, the main fs__mktemp() function
       will retry. If it's another error, we want to stop. */
    if (error != ERROR_FILE_EXISTS) {
      SET_REQ_WIN32_ERROR(req, error);
      return 1;
    }

    return 0;
  }

  fd = _open_osfhandle((intptr_t) file, 0);
  if (fd < 0) {
    /* The only known failure mode for _open_osfhandle() is EMFILE, in which
     * case GetLastError() will return zero. However we'll try to handle other
     * errors as well, should they ever occur.
     */
    if (errno == EMFILE)
      SET_REQ_UV_ERROR(req, UV_EMFILE, ERROR_TOO_MANY_OPEN_FILES);
    else if (GetLastError() != ERROR_SUCCESS)
      SET_REQ_WIN32_ERROR(req, GetLastError());
    else
      SET_REQ_WIN32_ERROR(req, UV_UNKNOWN);
    CloseHandle(file);
    return 1;
  }

  SET_REQ_RESULT(req, fd);

  return 1;
}


void fs__mkstemp(uv_fs_t* req) {
  fs__mktemp(req, fs__mkstemp_func);
}


void fs__scandir(uv_fs_t* req) {
  static const size_t dirents_initial_size = 32;

  HANDLE dir_handle = INVALID_HANDLE_VALUE;

  uv__dirent_t** dirents = NULL;
  size_t dirents_size = 0;
  size_t dirents_used = 0;

  IO_STATUS_BLOCK iosb;
  NTSTATUS status;

  /* Buffer to hold directory entries returned by NtQueryDirectoryFile.
   * It's important that this buffer can hold at least one entry, regardless
   * of the length of the file names present in the enumerated directory.
   * A file name is at most 256 WCHARs long.
   * According to MSDN, the buffer must be aligned at an 8-byte boundary.
   */
#if _MSC_VER
  __declspec(align(8)) char buffer[8192];
#else
  __attribute__ ((aligned (8))) char buffer[8192];
#endif

  STATIC_ASSERT(sizeof buffer >=
                sizeof(FILE_DIRECTORY_INFORMATION) + 256 * sizeof(WCHAR));

  /* Open the directory. */
  dir_handle =
      CreateFileW(req->file.pathw,
                  FILE_LIST_DIRECTORY | SYNCHRONIZE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL,
                  OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS,
                  NULL);
  if (dir_handle == INVALID_HANDLE_VALUE)
    goto win32_error;

  /* Read the first chunk. */
  status = pNtQueryDirectoryFile(dir_handle,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &iosb,
                                 &buffer,
                                 sizeof buffer,
                                 FileDirectoryInformation,
                                 FALSE,
                                 NULL,
                                 TRUE);

  /* If the handle is not a directory, we'll get STATUS_INVALID_PARAMETER.
   * This should be reported back as UV_ENOTDIR.
   */
  if (status == (NTSTATUS)STATUS_INVALID_PARAMETER)
    goto not_a_directory_error;

  while (NT_SUCCESS(status)) {
    char* position = buffer;
    size_t next_entry_offset = 0;

    do {
      FILE_DIRECTORY_INFORMATION* info;
      uv__dirent_t* dirent;

      size_t wchar_len;
      size_t wtf8_len;
      char* wtf8;

      /* Obtain a pointer to the current directory entry. */
      position += next_entry_offset;
      info = (FILE_DIRECTORY_INFORMATION*) position;

      /* Fetch the offset to the next directory entry. */
      next_entry_offset = info->NextEntryOffset;

      /* Compute the length of the filename in WCHARs. */
      wchar_len = info->FileNameLength / sizeof info->FileName[0];

      /* Skip over '.' and '..' entries.  It has been reported that
       * the SharePoint driver includes the terminating zero byte in
       * the filename length.  Strip those first.
       */
      while (wchar_len > 0 && info->FileName[wchar_len - 1] == L'\0')
        wchar_len -= 1;

      if (wchar_len == 0)
        continue;
      if (wchar_len == 1 && info->FileName[0] == L'.')
        continue;
      if (wchar_len == 2 && info->FileName[0] == L'.' &&
          info->FileName[1] == L'.')
        continue;

      /* Compute the space required to store the filename as WTF-8. */
      wtf8_len = uv_utf16_length_as_wtf8(&info->FileName[0], wchar_len);

      /* Resize the dirent array if needed. */
      if (dirents_used >= dirents_size) {
        size_t new_dirents_size =
            dirents_size == 0 ? dirents_initial_size : dirents_size << 1;
        uv__dirent_t** new_dirents =
            uv__realloc(dirents, new_dirents_size * sizeof *dirents);

        if (new_dirents == NULL)
          goto out_of_memory_error;

        dirents_size = new_dirents_size;
        dirents = new_dirents;
      }

      /* Allocate space for the uv dirent structure. The dirent structure
       * includes room for the first character of the filename, but `utf8_len`
       * doesn't count the NULL terminator at this point.
       */
      dirent = uv__malloc(sizeof *dirent + wtf8_len);
      if (dirent == NULL)
        goto out_of_memory_error;

      dirents[dirents_used++] = dirent;

      /* Convert file name to UTF-8. */
      wtf8 = &dirent->d_name[0];
      if (uv_utf16_to_wtf8(&info->FileName[0], wchar_len, &wtf8, &wtf8_len) != 0)
        goto out_of_memory_error;

      /* Fill out the type field. */
      if (info->FileAttributes & FILE_ATTRIBUTE_DEVICE)
        dirent->d_type = UV__DT_CHAR;
      else if (info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        dirent->d_type = UV__DT_LINK;
      else if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        dirent->d_type = UV__DT_DIR;
      else
        dirent->d_type = UV__DT_FILE;
    } while (next_entry_offset != 0);

    /* Read the next chunk. */
    status = pNtQueryDirectoryFile(dir_handle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &iosb,
                                   &buffer,
                                   sizeof buffer,
                                   FileDirectoryInformation,
                                   FALSE,
                                   NULL,
                                   FALSE);

    /* After the first pNtQueryDirectoryFile call, the function may return
     * STATUS_SUCCESS even if the buffer was too small to hold at least one
     * directory entry.
     */
    if (status == STATUS_SUCCESS && iosb.Information == 0)
      status = STATUS_BUFFER_OVERFLOW;
  }

  if (status != STATUS_NO_MORE_FILES)
    goto nt_error;

  CloseHandle(dir_handle);

  /* Store the result in the request object. */
  req->ptr = dirents;
  if (dirents != NULL)
    req->flags |= UV_FS_FREE_PTR;

  SET_REQ_RESULT(req, dirents_used);

  /* `nbufs` will be used as index by uv_fs_scandir_next. */
  req->fs.info.nbufs = 0;

  return;

nt_error:
  SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(status));
  goto cleanup;

win32_error:
  SET_REQ_WIN32_ERROR(req, GetLastError());
  goto cleanup;

not_a_directory_error:
  SET_REQ_UV_ERROR(req, UV_ENOTDIR, ERROR_DIRECTORY);
  goto cleanup;

out_of_memory_error:
  SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
  goto cleanup;

cleanup:
  if (dir_handle != INVALID_HANDLE_VALUE)
    CloseHandle(dir_handle);
  while (dirents_used > 0)
    uv__free(dirents[--dirents_used]);
  if (dirents != NULL)
    uv__free(dirents);
}

void fs__opendir(uv_fs_t* req) {
  WCHAR* pathw;
  size_t len;
  const WCHAR* fmt;
  WCHAR* find_path;
  uv_dir_t* dir;

  pathw = req->file.pathw;
  dir = NULL;
  find_path = NULL;

  /* Figure out whether path is a file or a directory. */
  if (!(GetFileAttributesW(pathw) & FILE_ATTRIBUTE_DIRECTORY)) {
    SET_REQ_UV_ERROR(req, UV_ENOTDIR, ERROR_DIRECTORY);
    goto error;
  }

  dir = uv__malloc(sizeof(*dir));
  if (dir == NULL) {
    SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
    goto error;
  }

  len = wcslen(pathw);

  if (len == 0)
    fmt = L"./*";
  else if (IS_SLASH(pathw[len - 1]))
    fmt = L"%s*";
  else
    fmt = L"%s\\*";

  find_path = uv__malloc(sizeof(WCHAR) * (len + 4));
  if (find_path == NULL) {
    SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
    goto error;
  }

  _snwprintf(find_path, len + 3, fmt, pathw);
  dir->dir_handle = FindFirstFileW(find_path, &dir->find_data);
  uv__free(find_path);
  find_path = NULL;
  if (dir->dir_handle == INVALID_HANDLE_VALUE &&
      GetLastError() != ERROR_FILE_NOT_FOUND) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    goto error;
  }

  dir->need_find_call = FALSE;
  req->ptr = dir;
  SET_REQ_RESULT(req, 0);
  return;

error:
  uv__free(dir);
  uv__free(find_path);
  req->ptr = NULL;
}

void fs__readdir(uv_fs_t* req) {
  uv_dir_t* dir;
  uv_dirent_t* dirents;
  uv__dirent_t dent;
  unsigned int dirent_idx;
  PWIN32_FIND_DATAW find_data;
  unsigned int i;
  int r;

  req->flags |= UV_FS_FREE_PTR;
  dir = req->ptr;
  dirents = dir->dirents;
  memset(dirents, 0, dir->nentries * sizeof(*dir->dirents));
  find_data = &dir->find_data;
  dirent_idx = 0;

  while (dirent_idx < dir->nentries) {
    if (dir->need_find_call && FindNextFileW(dir->dir_handle, find_data) == 0) {
      if (GetLastError() == ERROR_NO_MORE_FILES)
        break;
      goto error;
    }

    /* Skip "." and ".." entries. */
    if (find_data->cFileName[0] == L'.' &&
        (find_data->cFileName[1] == L'\0' ||
        (find_data->cFileName[1] == L'.' &&
        find_data->cFileName[2] == L'\0'))) {
      dir->need_find_call = TRUE;
      continue;
    }

    r = uv__convert_utf16_to_utf8((const WCHAR*) &find_data->cFileName,
                                  -1,
                                  (char**) &dirents[dirent_idx].name);
    if (r != 0)
      goto error;

    /* Copy file type. */
    if ((find_data->dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0)
      dent.d_type = UV__DT_CHAR;
    else if ((find_data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
      dent.d_type = UV__DT_LINK;
    else if ((find_data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
      dent.d_type = UV__DT_DIR;
    else
      dent.d_type = UV__DT_FILE;

    dirents[dirent_idx].type = uv__fs_get_dirent_type(&dent);
    dir->need_find_call = TRUE;
    ++dirent_idx;
  }

  SET_REQ_RESULT(req, dirent_idx);
  return;

error:
  SET_REQ_WIN32_ERROR(req, GetLastError());
  for (i = 0; i < dirent_idx; ++i) {
    uv__free((char*) dirents[i].name);
    dirents[i].name = NULL;
  }
}

void fs__closedir(uv_fs_t* req) {
  uv_dir_t* dir;

  dir = req->ptr;
  FindClose(dir->dir_handle);
  uv__free(req->ptr);
  SET_REQ_RESULT(req, 0);
}

INLINE static fs__stat_path_return_t fs__stat_path(WCHAR* path,
    uv_stat_t* statbuf, int do_lstat) {
  FILE_STAT_BASIC_INFORMATION stat_info;

  /* Check if the new fast API is available. */
  if (!pGetFileInformationByName) {
    return FS__STAT_PATH_TRY_SLOW;
  }

  /* Check if the API call fails. */
  if (!pGetFileInformationByName(path, FileStatBasicByNameInfo, &stat_info,
      sizeof(stat_info))) {
    switch(GetLastError()) {
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
      case ERROR_NOT_READY:
      case ERROR_BAD_NET_NAME:
        /* These errors aren't worth retrying with the slow path. */
        return FS__STAT_PATH_ERROR;
    }
    return FS__STAT_PATH_TRY_SLOW;
  }

  /* A file handle is needed to get st_size for links. */
  if ((stat_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return FS__STAT_PATH_TRY_SLOW;
  }

  if (stat_info.DeviceType == FILE_DEVICE_NULL) {
    fs__stat_assign_statbuf_null(statbuf);
    return FS__STAT_PATH_SUCCESS;
  }

  fs__stat_assign_statbuf(statbuf, stat_info, do_lstat);
  return FS__STAT_PATH_SUCCESS;
}

INLINE static int fs__stat_handle(HANDLE handle, uv_stat_t* statbuf,
    int do_lstat) {
  size_t target_length = 0;
  FILE_FS_DEVICE_INFORMATION device_info;
  FILE_ALL_INFORMATION file_info;
  FILE_FS_VOLUME_INFORMATION volume_info;
  NTSTATUS nt_status;
  IO_STATUS_BLOCK io_status;
  FILE_STAT_BASIC_INFORMATION stat_info;

  nt_status = pNtQueryVolumeInformationFile(handle,
                                            &io_status,
                                            &device_info,
                                            sizeof device_info,
                                            FileFsDeviceInformation);

  /* Buffer overflow (a warning status code) is expected here. */
  if (NT_ERROR(nt_status)) {
    SetLastError(pRtlNtStatusToDosError(nt_status));
    return -1;
  }

  /* If it's NUL device set fields as reasonable as possible and return. */
  if (device_info.DeviceType == FILE_DEVICE_NULL) {
    fs__stat_assign_statbuf_null(statbuf);
    return 0;
  }

  nt_status = pNtQueryInformationFile(handle,
                                      &io_status,
                                      &file_info,
                                      sizeof file_info,
                                      FileAllInformation);

  /* Buffer overflow (a warning status code) is expected here. */
  if (NT_ERROR(nt_status)) {
    SetLastError(pRtlNtStatusToDosError(nt_status));
    return -1;
  }

  nt_status = pNtQueryVolumeInformationFile(handle,
                                            &io_status,
                                            &volume_info,
                                            sizeof volume_info,
                                            FileFsVolumeInformation);

  /* Buffer overflow (a warning status code) is expected here. */
  if (io_status.Status == STATUS_NOT_IMPLEMENTED) {
    stat_info.VolumeSerialNumber.QuadPart = 0;
  } else if (NT_ERROR(nt_status)) {
    SetLastError(pRtlNtStatusToDosError(nt_status));
    return -1;
  } else {
    stat_info.VolumeSerialNumber.QuadPart = volume_info.VolumeSerialNumber;
  }

  stat_info.DeviceType = device_info.DeviceType;
  stat_info.FileAttributes = file_info.BasicInformation.FileAttributes;
  stat_info.NumberOfLinks = file_info.StandardInformation.NumberOfLinks;
  stat_info.FileId.QuadPart =
      file_info.InternalInformation.IndexNumber.QuadPart;
  stat_info.ChangeTime.QuadPart =
      file_info.BasicInformation.ChangeTime.QuadPart;
  stat_info.CreationTime.QuadPart =
      file_info.BasicInformation.CreationTime.QuadPart;
  stat_info.LastAccessTime.QuadPart =
      file_info.BasicInformation.LastAccessTime.QuadPart;
  stat_info.LastWriteTime.QuadPart =
      file_info.BasicInformation.LastWriteTime.QuadPart;
  stat_info.AllocationSize.QuadPart =
      file_info.StandardInformation.AllocationSize.QuadPart;

  if (do_lstat &&
      (file_info.BasicInformation.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    /*
     * If reading the link fails, the reparse point is not a symlink and needs
     * to be treated as a regular file. The higher level lstat function will
     * detect this failure and retry without do_lstat if appropriate.
     */
    if (fs__readlink_handle(handle, NULL, &target_length) != 0) {
      return -1;
    }
    stat_info.EndOfFile.QuadPart = target_length;
  } else {
    stat_info.EndOfFile.QuadPart =
      file_info.StandardInformation.EndOfFile.QuadPart;
  }

  fs__stat_assign_statbuf(statbuf, stat_info, do_lstat);
  return 0;
}

INLINE static void fs__stat_assign_statbuf_null(uv_stat_t* statbuf) {
  memset(statbuf, 0, sizeof(uv_stat_t));
  statbuf->st_mode = _S_IFCHR;
  statbuf->st_mode |= (_S_IREAD | _S_IWRITE) | ((_S_IREAD | _S_IWRITE) >> 3) |
                      ((_S_IREAD | _S_IWRITE) >> 6);
  statbuf->st_nlink = 1;
  statbuf->st_blksize = 4096;
  statbuf->st_rdev = FILE_DEVICE_NULL << 16;
}

INLINE static void fs__stat_assign_statbuf(uv_stat_t* statbuf,
    FILE_STAT_BASIC_INFORMATION stat_info, int do_lstat) {
  statbuf->st_dev = stat_info.VolumeSerialNumber.QuadPart;

  /* Todo: st_mode should probably always be 0666 for everyone. We might also
   * want to report 0777 if the file is a .exe or a directory.
   *
   * Currently it's based on whether the 'readonly' attribute is set, which
   * makes little sense because the semantics are so different: the 'read-only'
   * flag is just a way for a user to protect against accidental deletion, and
   * serves no security purpose. Windows uses ACLs for that.
   *
   * Also people now use uv_fs_chmod() to take away the writable bit for good
   * reasons. Windows however just makes the file read-only, which makes it
   * impossible to delete the file afterwards, since read-only files can't be
   * deleted.
   *
   * IOW it's all just a clusterfuck and we should think of something that
   * makes slightly more sense.
   *
   * And uv_fs_chmod should probably just fail on windows or be a total no-op.
   * There's nothing sensible it can do anyway.
   */
  statbuf->st_mode = 0;

  /*
  * On Windows, FILE_ATTRIBUTE_REPARSE_POINT is a general purpose mechanism
  * by which filesystem drivers can intercept and alter file system requests.
  *
  * The only reparse points we care about are symlinks and mount points, both
  * of which are treated as POSIX symlinks. Further, we only care when
  * invoked via lstat, which seeks information about the link instead of its
  * target. Otherwise, reparse points must be treated as regular files.
  */
  if (do_lstat &&
      (stat_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    statbuf->st_mode |= S_IFLNK;
    statbuf->st_size = stat_info.EndOfFile.QuadPart;
  }

  if (statbuf->st_mode == 0) {
    if (stat_info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      statbuf->st_mode |= _S_IFDIR;
      statbuf->st_size = 0;
    } else {
      statbuf->st_mode |= _S_IFREG;
      statbuf->st_size = stat_info.EndOfFile.QuadPart;
    }
  }

  if (stat_info.FileAttributes & FILE_ATTRIBUTE_READONLY)
    statbuf->st_mode |= _S_IREAD | (_S_IREAD >> 3) | (_S_IREAD >> 6);
  else
    statbuf->st_mode |= (_S_IREAD | _S_IWRITE) | ((_S_IREAD | _S_IWRITE) >> 3) |
                        ((_S_IREAD | _S_IWRITE) >> 6);

  uv__filetime_to_timespec(&statbuf->st_atim,
                           stat_info.LastAccessTime.QuadPart);
  uv__filetime_to_timespec(&statbuf->st_ctim,
                           stat_info.ChangeTime.QuadPart);
  uv__filetime_to_timespec(&statbuf->st_mtim,
                           stat_info.LastWriteTime.QuadPart);
  uv__filetime_to_timespec(&statbuf->st_birthtim,
                           stat_info.CreationTime.QuadPart);

  statbuf->st_ino = stat_info.FileId.QuadPart;

  /* st_blocks contains the on-disk allocation size in 512-byte units. */
  statbuf->st_blocks =
      (uint64_t) stat_info.AllocationSize.QuadPart >> 9;

  statbuf->st_nlink = stat_info.NumberOfLinks;

  /* The st_blksize is supposed to be the 'optimal' number of bytes for reading
   * and writing to the disk. That is, for any definition of 'optimal' - it's
   * supposed to at least avoid read-update-write behavior when writing to the
   * disk.
   *
   * However nobody knows this and even fewer people actually use this value,
   * and in order to fill it out we'd have to make another syscall to query the
   * volume for FILE_FS_SECTOR_SIZE_INFORMATION.
   *
   * Therefore we'll just report a sensible value that's quite commonly okay
   * on modern hardware.
   *
   * 4096 is the minimum required to be compatible with newer Advanced Format
   * drives (which have 4096 bytes per physical sector), and to be backwards
   * compatible with older drives (which have 512 bytes per physical sector).
   */
  statbuf->st_blksize = 4096;

  /* Todo: set st_flags to something meaningful. Also provide a wrapper for
   * chattr(2).
   */
  statbuf->st_flags = 0;

  /* Windows has nothing sensible to say about these values, so they'll just
   * remain empty.
   */
  statbuf->st_gid = 0;
  statbuf->st_uid = 0;
  statbuf->st_rdev = 0;
  statbuf->st_gen = 0;
}


INLINE static void fs__stat_prepare_path(WCHAR* pathw) {
  size_t len = wcslen(pathw);

  /* TODO: ignore namespaced paths. */
  if (len > 1 && pathw[len - 2] != L':' &&
      (pathw[len - 1] == L'\\' || pathw[len - 1] == L'/')) {
    pathw[len - 1] = '\0';
  }
}

INLINE static DWORD fs__stat_directory(WCHAR* path, uv_stat_t* statbuf,
    int do_lstat, DWORD ret_error) {
  HANDLE handle = INVALID_HANDLE_VALUE;
  FILE_STAT_BASIC_INFORMATION stat_info;
  FILE_ID_FULL_DIR_INFORMATION dir_info;
  FILE_FS_VOLUME_INFORMATION volume_info;
  FILE_FS_DEVICE_INFORMATION device_info;
  IO_STATUS_BLOCK io_status;
  NTSTATUS nt_status;
  WCHAR* path_dirpath = NULL;
  WCHAR* path_filename = NULL;
  UNICODE_STRING FileMask;
  size_t len;
  size_t split;
  WCHAR splitchar;
  int includes_name;

  /* AKA strtok or wcscspn, in reverse. */
  len = wcslen(path);
  split = len;

  includes_name = 0;
  while (split > 0 && path[split - 1] != L'\\' && path[split - 1] != L'/' &&
                      path[split - 1] != L':') {
    /* check if the path contains a character other than /,\,:,. */
    if (path[split-1] != '.') {
      includes_name = 1;
    }
    split--;
  }
  /* If the path is a relative path with a file name or a folder name */
  if (split == 0 && includes_name) {
    path_dirpath = L".";
  /* If there is a slash or a backslash */
  } else if (path[split - 1] == L'\\' || path[split - 1] == L'/') {
    path_dirpath = path;
    /* If there is no filename, consider it as a relative folder path */
    if (!includes_name) {
      split = len;
    /* Else, split it */
    } else {
      splitchar = path[split - 1];
      path[split - 1] = L'\0';
    }
  /* e.g. "..", "c:" */
  } else {
    path_dirpath = path;
    split = len;
  }
  path_filename = &path[split];

  len = 0;
  while (1) {
    if (path_filename[len] == L'\0')
      break;
    if (path_filename[len] == L'*' || path_filename[len] == L'?' ||
        path_filename[len] == L'>' || path_filename[len] == L'<' ||
        path_filename[len] == L'"') {
      ret_error = ERROR_INVALID_NAME;
      goto cleanup;
    }
    len++;
  }

  /* Get directory handle */
  handle = CreateFileW(path_dirpath,
                       FILE_LIST_DIRECTORY,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);

  if (handle == INVALID_HANDLE_VALUE) {
    ret_error = GetLastError();
    goto cleanup;
  }

  /* Get files in the directory */
  nt_status = uv__RtlUnicodeStringInit(&FileMask, path_filename, len);
  if (!NT_SUCCESS(nt_status)) {
    ret_error = pRtlNtStatusToDosError(nt_status);
    goto cleanup;
  }
  nt_status = pNtQueryDirectoryFile(handle,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &io_status,
                                    &dir_info,
                                    sizeof(dir_info),
                                    FileIdFullDirectoryInformation,
                                    TRUE,
                                    &FileMask,
                                    TRUE);

  /* Buffer overflow (a warning status code) is expected here since there isn't
   * enough space to store the FileName, and actually indicates success. */
  if (!NT_SUCCESS(nt_status) && nt_status != STATUS_BUFFER_OVERFLOW) {
    if (nt_status == STATUS_NO_MORE_FILES)
      ret_error = ERROR_PATH_NOT_FOUND;
    else
      ret_error = pRtlNtStatusToDosError(nt_status);
    goto cleanup;
  }

  /* Assign values to stat_info */
  memset(&stat_info, 0, sizeof(FILE_STAT_BASIC_INFORMATION));
  stat_info.FileAttributes = dir_info.FileAttributes;
  stat_info.CreationTime.QuadPart = dir_info.CreationTime.QuadPart;
  stat_info.LastAccessTime.QuadPart = dir_info.LastAccessTime.QuadPart;
  stat_info.LastWriteTime.QuadPart = dir_info.LastWriteTime.QuadPart;
  if (stat_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    /* A file handle is needed to get st_size for the link (from
     * FSCTL_GET_REPARSE_POINT), which is required by posix, but we are here
     * because getting the file handle failed. We could get just the
     * ReparsePointTag by querying FILE_ID_EXTD_DIR_INFORMATION instead to make
     * sure this really is a link before giving up here on the uv_fs_stat call,
     * but that doesn't seem essential. */
    if (!do_lstat)
      goto cleanup;
    stat_info.EndOfFile.QuadPart = 0;
    stat_info.AllocationSize.QuadPart = 0;
  } else {
    stat_info.EndOfFile.QuadPart = dir_info.EndOfFile.QuadPart;
    stat_info.AllocationSize.QuadPart = dir_info.AllocationSize.QuadPart;
  }
  stat_info.ChangeTime.QuadPart = dir_info.ChangeTime.QuadPart;
  stat_info.FileId.QuadPart = dir_info.FileId.QuadPart;

  /* Finish up by getting device info from the directory handle,
   * since files presumably must live on their device. */
  nt_status = pNtQueryVolumeInformationFile(handle,
                                            &io_status,
                                            &volume_info,
                                            sizeof volume_info,
                                            FileFsVolumeInformation);

  /* Buffer overflow (a warning status code) is expected here. */
  if (io_status.Status == STATUS_NOT_IMPLEMENTED) {
    stat_info.VolumeSerialNumber.QuadPart = 0;
  } else if (NT_ERROR(nt_status)) {
    ret_error = pRtlNtStatusToDosError(nt_status);
    goto cleanup;
  } else {
    stat_info.VolumeSerialNumber.QuadPart = volume_info.VolumeSerialNumber;
  }

  nt_status = pNtQueryVolumeInformationFile(handle,
                                            &io_status,
                                            &device_info,
                                            sizeof device_info,
                                            FileFsDeviceInformation);

  /* Buffer overflow (a warning status code) is expected here. */
  if (NT_ERROR(nt_status)) {
    ret_error = pRtlNtStatusToDosError(nt_status);
    goto cleanup;
  }

  stat_info.DeviceType = device_info.DeviceType;
  stat_info.NumberOfLinks = 1; /* No way to recover this info. */

  fs__stat_assign_statbuf(statbuf, stat_info, do_lstat);
  ret_error = 0;

cleanup:
  if (split != 0)
    path[split - 1] = splitchar;
  if (handle != INVALID_HANDLE_VALUE)
    CloseHandle(handle);
  return ret_error;
}

INLINE static DWORD fs__stat_impl_from_path(WCHAR* path,
                                            int do_lstat,
                                            uv_stat_t* statbuf) {
  HANDLE handle;
  DWORD flags;
  DWORD ret;

  /* If new API exists, try to use it. */
  switch (fs__stat_path(path, statbuf, do_lstat)) {
    case FS__STAT_PATH_SUCCESS:
      return 0;
    case FS__STAT_PATH_ERROR:
      return GetLastError();
    case FS__STAT_PATH_TRY_SLOW:
      break;
  }

  /* If the new API does not exist, use the old API. */
  flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (do_lstat)
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;

  handle = CreateFileW(path,
                       FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       flags,
                       NULL);

  if (handle == INVALID_HANDLE_VALUE) {
    ret = GetLastError();
    if (ret != ERROR_ACCESS_DENIED && ret != ERROR_SHARING_VIOLATION)
      return ret;
    return fs__stat_directory(path, statbuf, do_lstat, ret);
  }

  if (fs__stat_handle(handle, statbuf, do_lstat) != 0)
    ret = GetLastError();
  else
    ret = 0;

  CloseHandle(handle);
  return ret;
}


INLINE static void fs__stat_impl(uv_fs_t* req, int do_lstat) {
  DWORD error;

  error = fs__stat_impl_from_path(req->file.pathw, do_lstat, &req->statbuf);
  if (error != 0) {
    if (do_lstat &&
        (error == ERROR_SYMLINK_NOT_SUPPORTED ||
         error == ERROR_NOT_A_REPARSE_POINT)) {
      /* We opened a reparse point but it was not a symlink. Try again. */
      fs__stat_impl(req, 0);
    } else {
      /* Stat failed. */
      SET_REQ_WIN32_ERROR(req, error);
    }

    return;
  }

  req->ptr = &req->statbuf;
  SET_REQ_RESULT(req, 0);
}


INLINE static int fs__fstat_handle(int fd, HANDLE handle, uv_stat_t* statbuf) {
  DWORD file_type;

  /* Each file type is processed differently. */
  file_type = uv_guess_handle(fd);
  switch (file_type) {
  /* Disk files use the existing logic from fs__stat_handle. */
  case UV_FILE:
    return fs__stat_handle(handle, statbuf, 0);

  /* Devices and pipes are processed identically. There is no more information
   * for them from any API. Fields are set as reasonably as possible and the
   * function returns. */
  case UV_TTY:
  case UV_NAMED_PIPE:
    memset(statbuf, 0, sizeof(uv_stat_t));
    statbuf->st_mode = file_type == UV_TTY ? _S_IFCHR : _S_IFIFO;
    statbuf->st_nlink = 1;
    statbuf->st_rdev = (file_type == UV_TTY ? FILE_DEVICE_CONSOLE : FILE_DEVICE_NAMED_PIPE) << 16;
    statbuf->st_ino = (uintptr_t) handle;
    return 0;

  /* If file type is unknown it is an error. */
  case UV_UNKNOWN_HANDLE:
  default:
    SetLastError(ERROR_INVALID_HANDLE);
    return -1;
  }
}


static void fs__stat(uv_fs_t* req) {
  fs__stat_prepare_path(req->file.pathw);
  fs__stat_impl(req, 0);
}


static void fs__lstat(uv_fs_t* req) {
  fs__stat_prepare_path(req->file.pathw);
  fs__stat_impl(req, 1);
}


static void fs__fstat(uv_fs_t* req) {
  int fd = req->file.fd;
  HANDLE handle;

  VERIFY_FD(fd, req);

  handle = uv__get_osfhandle(fd);

  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_HANDLE);
    return;
  }

  if (fs__fstat_handle(fd, handle, &req->statbuf) != 0) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  req->ptr = &req->statbuf;
  SET_REQ_RESULT(req, 0);
}


static void fs__rename(uv_fs_t* req) {
  if (!MoveFileExW(req->file.pathw, req->fs.info.new_pathw, MOVEFILE_REPLACE_EXISTING)) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  SET_REQ_RESULT(req, 0);
}


INLINE static void fs__sync_impl(uv_fs_t* req) {
  int fd = req->file.fd;
  int result;

  VERIFY_FD(fd, req);

  result = FlushFileBuffers(uv__get_osfhandle(fd)) ? 0 : -1;
  if (result == -1) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
  } else {
    SET_REQ_RESULT(req, result);
  }
}


static void fs__fsync(uv_fs_t* req) {
  fs__sync_impl(req);
}


static void fs__fdatasync(uv_fs_t* req) {
  fs__sync_impl(req);
}


static void fs__ftruncate(uv_fs_t* req) {
  int fd = req->file.fd;
  HANDLE handle;
  struct uv__fd_info_s fd_info = { 0 };
  NTSTATUS status;
  IO_STATUS_BLOCK io_status;
  FILE_END_OF_FILE_INFORMATION eof_info;

  VERIFY_FD(fd, req);

  handle = uv__get_osfhandle(fd);

  if (uv__fd_hash_get(fd, &fd_info)) {
    if (fd_info.is_directory) {
      SET_REQ_WIN32_ERROR(req, ERROR_ACCESS_DENIED);
      return;
    }

    if (fd_info.mapping != INVALID_HANDLE_VALUE) {
      CloseHandle(fd_info.mapping);
    }
  }

  eof_info.EndOfFile.QuadPart = req->fs.info.offset;

  status = pNtSetInformationFile(handle,
                                 &io_status,
                                 &eof_info,
                                 sizeof eof_info,
                                 FileEndOfFileInformation);

  if (NT_SUCCESS(status)) {
    SET_REQ_RESULT(req, 0);
  } else {
    SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(status));

    if (fd_info.flags) {
      CloseHandle(handle);
      fd_info.mapping = INVALID_HANDLE_VALUE;
      fd_info.size.QuadPart = 0;
      fd_info.current_pos.QuadPart = 0;
      uv__fd_hash_add(fd, &fd_info);
      return;
    }
  }

  if (fd_info.flags) {
    fd_info.size = eof_info.EndOfFile;

    if (fd_info.size.QuadPart == 0) {
      fd_info.mapping = INVALID_HANDLE_VALUE;
    } else {
      DWORD flProtect = (fd_info.flags & (UV_FS_O_RDONLY | UV_FS_O_WRONLY |
        UV_FS_O_RDWR)) == UV_FS_O_RDONLY ? PAGE_READONLY : PAGE_READWRITE;
      fd_info.mapping = CreateFileMapping(handle,
                                          NULL,
                                          flProtect,
                                          fd_info.size.HighPart,
                                          fd_info.size.LowPart,
                                          NULL);
      if (fd_info.mapping == NULL) {
        SET_REQ_WIN32_ERROR(req, GetLastError());
        CloseHandle(handle);
        fd_info.mapping = INVALID_HANDLE_VALUE;
        fd_info.size.QuadPart = 0;
        fd_info.current_pos.QuadPart = 0;
        uv__fd_hash_add(fd, &fd_info);
        return;
      }
    }

    uv__fd_hash_add(fd, &fd_info);
  }
}


static void fs__copyfile(uv_fs_t* req) {
  int flags;
  int overwrite;
  uv_stat_t statbuf;
  uv_stat_t new_statbuf;

  flags = req->fs.info.file_flags;

  if (flags & UV_FS_COPYFILE_FICLONE_FORCE) {
    SET_REQ_UV_ERROR(req, UV_ENOSYS, ERROR_NOT_SUPPORTED);
    return;
  }

  overwrite = flags & UV_FS_COPYFILE_EXCL;

  if (CopyFileW(req->file.pathw, req->fs.info.new_pathw, overwrite) != 0) {
    SET_REQ_RESULT(req, 0);
    return;
  }

  SET_REQ_WIN32_ERROR(req, GetLastError());
  if (req->result != UV_EBUSY)
    return;

  /* if error UV_EBUSY check if src and dst file are the same */
  if (fs__stat_impl_from_path(req->file.pathw, 0, &statbuf) != 0 ||
      fs__stat_impl_from_path(req->fs.info.new_pathw, 0, &new_statbuf) != 0) {
    return;
  }

  if (statbuf.st_dev == new_statbuf.st_dev &&
      statbuf.st_ino == new_statbuf.st_ino) {
    SET_REQ_RESULT(req, 0);
  }
}


static void fs__sendfile(uv_fs_t* req) {
  int fd_in = req->file.fd, fd_out = req->fs.info.fd_out;
  size_t length = req->fs.info.bufsml[0].len;
  int64_t offset = req->fs.info.offset;
  const size_t max_buf_size = 65536;
  size_t buf_size = length < max_buf_size ? length : max_buf_size;
  int n, result = 0;
  int64_t result_offset = 0;
  char* buf = (char*) uv__malloc(buf_size);
  if (!buf) {
    uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
  }

  if (offset != -1) {
    result_offset = _lseeki64(fd_in, offset, SEEK_SET);
  }

  if (result_offset == -1) {
    result = -1;
  } else {
    while (length > 0) {
      n = _read(fd_in, buf, length < buf_size ? length : buf_size);
      if (n == 0) {
        break;
      } else if (n == -1) {
        result = -1;
        break;
      }

      length -= n;

      n = _write(fd_out, buf, n);
      if (n == -1) {
        result = -1;
        break;
      }

      result += n;
    }
  }

  uv__free(buf);

  SET_REQ_RESULT(req, result);
}


static void fs__access(uv_fs_t* req) {
  DWORD attr = GetFileAttributesW(req->file.pathw);

  if (attr == INVALID_FILE_ATTRIBUTES) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  /*
   * Access is possible if
   * - write access wasn't requested,
   * - or the file isn't read-only,
   * - or it's a directory.
   * (Directories cannot be read-only on Windows.)
   */
  if (!(req->fs.info.mode & W_OK) ||
      !(attr & FILE_ATTRIBUTE_READONLY) ||
      (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    SET_REQ_RESULT(req, 0);
  } else {
    SET_REQ_WIN32_ERROR(req, UV_EPERM);
  }

}


static void fs__chmod(uv_fs_t* req) {
  int result = _wchmod(req->file.pathw, req->fs.info.mode);
  if (result == -1)
    SET_REQ_WIN32_ERROR(req, _doserrno);
  else
    SET_REQ_RESULT(req, 0);
}


static void fs__fchmod(uv_fs_t* req) {
  int fd = req->file.fd;
  int clear_archive_flag;
  HANDLE handle;
  NTSTATUS nt_status;
  IO_STATUS_BLOCK io_status;
  FILE_BASIC_INFORMATION file_info;

  VERIFY_FD(fd, req);

  handle = ReOpenFile(uv__get_osfhandle(fd), FILE_WRITE_ATTRIBUTES, 0, 0);
  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  nt_status = pNtQueryInformationFile(handle,
                                      &io_status,
                                      &file_info,
                                      sizeof file_info,
                                      FileBasicInformation);

  if (!NT_SUCCESS(nt_status)) {
    SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(nt_status));
    goto fchmod_cleanup;
  }

  /* Test if the Archive attribute is cleared */
  if ((file_info.FileAttributes & FILE_ATTRIBUTE_ARCHIVE) == 0) {
      /* Set Archive flag, otherwise setting or clearing the read-only
         flag will not work */
      file_info.FileAttributes |= FILE_ATTRIBUTE_ARCHIVE;
      nt_status = pNtSetInformationFile(handle,
                                        &io_status,
                                        &file_info,
                                        sizeof file_info,
                                        FileBasicInformation);
      if (!NT_SUCCESS(nt_status)) {
        SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(nt_status));
        goto fchmod_cleanup;
      }
      /* Remember to clear the flag later on */
      clear_archive_flag = 1;
  } else {
      clear_archive_flag = 0;
  }

  if (req->fs.info.mode & _S_IWRITE) {
    file_info.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
  } else {
    file_info.FileAttributes |= FILE_ATTRIBUTE_READONLY;
  }

  nt_status = pNtSetInformationFile(handle,
                                    &io_status,
                                    &file_info,
                                    sizeof file_info,
                                    FileBasicInformation);

  if (!NT_SUCCESS(nt_status)) {
    SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(nt_status));
    goto fchmod_cleanup;
  }

  if (clear_archive_flag) {
      file_info.FileAttributes &= ~FILE_ATTRIBUTE_ARCHIVE;
      if (file_info.FileAttributes == 0) {
          file_info.FileAttributes = FILE_ATTRIBUTE_NORMAL;
      }
      nt_status = pNtSetInformationFile(handle,
                                        &io_status,
                                        &file_info,
                                        sizeof file_info,
                                        FileBasicInformation);
      if (!NT_SUCCESS(nt_status)) {
        SET_REQ_WIN32_ERROR(req, pRtlNtStatusToDosError(nt_status));
        goto fchmod_cleanup;
      }
  }

  SET_REQ_SUCCESS(req);
fchmod_cleanup:
  CloseHandle(handle);
}


INLINE static int fs__utime_handle(HANDLE handle, double atime, double mtime) {
  FILETIME filetime_a, filetime_m;

  TIME_T_TO_FILETIME(atime, &filetime_a);
  TIME_T_TO_FILETIME(mtime, &filetime_m);

  if (!SetFileTime(handle, NULL, &filetime_a, &filetime_m)) {
    return -1;
  }

  return 0;
}

INLINE static DWORD fs__utime_impl_from_path(WCHAR* path,
                                             double atime,
                                             double mtime,
                                             int do_lutime) {
  HANDLE handle;
  DWORD flags;
  DWORD ret;

  flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (do_lutime) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  handle = CreateFileW(path,
                       FILE_WRITE_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       flags,
                       NULL);

  if (handle == INVALID_HANDLE_VALUE)
    return GetLastError();

  if (fs__utime_handle(handle, atime, mtime) != 0)
    ret = GetLastError();
  else
    ret = 0;

  CloseHandle(handle);
  return ret;
}

INLINE static void fs__utime_impl(uv_fs_t* req, int do_lutime) {
  DWORD error;

  error = fs__utime_impl_from_path(req->file.pathw,
                                   req->fs.time.atime,
                                   req->fs.time.mtime,
                                   do_lutime);

  if (error != 0) {
    if (do_lutime &&
        (error == ERROR_SYMLINK_NOT_SUPPORTED ||
         error == ERROR_NOT_A_REPARSE_POINT)) {
      /* Opened file is a reparse point but not a symlink. Try again. */
      fs__utime_impl(req, 0);
    } else {
      /* utime failed. */
      SET_REQ_WIN32_ERROR(req, error);
    }

    return;
  }

  SET_REQ_RESULT(req, 0);
}

static void fs__utime(uv_fs_t* req) {
  fs__utime_impl(req, /* do_lutime */ 0);
}


static void fs__futime(uv_fs_t* req) {
  int fd = req->file.fd;
  HANDLE handle;
  VERIFY_FD(fd, req);

  handle = uv__get_osfhandle(fd);

  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, ERROR_INVALID_HANDLE);
    return;
  }

  if (fs__utime_handle(handle, req->fs.time.atime, req->fs.time.mtime) != 0) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  SET_REQ_RESULT(req, 0);
}

static void fs__lutime(uv_fs_t* req) {
  fs__utime_impl(req, /* do_lutime */ 1);
}


static void fs__link(uv_fs_t* req) {
  DWORD r = CreateHardLinkW(req->fs.info.new_pathw, req->file.pathw, NULL);
  if (r == 0)
    SET_REQ_WIN32_ERROR(req, GetLastError());
  else
    SET_REQ_RESULT(req, 0);
}


static void fs__create_junction(uv_fs_t* req, const WCHAR* path,
    const WCHAR* new_path) {
  HANDLE handle = INVALID_HANDLE_VALUE;
  REPARSE_DATA_BUFFER *buffer = NULL;
  int created = 0;
  int target_len;
  int is_absolute, is_long_path;
  int needed_buf_size, used_buf_size, used_data_size, path_buf_len;
  int start, len, i;
  int add_slash;
  DWORD bytes;
  WCHAR* path_buf;

  target_len = wcslen(path);
  is_long_path = wcsncmp(path, LONG_PATH_PREFIX, LONG_PATH_PREFIX_LEN) == 0;

  if (is_long_path) {
    is_absolute = 1;
  } else {
    is_absolute = target_len >= 3 && IS_LETTER(path[0]) &&
      path[1] == L':' && IS_SLASH(path[2]);
  }

  if (!is_absolute) {
    /* Not supporting relative paths */
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_NOT_SUPPORTED);
    return;
  }

  /* Do a pessimistic calculation of the required buffer size */
  needed_buf_size =
      FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) +
      JUNCTION_PREFIX_LEN * sizeof(WCHAR) +
      2 * (target_len + 2) * sizeof(WCHAR);

  /* Allocate the buffer */
  buffer = (REPARSE_DATA_BUFFER*)uv__malloc(needed_buf_size);
  if (!buffer) {
    uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
  }

  /* Grab a pointer to the part of the buffer where filenames go */
  path_buf = (WCHAR*)&(buffer->MountPointReparseBuffer.PathBuffer);
  path_buf_len = 0;

  /* Copy the substitute (internal) target path */
  start = path_buf_len;

  wcsncpy((WCHAR*)&path_buf[path_buf_len], JUNCTION_PREFIX,
    JUNCTION_PREFIX_LEN);
  path_buf_len += JUNCTION_PREFIX_LEN;

  add_slash = 0;
  for (i = is_long_path ? LONG_PATH_PREFIX_LEN : 0; path[i] != L'\0'; i++) {
    if (IS_SLASH(path[i])) {
      add_slash = 1;
      continue;
    }

    if (add_slash) {
      path_buf[path_buf_len++] = L'\\';
      add_slash = 0;
    }

    path_buf[path_buf_len++] = path[i];
  }
  if (add_slash)
    path_buf[path_buf_len++] = L'\\';
  len = path_buf_len - start;

  /* Insert null terminator */
  path_buf[path_buf_len++] = L'\0';

  /* Set the info about the substitute name */
  buffer->MountPointReparseBuffer.SubstituteNameOffset = start * sizeof(WCHAR);
  buffer->MountPointReparseBuffer.SubstituteNameLength = len * sizeof(WCHAR);

  /* Copy the print name of the target path */
  start = path_buf_len;
  add_slash = 0;
  for (i = is_long_path ? LONG_PATH_PREFIX_LEN : 0; path[i] != L'\0'; i++) {
    if (IS_SLASH(path[i])) {
      add_slash = 1;
      continue;
    }

    if (add_slash) {
      path_buf[path_buf_len++] = L'\\';
      add_slash = 0;
    }

    path_buf[path_buf_len++] = path[i];
  }
  len = path_buf_len - start;
  if (len == 2 || add_slash) {
    path_buf[path_buf_len++] = L'\\';
    len++;
  }

  /* Insert another null terminator */
  path_buf[path_buf_len++] = L'\0';

  /* Set the info about the print name */
  buffer->MountPointReparseBuffer.PrintNameOffset = start * sizeof(WCHAR);
  buffer->MountPointReparseBuffer.PrintNameLength = len * sizeof(WCHAR);

  /* Calculate how much buffer space was actually used */
  used_buf_size = FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) +
    path_buf_len * sizeof(WCHAR);
  used_data_size = used_buf_size -
    FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer);

  /* Put general info in the data buffer */
  buffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  buffer->ReparseDataLength = used_data_size;
  buffer->Reserved = 0;

  /* Create a new directory */
  if (!CreateDirectoryW(new_path, NULL)) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    goto error;
  }
  created = 1;

  /* Open the directory */
  handle = CreateFileW(new_path,
                       GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS |
                         FILE_FLAG_OPEN_REPARSE_POINT,
                       NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    goto error;
  }

  /* Create the actual reparse point */
  if (!DeviceIoControl(handle,
                       FSCTL_SET_REPARSE_POINT,
                       buffer,
                       used_buf_size,
                       NULL,
                       0,
                       &bytes,
                       NULL)) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    goto error;
  }

  /* Clean up */
  CloseHandle(handle);
  uv__free(buffer);

  SET_REQ_RESULT(req, 0);
  return;

error:
  uv__free(buffer);

  if (handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
  }

  if (created) {
    RemoveDirectoryW(new_path);
  }
}


static void fs__symlink(uv_fs_t* req) {
  WCHAR* pathw;
  WCHAR* new_pathw;
  int flags;
  int err;

  pathw = req->file.pathw;
  new_pathw = req->fs.info.new_pathw;

  if (req->fs.info.file_flags & UV_FS_SYMLINK_JUNCTION) {
    fs__create_junction(req, pathw, new_pathw);
    return;
  }

  if (req->fs.info.file_flags & UV_FS_SYMLINK_DIR)
    flags = SYMBOLIC_LINK_FLAG_DIRECTORY | uv__file_symlink_usermode_flag;
  else
    flags = uv__file_symlink_usermode_flag;

  if (CreateSymbolicLinkW(new_pathw, pathw, flags)) {
    SET_REQ_RESULT(req, 0);
    return;
  }

  /* Something went wrong. We will test if it is because of user-mode
   * symlinks.
   */
  err = GetLastError();
  if (err == ERROR_INVALID_PARAMETER &&
      flags & SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) {
    /* This system does not support user-mode symlinks. We will clear the
     * unsupported flag and retry.
     */
    uv__file_symlink_usermode_flag = 0;
    fs__symlink(req);
  } else {
    SET_REQ_WIN32_ERROR(req, err);
  }
}


static void fs__readlink(uv_fs_t* req) {
  HANDLE handle;

  handle = CreateFileW(req->file.pathw,
                       0,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);

  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  assert(req->ptr == NULL);
  if (fs__readlink_handle(handle, (char**) &req->ptr, NULL) != 0) {
    DWORD error = GetLastError();
    SET_REQ_WIN32_ERROR(req, error);
    if (error == ERROR_NOT_A_REPARSE_POINT)
      req->result = UV_EINVAL;
    CloseHandle(handle);
    return;
  }

  req->flags |= UV_FS_FREE_PTR;
  SET_REQ_RESULT(req, 0);

  CloseHandle(handle);
}


static ssize_t fs__realpath_handle(HANDLE handle, char** realpath_ptr) {
  int r;
  DWORD w_realpath_len;
  WCHAR* w_realpath_ptr = NULL;
  WCHAR* w_realpath_buf;

  w_realpath_len = GetFinalPathNameByHandleW(handle, NULL, 0, VOLUME_NAME_DOS);
  if (w_realpath_len == 0) {
    return -1;
  }

  w_realpath_buf = uv__malloc((w_realpath_len + 1) * sizeof(WCHAR));
  if (w_realpath_buf == NULL) {
    SetLastError(ERROR_OUTOFMEMORY);
    return -1;
  }
  w_realpath_ptr = w_realpath_buf;

  if (GetFinalPathNameByHandleW(
          handle, w_realpath_ptr, w_realpath_len, VOLUME_NAME_DOS) == 0) {
    uv__free(w_realpath_buf);
    SetLastError(ERROR_INVALID_HANDLE);
    return -1;
  }

  /* convert UNC path to long path */
  if (wcsncmp(w_realpath_ptr,
              UNC_PATH_PREFIX,
              UNC_PATH_PREFIX_LEN) == 0) {
    w_realpath_ptr += 6;
    *w_realpath_ptr = L'\\';
    w_realpath_len -= 6;
  } else if (wcsncmp(w_realpath_ptr,
                      LONG_PATH_PREFIX,
                      LONG_PATH_PREFIX_LEN) == 0) {
    w_realpath_ptr += 4;
    w_realpath_len -= 4;
  } else {
    uv__free(w_realpath_buf);
    SetLastError(ERROR_INVALID_HANDLE);
    return -1;
  }

  assert(*realpath_ptr == NULL);
  r = uv_utf16_to_wtf8(w_realpath_ptr, w_realpath_len, realpath_ptr, NULL);
  uv__free(w_realpath_buf);
  return r;
}

static void fs__realpath(uv_fs_t* req) {
  HANDLE handle;

  handle = CreateFileW(req->file.pathw,
                       0,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  assert(req->ptr == NULL);
  if (fs__realpath_handle(handle, (char**) &req->ptr) == -1) {
    CloseHandle(handle);
    SET_REQ_WIN32_ERROR(req, GetLastError());
    return;
  }

  CloseHandle(handle);
  req->flags |= UV_FS_FREE_PTR;
  SET_REQ_RESULT(req, 0);
}


static void fs__chown(uv_fs_t* req) {
  SET_REQ_RESULT(req, 0);
}


static void fs__fchown(uv_fs_t* req) {
  SET_REQ_RESULT(req, 0);
}


static void fs__lchown(uv_fs_t* req) {
  SET_REQ_RESULT(req, 0);
}


static void fs__statfs(uv_fs_t* req) {
  uv_statfs_t* stat_fs;
  DWORD sectors_per_cluster;
  DWORD bytes_per_sector;
  DWORD free_clusters;
  DWORD total_clusters;
  WCHAR* pathw;

  pathw = req->file.pathw;
retry_get_disk_free_space:
  if (0 == GetDiskFreeSpaceW(pathw,
                             &sectors_per_cluster,
                             &bytes_per_sector,
                             &free_clusters,
                             &total_clusters)) {
    DWORD err;
    WCHAR* fpart;
    size_t len;
    DWORD ret;
    BOOL is_second;

    err = GetLastError();
    is_second = pathw != req->file.pathw;
    if (err != ERROR_DIRECTORY || is_second) {
      if (is_second)
        uv__free(pathw);

      SET_REQ_WIN32_ERROR(req, err);
      return;
    }

    len = MAX_PATH + 1;
    pathw = uv__malloc(len * sizeof(*pathw));
    if (pathw == NULL) {
      SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
      return;
    }
retry_get_full_path_name:
    ret = GetFullPathNameW(req->file.pathw,
                           len,
                           pathw,
                           &fpart);
    if (ret == 0) {
      uv__free(pathw);
      SET_REQ_WIN32_ERROR(req, err);
      return;
    } else if (ret > len) {
      len = ret;
      pathw = uv__reallocf(pathw, len * sizeof(*pathw));
      if (pathw == NULL) {
        SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
        return;
      }
      goto retry_get_full_path_name;
    }
    if (fpart != 0)
      *fpart = L'\0';

    goto retry_get_disk_free_space;
  }
  if (pathw != req->file.pathw) {
    uv__free(pathw);
  }

  stat_fs = uv__malloc(sizeof(*stat_fs));
  if (stat_fs == NULL) {
    SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
    return;
  }

  stat_fs->f_type = 0;
  stat_fs->f_bsize = bytes_per_sector * sectors_per_cluster;
  stat_fs->f_blocks = total_clusters;
  stat_fs->f_bfree = free_clusters;
  stat_fs->f_bavail = free_clusters;
  stat_fs->f_files = 0;
  stat_fs->f_ffree = 0;
  req->ptr = stat_fs;
  req->flags |= UV_FS_FREE_PTR;
  SET_REQ_RESULT(req, 0);
}


static void uv__fs_work(struct uv__work* w) {
  uv_fs_t* req;

  req = container_of(w, uv_fs_t, work_req);
  assert(req->type == UV_FS);

#define XX(uc, lc)  case UV_FS_##uc: fs__##lc(req); break;
  switch (req->fs_type) {
    XX(OPEN, open)
    XX(CLOSE, close)
    XX(READ, read)
    XX(WRITE, write)
    XX(COPYFILE, copyfile)
    XX(SENDFILE, sendfile)
    XX(STAT, stat)
    XX(LSTAT, lstat)
    XX(FSTAT, fstat)
    XX(FTRUNCATE, ftruncate)
    XX(UTIME, utime)
    XX(FUTIME, futime)
    XX(LUTIME, lutime)
    XX(ACCESS, access)
    XX(CHMOD, chmod)
    XX(FCHMOD, fchmod)
    XX(FSYNC, fsync)
    XX(FDATASYNC, fdatasync)
    XX(UNLINK, unlink)
    XX(RMDIR, rmdir)
    XX(MKDIR, mkdir)
    XX(MKDTEMP, mkdtemp)
    XX(MKSTEMP, mkstemp)
    XX(RENAME, rename)
    XX(SCANDIR, scandir)
    XX(READDIR, readdir)
    XX(OPENDIR, opendir)
    XX(CLOSEDIR, closedir)
    XX(LINK, link)
    XX(SYMLINK, symlink)
    XX(READLINK, readlink)
    XX(REALPATH, realpath)
    XX(CHOWN, chown)
    XX(FCHOWN, fchown)
    XX(LCHOWN, lchown)
    XX(STATFS, statfs)
    default:
      assert(!"bad uv_fs_type");
  }
}


static void uv__fs_done(struct uv__work* w, int status) {
  uv_fs_t* req;

  req = container_of(w, uv_fs_t, work_req);
  uv__req_unregister(req->loop);

  if (status == UV_ECANCELED) {
    assert(req->result == 0);
    SET_REQ_UV_ERROR(req, UV_ECANCELED, 0);
  }

  req->cb(req);
}


void uv_fs_req_cleanup(uv_fs_t* req) {
  if (req == NULL)
    return;

  if (req->flags & UV_FS_CLEANEDUP)
    return;

  if (req->flags & UV_FS_FREE_PATHS)
    uv__free(req->file.pathw);

  if (req->flags & UV_FS_FREE_PTR) {
    if (req->fs_type == UV_FS_SCANDIR && req->ptr != NULL)
      uv__fs_scandir_cleanup(req);
    else if (req->fs_type == UV_FS_READDIR)
      uv__fs_readdir_cleanup(req);
    else
      uv__free(req->ptr);
  }

  if (req->fs.info.bufs != req->fs.info.bufsml)
    uv__free(req->fs.info.bufs);

  req->path = NULL;
  req->file.pathw = NULL;
  req->fs.info.new_pathw = NULL;
  req->fs.info.bufs = NULL;
  req->ptr = NULL;

  req->flags |= UV_FS_CLEANEDUP;
}


int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags,
    int mode, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_OPEN);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.info.file_flags = flags;
  req->fs.info.mode = mode;
  POST;
}


int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file fd, uv_fs_cb cb) {
  INIT(UV_FS_CLOSE);
  req->file.fd = fd;
  POST;
}


int uv_fs_read(uv_loop_t* loop,
               uv_fs_t* req,
               uv_file fd,
               const uv_buf_t bufs[],
               unsigned int nbufs,
               int64_t offset,
               uv_fs_cb cb) {
  INIT(UV_FS_READ);

  if (bufs == NULL || nbufs == 0) {
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
    return UV_EINVAL;
  }

  req->file.fd = fd;

  req->fs.info.nbufs = nbufs;
  req->fs.info.bufs = req->fs.info.bufsml;
  if (nbufs > ARRAY_SIZE(req->fs.info.bufsml))
    req->fs.info.bufs = uv__malloc(nbufs * sizeof(*bufs));

  if (req->fs.info.bufs == NULL) {
    SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
    return UV_ENOMEM;
  }

  memcpy(req->fs.info.bufs, bufs, nbufs * sizeof(*bufs));

  req->fs.info.offset = offset;
  POST;
}


int uv_fs_write(uv_loop_t* loop,
                uv_fs_t* req,
                uv_file fd,
                const uv_buf_t bufs[],
                unsigned int nbufs,
                int64_t offset,
                uv_fs_cb cb) {
  INIT(UV_FS_WRITE);

  if (bufs == NULL || nbufs == 0) {
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
    return UV_EINVAL;
  }

  req->file.fd = fd;

  req->fs.info.nbufs = nbufs;
  req->fs.info.bufs = req->fs.info.bufsml;
  if (nbufs > ARRAY_SIZE(req->fs.info.bufsml))
    req->fs.info.bufs = uv__malloc(nbufs * sizeof(*bufs));

  if (req->fs.info.bufs == NULL) {
    SET_REQ_UV_ERROR(req, UV_ENOMEM, ERROR_OUTOFMEMORY);
    return UV_ENOMEM;
  }

  memcpy(req->fs.info.bufs, bufs, nbufs * sizeof(*bufs));

  req->fs.info.offset = offset;
  POST;
}


int uv_fs_unlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
    uv_fs_cb cb) {
  int err;

  INIT(UV_FS_UNLINK);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_mkdir(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode,
    uv_fs_cb cb) {
  int err;

  INIT(UV_FS_MKDIR);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.info.mode = mode;
  POST;
}


int uv_fs_mkdtemp(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* tpl,
                  uv_fs_cb cb) {
  int err;

  INIT(UV_FS_MKDTEMP);
  err = fs__capture_path(req, tpl, NULL, TRUE);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_mkstemp(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* tpl,
                  uv_fs_cb cb) {
  int err;

  INIT(UV_FS_MKSTEMP);
  err = fs__capture_path(req, tpl, NULL, TRUE);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_rmdir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_RMDIR);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_scandir(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags,
    uv_fs_cb cb) {
  int err;

  INIT(UV_FS_SCANDIR);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.info.file_flags = flags;
  POST;
}

int uv_fs_opendir(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* path,
                  uv_fs_cb cb) {
  int err;

  INIT(UV_FS_OPENDIR);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }
  POST;
}

int uv_fs_readdir(uv_loop_t* loop,
                  uv_fs_t* req,
                  uv_dir_t* dir,
                  uv_fs_cb cb) {
  INIT(UV_FS_READDIR);

  if (dir == NULL ||
      dir->dirents == NULL ||
      dir->dir_handle == INVALID_HANDLE_VALUE) {
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
    return UV_EINVAL;
  }

  req->ptr = dir;
  POST;
}

int uv_fs_closedir(uv_loop_t* loop,
                   uv_fs_t* req,
                   uv_dir_t* dir,
                   uv_fs_cb cb) {
  INIT(UV_FS_CLOSEDIR);
  if (dir == NULL) {
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
    return UV_EINVAL;
  }
  req->ptr = dir;
  POST;
}

int uv_fs_link(uv_loop_t* loop, uv_fs_t* req, const char* path,
    const char* new_path, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_LINK);
  err = fs__capture_path(req, path, new_path, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_symlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
    const char* new_path, int flags, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_SYMLINK);
  err = fs__capture_path(req, path, new_path, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.info.file_flags = flags;
  POST;
}


int uv_fs_readlink(uv_loop_t* loop, uv_fs_t* req, const char* path,
    uv_fs_cb cb) {
  int err;

  INIT(UV_FS_READLINK);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_realpath(uv_loop_t* loop, uv_fs_t* req, const char* path,
    uv_fs_cb cb) {
  int err;

  INIT(UV_FS_REALPATH);

  if (!path) {
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
    return UV_EINVAL;
  }

  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_chown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid,
    uv_gid_t gid, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_CHOWN);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_fchown(uv_loop_t* loop, uv_fs_t* req, uv_file fd, uv_uid_t uid,
    uv_gid_t gid, uv_fs_cb cb) {
  INIT(UV_FS_FCHOWN);
  POST;
}


int uv_fs_lchown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid,
    uv_gid_t gid, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_LCHOWN);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_STAT);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_lstat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_LSTAT);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_fstat(uv_loop_t* loop, uv_fs_t* req, uv_file fd, uv_fs_cb cb) {
  INIT(UV_FS_FSTAT);
  req->file.fd = fd;
  POST;
}


int uv_fs_rename(uv_loop_t* loop, uv_fs_t* req, const char* path,
    const char* new_path, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_RENAME);
  err = fs__capture_path(req, path, new_path, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}


int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_file fd, uv_fs_cb cb) {
  INIT(UV_FS_FSYNC);
  req->file.fd = fd;
  POST;
}


int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_file fd, uv_fs_cb cb) {
  INIT(UV_FS_FDATASYNC);
  req->file.fd = fd;
  POST;
}


int uv_fs_ftruncate(uv_loop_t* loop, uv_fs_t* req, uv_file fd,
    int64_t offset, uv_fs_cb cb) {
  INIT(UV_FS_FTRUNCATE);
  req->file.fd = fd;
  req->fs.info.offset = offset;
  POST;
}


int uv_fs_copyfile(uv_loop_t* loop,
                   uv_fs_t* req,
                   const char* path,
                   const char* new_path,
                   int flags,
                   uv_fs_cb cb) {
  int err;

  INIT(UV_FS_COPYFILE);

  if (flags & ~(UV_FS_COPYFILE_EXCL |
                UV_FS_COPYFILE_FICLONE |
                UV_FS_COPYFILE_FICLONE_FORCE)) {
    SET_REQ_UV_ERROR(req, UV_EINVAL, ERROR_INVALID_PARAMETER);
    return UV_EINVAL;
  }

  err = fs__capture_path(req, path, new_path, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.info.file_flags = flags;
  POST;
}


int uv_fs_sendfile(uv_loop_t* loop, uv_fs_t* req, uv_file fd_out,
    uv_file fd_in, int64_t in_offset, size_t length, uv_fs_cb cb) {
  INIT(UV_FS_SENDFILE);
  req->file.fd = fd_in;
  req->fs.info.fd_out = fd_out;
  req->fs.info.offset = in_offset;
  req->fs.info.bufsml[0].len = length;
  POST;
}


int uv_fs_access(uv_loop_t* loop,
                 uv_fs_t* req,
                 const char* path,
                 int flags,
                 uv_fs_cb cb) {
  int err;

  INIT(UV_FS_ACCESS);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.info.mode = flags;
  POST;
}


int uv_fs_chmod(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode,
    uv_fs_cb cb) {
  int err;

  INIT(UV_FS_CHMOD);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.info.mode = mode;
  POST;
}


int uv_fs_fchmod(uv_loop_t* loop, uv_fs_t* req, uv_file fd, int mode,
    uv_fs_cb cb) {
  INIT(UV_FS_FCHMOD);
  req->file.fd = fd;
  req->fs.info.mode = mode;
  POST;
}


int uv_fs_utime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime,
    double mtime, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_UTIME);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.time.atime = atime;
  req->fs.time.mtime = mtime;
  POST;
}


int uv_fs_futime(uv_loop_t* loop, uv_fs_t* req, uv_file fd, double atime,
    double mtime, uv_fs_cb cb) {
  INIT(UV_FS_FUTIME);
  req->file.fd = fd;
  req->fs.time.atime = atime;
  req->fs.time.mtime = mtime;
  POST;
}

int uv_fs_lutime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime,
    double mtime, uv_fs_cb cb) {
  int err;

  INIT(UV_FS_LUTIME);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  req->fs.time.atime = atime;
  req->fs.time.mtime = mtime;
  POST;
}


int uv_fs_statfs(uv_loop_t* loop,
                 uv_fs_t* req,
                 const char* path,
                 uv_fs_cb cb) {
  int err;

  INIT(UV_FS_STATFS);
  err = fs__capture_path(req, path, NULL, cb != NULL);
  if (err) {
    SET_REQ_WIN32_ERROR(req, err);
    return req->result;
  }

  POST;
}

int uv_fs_get_system_error(const uv_fs_t* req) {
  return req->sys_errno_;
}
