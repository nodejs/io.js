
.. _fs:

File system operations
======================

libuv provides a wide variety of cross-platform sync and async file system
operations. All functions defined in this document take a callback, which is
allowed to be NULL. If the callback is NULL the request is completed synchronously,
otherwise it will be performed asynchronously.

All file operations are run on the threadpool. See :ref:`threadpool` for information
on the threadpool size.

Starting with libuv v1.45.0, some file operations on Linux are handed off to
`io_uring <https://en.wikipedia.org/wiki/Io_uring>` when possible. Apart from
a (sometimes significant) increase in throughput there should be no change in
observable behavior. Libuv reverts to using its threadpool when the necessary
kernel features are unavailable or unsuitable. Starting with libuv v1.49.0 this
behavior was reverted and Libuv on Linux by default will be using the threadpool
again. In order to enable io_uring the :c:type:`uv_loop_t` instance must be
configured with the :c:type:`UV_LOOP_ENABLE_IO_URING_SQPOLL` option.

.. note::
     On Windows `uv_fs_*` functions use utf-8 encoding.

Data types
----------

.. c:type:: uv_fs_t

    File system request type.

.. c:type:: uv_timespec_t

    Y2K38-unsafe data type for storing times with nanosecond resolution.
    Will be replaced with :c:type:`uv_timespec64_t` in libuv v2.0.

    ::

        typedef struct {
            long tv_sec;
            long tv_nsec;
        } uv_timespec_t;

.. c:type:: uv_stat_t

    Portable equivalent of ``struct stat``.

    ::

        typedef struct {
            uint64_t st_dev;
            uint64_t st_mode;
            uint64_t st_nlink;
            uint64_t st_uid;
            uint64_t st_gid;
            uint64_t st_rdev;
            uint64_t st_ino;
            uint64_t st_size;
            uint64_t st_blksize;
            uint64_t st_blocks;
            uint64_t st_flags;
            uint64_t st_gen;
            uv_timespec_t st_atim;
            uv_timespec_t st_mtim;
            uv_timespec_t st_ctim;
            uv_timespec_t st_birthtim;
        } uv_stat_t;

.. c:enum:: uv_fs_type

    File system request type.

    ::

        typedef enum {
            UV_FS_UNKNOWN = -1,
            UV_FS_CUSTOM,
            UV_FS_OPEN,
            UV_FS_CLOSE,
            UV_FS_READ,
            UV_FS_WRITE,
            UV_FS_SENDFILE,
            UV_FS_STAT,
            UV_FS_LSTAT,
            UV_FS_FSTAT,
            UV_FS_FTRUNCATE,
            UV_FS_UTIME,
            UV_FS_FUTIME,
            UV_FS_ACCESS,
            UV_FS_CHMOD,
            UV_FS_FCHMOD,
            UV_FS_FSYNC,
            UV_FS_FDATASYNC,
            UV_FS_UNLINK,
            UV_FS_RMDIR,
            UV_FS_MKDIR,
            UV_FS_MKDTEMP,
            UV_FS_RENAME,
            UV_FS_SCANDIR,
            UV_FS_LINK,
            UV_FS_SYMLINK,
            UV_FS_READLINK,
            UV_FS_CHOWN,
            UV_FS_FCHOWN,
            UV_FS_REALPATH,
            UV_FS_COPYFILE,
            UV_FS_LCHOWN,
            UV_FS_OPENDIR,
            UV_FS_READDIR,
            UV_FS_CLOSEDIR,
            UV_FS_MKSTEMP,
            UV_FS_LUTIME
        } uv_fs_type;

.. c:type:: uv_statfs_t

    Reduced cross platform equivalent of ``struct statfs``.
    Used in :c:func:`uv_fs_statfs`.

    ::

        typedef struct uv_statfs_s {
            uint64_t f_type;
            uint64_t f_bsize;
            uint64_t f_blocks;
            uint64_t f_bfree;
            uint64_t f_bavail;
            uint64_t f_files;
            uint64_t f_ffree;
            uint64_t f_spare[4];
        } uv_statfs_t;

.. c:enum:: uv_dirent_type_t

    Type of dirent.

    ::

        typedef enum {
            UV_DIRENT_UNKNOWN,
            UV_DIRENT_FILE,
            UV_DIRENT_DIR,
            UV_DIRENT_LINK,
            UV_DIRENT_FIFO,
            UV_DIRENT_SOCKET,
            UV_DIRENT_CHAR,
            UV_DIRENT_BLOCK
        } uv_dirent_type_t;


