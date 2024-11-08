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

#include "uv.h"
#include "task.h"

#if defined(__unix__) || defined(__POSIX__) || \
    defined(__APPLE__) || defined(__sun) || \
    defined(_AIX) || defined(__MVS__) || \
    defined(__HAIKU__) || defined(__QNX__)
#include <unistd.h> /* unlink, etc. */
#else
# include <direct.h>
# include <io.h>
# define unlink _unlink
#endif

static const char fixture[] = "test/fixtures/load_error.node";
static const char dst[] = "test_file_dst";
static int result_check_count;


static void fail_cb(uv_fs_t* req) {
  FATAL("fail_cb should not have been called");
}

static void handle_result(uv_fs_t* req) {
  uv_fs_t stat_req;
  uint64_t size;
  uint64_t mode;
  uint64_t uid;
  uint64_t gid;
  int r;

  ASSERT_EQ(req->fs_type, UV_FS_COPYFILE);
  ASSERT_OK(req->result);

  /* Verify that the file size and mode are the same. */
  r = uv_fs_stat(NULL, &stat_req, req->path, NULL);
  ASSERT_OK(r);
  size = stat_req.statbuf.st_size;
  mode = stat_req.statbuf.st_mode;
  uid = stat_req.statbuf.st_uid;
  gid = stat_req.statbuf.st_gid;
  uv_fs_req_cleanup(&stat_req);
  r = uv_fs_stat(NULL, &stat_req, dst, NULL);
  ASSERT_OK(r);
  ASSERT_EQ(stat_req.statbuf.st_size, size);
  ASSERT_EQ(stat_req.statbuf.st_mode, mode);
  ASSERT_EQ(stat_req.statbuf.st_uid, uid);
  ASSERT_EQ(stat_req.statbuf.st_gid, gid);
  uv_fs_req_cleanup(&stat_req);
  uv_fs_req_cleanup(req);
  result_check_count++;
}


static void touch_file(const char* name, unsigned int size) {
  uv_file file;
  uv_fs_t req;
  uv_buf_t buf;
  int r;
  unsigned int i;

  r = uv_fs_open(NULL, &req, name,
                 UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC,
                 S_IWUSR | S_IRUSR, NULL);
  uv_fs_req_cleanup(&req);
  ASSERT_GE(r, 0);
  file = r;

  buf = uv_buf_init("a", 1);

  /* Inefficient but simple. */
  for (i = 0; i < size; i++) {
    r = uv_fs_write(NULL, &req, file, &buf, 1, i, NULL);
    uv_fs_req_cleanup(&req);
    ASSERT_GE(r, 0);
  }

  r = uv_fs_close(NULL, &req, file, NULL);
  uv_fs_req_cleanup(&req);
  ASSERT_OK(r);
}