.. c:type:: uv_dirent_t

    Cross platform (reduced) equivalent of ``struct dirent``.
    Used in :c:func:`uv_fs_scandir_next`.

    ::

        typedef struct uv_dirent_s {
            const char* name;
            uv_dirent_type_t type;
        } uv_dirent_t;

.. c:type:: uv_dir_t

    Data type used for streaming directory iteration.
    Used by :c:func:`uv_fs_opendir()`, :c:func:`uv_fs_readdir()`, and
    :c:func:`uv_fs_closedir()`. `dirents` represents a user provided array of
    `uv_dirent_t`s used to hold results. `nentries` is the user provided maximum
    array size of `dirents`.

    ::

        typedef struct uv_dir_s {
            uv_dirent_t* dirents;
            size_t nentries;
        } uv_dir_t;

.. c:type:: void (*uv_fs_cb)(uv_fs_t* req)

    Callback called when a request is completed asynchronously.


Public members
^^^^^^^^^^^^^^

.. c:member:: uv_loop_t* uv_fs_t.loop

    Loop that started this request and where completion will be reported.
    Readonly.

.. c:member:: uv_fs_type uv_fs_t.fs_type

    FS request type.

.. c:member:: const char* uv_fs_t.path

    Path affecting the request.

.. c:member:: ssize_t uv_fs_t.result

    Result of the request. < 0 means error, success otherwise. On requests such
    as :c:func:`uv_fs_read` or :c:func:`uv_fs_write` it indicates the amount of
    data that was read or written, respectively.

.. c:member:: uv_stat_t uv_fs_t.statbuf

    Stores the result of :c:func:`uv_fs_stat` and other stat requests.

.. c:member:: void* uv_fs_t.ptr

    Stores the result of :c:func:`uv_fs_readlink` and
    :c:func:`uv_fs_realpath` and serves as an alias to `statbuf`.

.. seealso:: The :c:type:`uv_req_t` members also apply.


API
---

.. c:function:: void uv_fs_req_cleanup(uv_fs_t* req)

    Cleanup request. Must be called after a request is finished to deallocate
    any memory libuv might have allocated.

.. c:function:: int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb)

    Equivalent to :man:`close(2)`.

.. c:function:: int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags, int mode, uv_fs_cb cb)

    Equivalent to :man:`open(2)`.

    .. note::
        On Windows libuv uses `CreateFileW` and thus the file is always opened
        in binary mode. Because of this the O_BINARY and O_TEXT flags are not
        supported.

.. c:function:: int uv_fs_read(uv_loop_t* loop, uv_fs_t* req, uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset, uv_fs_cb cb)

    Equivalent to :man:`preadv(2)`. If the `offset` argument is `-1`, then
    the current file offset is used and updated.

    .. warning::
        On Windows, under non-MSVC environments (e.g. when GCC or Clang is used
        to build libuv), files opened using ``UV_FS_O_FILEMAP`` may cause a fatal
        crash if the memory mapped read operation fails.

.. c:function:: int uv_fs_unlink(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)

    Equivalent to :man:`unlink(2)`.

.. c:function:: int uv_fs_write(uv_loop_t* loop, uv_fs_t* req, uv_file file, const uv_buf_t bufs[], unsigned int nbufs, int64_t offset, uv_fs_cb cb)

    Equivalent to :man:`pwritev(2)`. If the `offset` argument is `-1`, then
    the current file offset is used and updated.

    .. warning::
        On Windows, under non-MSVC environments (e.g. when GCC or Clang is used
        to build libuv), files opened using ``UV_FS_O_FILEMAP`` may cause a fatal
        crash if the memory mapped write operation fails.

.. c:function:: int uv_fs_mkdir(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode, uv_fs_cb cb)

    Equivalent to :man:`mkdir(2)`.

    .. note::
        `mode` is currently not implemented on Windows.

.. c:function:: int uv_fs_mkdtemp(uv_loop_t* loop, uv_fs_t* req, const char* tpl, uv_fs_cb cb)

    Equivalent to :man:`mkdtemp(3)`. The result can be found as a null terminated string at `req->path`.

.. c:function:: int uv_fs_mkstemp(uv_loop_t* loop, uv_fs_t* req, const char* tpl, uv_fs_cb cb)

    Equivalent to :man:`mkstemp(3)`. The created file path can be found as a null terminated string at `req->path`.
    The file descriptor can be found as an integer at `req->result`.

    .. versionadded:: 1.34.0

.. c:function:: int uv_fs_rmdir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)

    Equivalent to :man:`rmdir(2)`.

.. c:function:: int uv_fs_opendir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)

    Opens `path` as a directory stream. On success, a `uv_dir_t` is allocated
    and returned via `req->ptr`. This memory is not freed by
    `uv_fs_req_cleanup()`, although `req->ptr` is set to `NULL`. The allocated
    memory must be freed by calling `uv_fs_closedir()`. On failure, no memory
    is allocated.

    The contents of the directory can be iterated over by passing the resulting
    `uv_dir_t` to `uv_fs_readdir()`.

    .. versionadded:: 1.28.0

.. c:function:: int uv_fs_closedir(uv_loop_t* loop, uv_fs_t* req, uv_dir_t* dir, uv_fs_cb cb)

    Closes the directory stream represented by `dir` and frees the memory
    allocated by `uv_fs_opendir()`.

    .. versionadded:: 1.28.0

.. c:function:: int uv_fs_readdir(uv_loop_t* loop, uv_fs_t* req, uv_dir_t* dir, uv_fs_cb cb)

    Iterates over the directory stream, `dir`, returned by a successful
    `uv_fs_opendir()` call. Prior to invoking `uv_fs_readdir()`, the caller
    must set `dir->dirents` and `dir->nentries`, representing the array of
    :c:type:`uv_dirent_t` elements used to hold the read directory entries and
    its size.

    On success, the result is an integer >= 0 representing the number of entries
    read from the stream.

    .. versionadded:: 1.28.0

    .. warning::
        `uv_fs_readdir()` is not thread safe.

    .. note::
        This function does not return the "." and ".." entries.

    .. note::
        On success this function allocates memory that must be freed using
        `uv_fs_req_cleanup()`. `uv_fs_req_cleanup()` must be called before
        closing the directory with `uv_fs_closedir()`.

.. c:function:: int uv_fs_scandir(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags, uv_fs_cb cb)
.. c:function:: int uv_fs_scandir_next(uv_fs_t* req, uv_dirent_t* ent)

    Equivalent to :man:`scandir(3)`, with a slightly different API. Once the callback
    for the request is called, the user can use :c:func:`uv_fs_scandir_next` to
    get `ent` populated with the next directory entry data. When there are no
    more entries ``UV_EOF`` will be returned.

    .. note::
        Unlike `scandir(3)`, this function does not return the "." and ".." entries.

    .. note::
        On Linux, getting the type of an entry is only supported by some file systems (btrfs, ext2,
        ext3 and ext4 at the time of this writing), check the :man:`getdents(2)` man page.

.. c:function:: int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)
.. c:function:: int uv_fs_fstat(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb)
.. c:function:: int uv_fs_lstat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)

    Equivalent to :man:`stat(2)`, :man:`fstat(2)` and :man:`lstat(2)` respectively.

.. c:function:: int uv_fs_statfs(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)

    Equivalent to :man:`statfs(2)`. On success, a `uv_statfs_t` is allocated
    and returned via `req->ptr`. This memory is freed by `uv_fs_req_cleanup()`.

    .. note::
        Any fields in the resulting `uv_statfs_t` that are not supported by the
        underlying operating system are set to zero.

    .. versionadded:: 1.31.0

.. c:function:: int uv_fs_rename(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, uv_fs_cb cb)

    Equivalent to :man:`rename(2)`.

.. c:function:: int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb)

    Equivalent to :man:`fsync(2)`.

    .. note::
        For AIX, `uv_fs_fsync` returns `UV_EBADF` on file descriptors referencing
        non regular files.

.. c:function:: int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb)

    Equivalent to :man:`fdatasync(2)`.

.. c:function:: int uv_fs_ftruncate(uv_loop_t* loop, uv_fs_t* req, uv_file file, int64_t offset, uv_fs_cb cb)

    Equivalent to :man:`ftruncate(2)`.