TEST_IMPL(fs_copyfile) {
  const char src[] = "test_file_src";
  uv_loop_t* loop;
  uv_fs_t req;
  int r;

  loop = uv_default_loop();

  /* Fails with EINVAL if bad flags are passed. */
  r = uv_fs_copyfile(NULL, &req, src, dst, -1, NULL);
  ASSERT_EQ(r, UV_EINVAL);
  uv_fs_req_cleanup(&req);

  /* Fails with ENOENT if source does not exist. */
  unlink(src);
  unlink(dst);
  r = uv_fs_copyfile(NULL, &req, src, dst, 0, NULL);
  ASSERT_EQ(req.result, UV_ENOENT);
  ASSERT_EQ(r, UV_ENOENT);
  uv_fs_req_cleanup(&req);
  /* The destination should not exist. */
  r = uv_fs_stat(NULL, &req, dst, NULL);
  ASSERT(r);
  uv_fs_req_cleanup(&req);

  /* Succeeds if src and dst files are identical. */
  touch_file(src, 12);
  r = uv_fs_copyfile(NULL, &req, src, src, 0, NULL);
  ASSERT_OK(r);
  uv_fs_req_cleanup(&req);
  /* Verify that the src file did not get truncated. */
  r = uv_fs_stat(NULL, &req, src, NULL);
  ASSERT_OK(r);
  ASSERT_EQ(12, req.statbuf.st_size);
  uv_fs_req_cleanup(&req);
  unlink(src);

  /* Copies file synchronously. Creates new file. */
  unlink(dst);
  r = uv_fs_copyfile(NULL, &req, fixture, dst, 0, NULL);
  ASSERT_OK(r);
  handle_result(&req);

  /* Copies a file of size zero. */
  unlink(dst);
  touch_file(src, 0);
  r = uv_fs_copyfile(NULL, &req, src, dst, 0, NULL);
  ASSERT_OK(r);
  handle_result(&req);

  /* Copies file synchronously. Overwrites existing file. */
  r = uv_fs_copyfile(NULL, &req, fixture, dst, 0, NULL);
  ASSERT_OK(r);
  handle_result(&req);

  /* Fails to overwrites existing file. */
  ASSERT_OK(uv_fs_chmod(NULL, &req, dst, 0644, NULL));
  uv_fs_req_cleanup(&req);
  r = uv_fs_copyfile(NULL, &req, fixture, dst, UV_FS_COPYFILE_EXCL, NULL);
  ASSERT_EQ(r, UV_EEXIST);
  uv_fs_req_cleanup(&req);

  /* Truncates when an existing destination is larger than the source file. */
  ASSERT_OK(uv_fs_chmod(NULL, &req, dst, 0644, NULL));
  uv_fs_req_cleanup(&req);
  touch_file(src, 1);
  r = uv_fs_copyfile(NULL, &req, src, dst, 0, NULL);
  ASSERT_OK(r);
  handle_result(&req);

  /* Copies a larger file. */
  unlink(dst);
  touch_file(src, 4096 * 2);
  r = uv_fs_copyfile(NULL, &req, src, dst, 0, NULL);
  ASSERT_OK(r);
  handle_result(&req);
  unlink(src);

  /* Copies file asynchronously */
  unlink(dst);
  r = uv_fs_copyfile(loop, &req, fixture, dst, 0, handle_result);
  ASSERT_OK(r);
  ASSERT_EQ(5, result_check_count);
  uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_EQ(6, result_check_count);
  /* Ensure file is user-writable (not copied from src). */
  ASSERT_OK(uv_fs_chmod(NULL, &req, dst, 0644, NULL));
  uv_fs_req_cleanup(&req);

  /* If the flags are invalid, the loop should not be kept open */
  unlink(dst);
  r = uv_fs_copyfile(loop, &req, fixture, dst, -1, fail_cb);
  ASSERT_EQ(r, UV_EINVAL);
  uv_run(loop, UV_RUN_DEFAULT);

  /* Copies file using UV_FS_COPYFILE_FICLONE. */
  unlink(dst);
  r = uv_fs_copyfile(NULL, &req, fixture, dst, UV_FS_COPYFILE_FICLONE, NULL);
  ASSERT_OK(r);
  handle_result(&req);

  /* Copies file using UV_FS_COPYFILE_FICLONE_FORCE. */
  unlink(dst);
  r = uv_fs_copyfile(NULL, &req, fixture, dst, UV_FS_COPYFILE_FICLONE_FORCE,
                     NULL);
  ASSERT_LE(r, 0);

  if (r == 0)
    handle_result(&req);

#ifndef _WIN32
  /* Copying respects permissions/mode. */
  unlink(dst);
  touch_file(dst, 0);
  chmod(dst, S_IRUSR|S_IRGRP|S_IROTH); /* Sets file mode to 444 (read-only). */
  r = uv_fs_copyfile(NULL, &req, fixture, dst, 0, NULL);
  /* On IBMi PASE, qsecofr users can overwrite read-only files */
# ifndef __PASE__
  ASSERT_EQ(req.result, UV_EACCES);
  ASSERT_EQ(r, UV_EACCES);
# endif
  uv_fs_req_cleanup(&req);
#endif

  unlink(dst); /* Cleanup */
  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