.. c:function:: int uv_fs_copyfile(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, int flags, uv_fs_cb cb)

    Copies a file from `path` to `new_path`. Supported `flags` are described below.

    - `UV_FS_COPYFILE_EXCL`: If present, `uv_fs_copyfile()` will fail with
      `UV_EEXIST` if the destination path already exists. The default behavior
      is to overwrite the destination if it exists.
    - `UV_FS_COPYFILE_FICLONE`: If present, `uv_fs_copyfile()` will attempt to
      create a copy-on-write reflink. If the underlying platform does not
      support copy-on-write, or an error occurs while attempting to use
      copy-on-write, a fallback copy mechanism based on
      :c:func:`uv_fs_sendfile()` is used.
    - `UV_FS_COPYFILE_FICLONE_FORCE`: If present, `uv_fs_copyfile()` will
      attempt to create a copy-on-write reflink. If the underlying platform does
      not support copy-on-write, or an error occurs while attempting to use
      copy-on-write, then an error is returned.

    .. warning::
        If the destination path is created, but an error occurs while copying
        the data, then the destination path is removed. There is a brief window
        of time between closing and removing the file where another process
        could access the file.

    .. versionadded:: 1.14.0

    .. versionchanged:: 1.20.0 `UV_FS_COPYFILE_FICLONE` and
        `UV_FS_COPYFILE_FICLONE_FORCE` are supported.

    .. versionchanged:: 1.33.0 If an error occurs while using
        `UV_FS_COPYFILE_FICLONE_FORCE`, that error is returned. Previously,
        all errors were mapped to `UV_ENOTSUP`.

.. c:function:: int uv_fs_sendfile(uv_loop_t* loop, uv_fs_t* req, uv_file out_fd, uv_file in_fd, int64_t in_offset, size_t length, uv_fs_cb cb)

    Limited equivalent to :man:`sendfile(2)`.

.. c:function:: int uv_fs_access(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode, uv_fs_cb cb)

    Equivalent to :man:`access(2)` on Unix. Windows uses ``GetFileAttributesW()``.

.. c:function:: int uv_fs_chmod(uv_loop_t* loop, uv_fs_t* req, const char* path, int mode, uv_fs_cb cb)
.. c:function:: int uv_fs_fchmod(uv_loop_t* loop, uv_fs_t* req, uv_file file, int mode, uv_fs_cb cb)

    Equivalent to :man:`chmod(2)` and :man:`fchmod(2)` respectively.

.. c:function:: int uv_fs_utime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime, double mtime, uv_fs_cb cb)
.. c:function:: int uv_fs_futime(uv_loop_t* loop, uv_fs_t* req, uv_file file, double atime, double mtime, uv_fs_cb cb)
.. c:function:: int uv_fs_lutime(uv_loop_t* loop, uv_fs_t* req, const char* path, double atime, double mtime, uv_fs_cb cb)

    Equivalent to :man:`utime(2)`, :man:`futimes(3)` and :man:`lutimes(3)` respectively.

    .. note::
      z/OS: `uv_fs_lutime()` is not implemented for z/OS. It can still be called but will return
      ``UV_ENOSYS``.

    .. note::
      AIX: `uv_fs_futime()` and `uv_fs_lutime()` functions only work for AIX 7.1 and newer.
      They can still be called on older versions but will return ``UV_ENOSYS``.

    .. versionchanged:: 1.10.0 sub-second precission is supported on Windows

.. c:function:: int uv_fs_link(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, uv_fs_cb cb)

    Equivalent to :man:`link(2)`.

.. c:function:: int uv_fs_symlink(uv_loop_t* loop, uv_fs_t* req, const char* path, const char* new_path, int flags, uv_fs_cb cb)

    Equivalent to :man:`symlink(2)`.

    .. note::
        On Windows the `flags` parameter can be specified to control how the symlink will
        be created:

            * ``UV_FS_SYMLINK_DIR``: indicates that `path` points to a directory.

            * ``UV_FS_SYMLINK_JUNCTION``: request that the symlink is created
              using junction points.

.. c:function:: int uv_fs_readlink(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)

    Equivalent to :man:`readlink(2)`.
    The resulting string is stored in `req->ptr`.

.. c:function:: int uv_fs_realpath(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb)

    Equivalent to :man:`realpath(3)` on Unix. Windows uses `GetFinalPathNameByHandleW <https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew>`_.
    The resulting string is stored in `req->ptr`.

    .. warning::
        This function has certain platform-specific caveats that were discovered when used in Node.

        * macOS and other BSDs: this function will fail with UV_ELOOP if more than 32 symlinks are
          found while resolving the given path.  This limit is hardcoded and cannot be sidestepped.
        * Windows: while this function works in the common case, there are a number of corner cases
          where it doesn't:

          - Paths in ramdisk volumes created by tools which sidestep the Volume Manager (such as ImDisk)
            cannot be resolved.
          - Inconsistent casing when using drive letters.
          - Resolved path bypasses subst'd drives.

        While this function can still be used, it's not recommended if scenarios such as the
        above need to be supported.

        The background story and some more details on these issues can be checked
        `here <https://github.com/nodejs/node/issues/7726>`_.

    .. versionadded:: 1.8.0

.. c:function:: int uv_fs_chown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid, uv_gid_t gid, uv_fs_cb cb)
.. c:function:: int uv_fs_fchown(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_uid_t uid, uv_gid_t gid, uv_fs_cb cb)
.. c:function:: int uv_fs_lchown(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_uid_t uid, uv_gid_t gid, uv_fs_cb cb)

    Equivalent to :man:`chown(2)`, :man:`fchown(2)` and :man:`lchown(2)` respectively.

    .. note::
        These functions are not implemented on Windows.

    .. versionchanged:: 1.21.0 implemented uv_fs_lchown

.. c:function:: uv_fs_type uv_fs_get_type(const uv_fs_t* req)

    Returns `req->fs_type`.

    .. versionadded:: 1.19.0

.. c:function:: ssize_t uv_fs_get_result(const uv_fs_t* req)

    Returns `req->result`.

    .. versionadded:: 1.19.0

.. c:function:: int uv_fs_get_system_error(const uv_fs_t* req)

    Returns the platform specific error code - `GetLastError()` value on Windows
    and `-(req->result)` on other platforms.

    .. versionadded:: 1.38.0

.. c:function:: void* uv_fs_get_ptr(const uv_fs_t* req)

    Returns `req->ptr`.

    .. versionadded:: 1.19.0

.. c:function:: const char* uv_fs_get_path(const uv_fs_t* req)

    Returns `req->path`.

    .. versionadded:: 1.19.0

.. c:function:: uv_stat_t* uv_fs_get_statbuf(uv_fs_t* req)

    Returns `&req->statbuf`.

    .. versionadded:: 1.19.0

.. seealso:: The :c:type:`uv_req_t` API functions also apply.

Helper functions
----------------

.. c:function:: uv_os_fd_t uv_get_osfhandle(int fd)

   For a file descriptor in the C runtime, get the OS-dependent handle.
   On UNIX, returns the ``fd`` intact. On Windows, this calls `_get_osfhandle <https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/get-osfhandle?view=vs-2019>`_.
   Note that the return value is still owned by the C runtime,
   any attempts to close it or to use it after closing the fd may lead to malfunction.

    .. versionadded:: 1.12.0

.. c:function:: int uv_open_osfhandle(uv_os_fd_t os_fd)

   For a OS-dependent handle, get the file descriptor in the C runtime.
   On UNIX, returns the ``os_fd`` intact. On Windows, this calls `_open_osfhandle <https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/open-osfhandle?view=vs-2019>`_.
   Note that this consumes the argument, any attempts to close it or to use it
   after closing the return value may lead to malfunction.

    .. versionadded:: 1.23.0

File open constants
-------------------

.. c:macro:: UV_FS_O_APPEND

    The file is opened in append mode. Before each write, the file offset is
    positioned at the end of the file.

.. c:macro:: UV_FS_O_CREAT

    The file is created if it does not already exist.

.. c:macro:: UV_FS_O_DIRECT

    File I/O is done directly to and from user-space buffers, which must be
    aligned. Buffer size and address should be a multiple of the physical sector
    size of the block device.

    .. note::
        `UV_FS_O_DIRECT` is supported on Linux, and on Windows via
        `FILE_FLAG_NO_BUFFERING <https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering>`_.
        `UV_FS_O_DIRECT` is not supported on macOS.

.. c:macro:: UV_FS_O_DIRECTORY

    If the path is not a directory, fail the open.

    .. note::
        `UV_FS_O_DIRECTORY` is not supported on Windows.

.. c:macro:: UV_FS_O_DSYNC

    The file is opened for synchronous I/O. Write operations will complete once
    all data and a minimum of metadata are flushed to disk.

    .. note::
        `UV_FS_O_DSYNC` is supported on Windows via
        `FILE_FLAG_WRITE_THROUGH <https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering>`_.

.. c:macro:: UV_FS_O_EXCL

    If the `O_CREAT` flag is set and the file already exists, fail the open.

    .. note::
        In general, the behavior of `O_EXCL` is undefined if it is used without
        `O_CREAT`. There is one exception: on Linux 2.6 and later, `O_EXCL` can
        be used without `O_CREAT` if pathname refers to a block device. If the
        block device is in use by the system (e.g., mounted), the open will fail
        with the error `EBUSY`.

.. c:macro:: UV_FS_O_EXLOCK

    Atomically obtain an exclusive lock.

    .. note::
        `UV_FS_O_EXLOCK` is only supported on macOS and Windows.

    .. versionchanged:: 1.17.0 support is added for Windows.

.. c:macro:: UV_FS_O_FILEMAP

    Use a memory file mapping to access the file. When using this flag, the
    file cannot be open multiple times concurrently.

    .. note::
        `UV_FS_O_FILEMAP` is only supported on Windows.

.. c:macro:: UV_FS_O_NOATIME

    Do not update the file access time when the file is read.

    .. note::
        `UV_FS_O_NOATIME` is not supported on Windows.

.. c:macro:: UV_FS_O_NOCTTY

    If the path identifies a terminal device, opening the path will not cause
    that terminal to become the controlling terminal for the process (if the
    process does not already have one).

    .. note::
        `UV_FS_O_NOCTTY` is not supported on Windows.

.. c:macro:: UV_FS_O_NOFOLLOW

    If the path is a symbolic link, fail the open.

    .. note::
        `UV_FS_O_NOFOLLOW` is not supported on Windows.

.. c:macro:: UV_FS_O_NONBLOCK

    Open the file in nonblocking mode if possible.

    .. note::
        `UV_FS_O_NONBLOCK` is not supported on Windows.

.. c:macro:: UV_FS_O_RANDOM

    Access is intended to be random. The system can use this as a hint to
    optimize file caching.

    .. note::
        `UV_FS_O_RANDOM` is only supported on Windows via
        `FILE_FLAG_RANDOM_ACCESS <https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew>`_.

.. c:macro:: UV_FS_O_RDONLY

    Open the file for read-only access.

.. c:macro:: UV_FS_O_RDWR

    Open the file for read-write access.

.. c:macro:: UV_FS_O_SEQUENTIAL

    Access is intended to be sequential from beginning to end. The system can
    use this as a hint to optimize file caching.

    .. note::
        `UV_FS_O_SEQUENTIAL` is only supported on Windows via
        `FILE_FLAG_SEQUENTIAL_SCAN <https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew>`_.

.. c:macro:: UV_FS_O_SHORT_LIVED

    The file is temporary and should not be flushed to disk if possible.

    .. note::
        `UV_FS_O_SHORT_LIVED` is only supported on Windows via
        `FILE_ATTRIBUTE_TEMPORARY <https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew>`_.

.. c:macro:: UV_FS_O_SYMLINK

    Open the symbolic link itself rather than the resource it points to.

.. c:macro:: UV_FS_O_SYNC

    The file is opened for synchronous I/O. Write operations will complete once
    all data and all metadata are flushed to disk.

    .. note::
        `UV_FS_O_SYNC` is supported on Windows via
        `FILE_FLAG_WRITE_THROUGH <https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering>`_.

.. c:macro:: UV_FS_O_TEMPORARY

    The file is temporary and should not be flushed to disk if possible.

    .. note::
        `UV_FS_O_TEMPORARY` is only supported on Windows via
        `FILE_ATTRIBUTE_TEMPORARY <https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew>`_.

.. c:macro:: UV_FS_O_TRUNC

    If the file exists and is a regular file, and the file is opened
    successfully for write access, its length shall be truncated to zero.

.. c:macro:: UV_FS_O_WRONLY

    Open the file for write-only access.
