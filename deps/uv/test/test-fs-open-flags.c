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

#ifdef _WIN32

#include "uv.h"
#include "task.h"

#if defined(__unix__) || defined(__POSIX__) || \
    defined(__APPLE__) || defined(__sun) || \
    defined(_AIX) || defined(__MVS__) || \
    defined(__HAIKU__)
# include <unistd.h> /* unlink, rmdir */
#else
# include <direct.h>
# define rmdir _rmdir
# define unlink _unlink
#endif

static int flags;

static uv_fs_t close_req;
static uv_fs_t mkdir_req;
static uv_fs_t open_req;
static uv_fs_t read_req;
static uv_fs_t rmdir_req;
static uv_fs_t unlink_req;
static uv_fs_t write_req;

static char buf[32];
static uv_buf_t iov;

/* Opening the same file multiple times quickly can cause uv_fs_open to fail
 * with EBUSY, so append an identifier to the file name for each operation */
static int sid = 0;

#define FILE_NAME_SIZE 128
static char absent_file[FILE_NAME_SIZE];
static char empty_file[FILE_NAME_SIZE];
static char dummy_file[FILE_NAME_SIZE];
static char empty_dir[] = "empty_dir";

static void setup(void) {
  int r;

  /* empty_dir */
  r = uv_fs_rmdir(NULL, &rmdir_req, empty_dir, NULL);
  ASSERT(r == 0 || r == UV_ENOENT);
  ASSERT(rmdir_req.result == 0 || rmdir_req.result == UV_ENOENT);
  uv_fs_req_cleanup(&rmdir_req);

  r = uv_fs_mkdir(NULL, &mkdir_req, empty_dir, 0755, NULL);
  ASSERT_OK(r);
  ASSERT_OK(mkdir_req.result);
  uv_fs_req_cleanup(&mkdir_req);
}

static void refresh(void) {
  int r;

  /* absent_file */
  sprintf(absent_file, "test_file_%d", sid++);

  r = uv_fs_unlink(NULL, &unlink_req, absent_file, NULL);
  ASSERT(r == 0 || r == UV_ENOENT);
  ASSERT(unlink_req.result == 0 || unlink_req.result == UV_ENOENT);
  uv_fs_req_cleanup(&unlink_req);

  /* empty_file */
  sprintf(empty_file, "test_file_%d", sid++);

  r = uv_fs_open(NULL, &open_req, empty_file,
    UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_WRONLY, S_IWUSR | S_IRUSR, NULL);
  ASSERT_GE(r, 0);
  ASSERT_GE(open_req.result, 0);
  uv_fs_req_cleanup(&open_req);

  r = uv_fs_close(NULL, &close_req, open_req.result, NULL);
  ASSERT_OK(r);
  ASSERT_OK(close_req.result);
  uv_fs_req_cleanup(&close_req);

  /* dummy_file */
  sprintf(dummy_file, "test_file_%d", sid++);

  r = uv_fs_open(NULL, &open_req, dummy_file,
    UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_WRONLY, S_IWUSR | S_IRUSR, NULL);
  ASSERT_GE(r, 0);
  ASSERT_GE(open_req.result, 0);
  uv_fs_req_cleanup(&open_req);

  iov = uv_buf_init("a", 1);
  r = uv_fs_write(NULL, &write_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(1, r);
  ASSERT_EQ(1, write_req.result);
  uv_fs_req_cleanup(&write_req);

  r = uv_fs_close(NULL, &close_req, open_req.result, NULL);
  ASSERT_OK(r);
  ASSERT_OK(close_req.result);
  uv_fs_req_cleanup(&close_req);
}

static void cleanup(void) {
  unlink(absent_file);
  unlink(empty_file);
  unlink(dummy_file);
}

static void openFail(char *file, int error) {
  int r;

  refresh();

  r = uv_fs_open(NULL, &open_req, file, flags, S_IWUSR | S_IRUSR, NULL);
  ASSERT_EQ(r, error);
  ASSERT_EQ(open_req.result, error);
  uv_fs_req_cleanup(&open_req);

  /* Ensure the first call does not create the file */
  r = uv_fs_open(NULL, &open_req, file, flags, S_IWUSR | S_IRUSR, NULL);
  ASSERT_EQ(r, error);
  ASSERT_EQ(open_req.result, error);
  uv_fs_req_cleanup(&open_req);

  cleanup();
}

static void refreshOpen(char *file) {
  int r;

  refresh();

  r = uv_fs_open(NULL, &open_req, file, flags, S_IWUSR | S_IRUSR, NULL);
  ASSERT_GE(r, 0);
  ASSERT_GE(open_req.result, 0);
  uv_fs_req_cleanup(&open_req);
}

static void writeExpect(char *file, char *expected, int size) {
  int r;

  refreshOpen(file);

  iov = uv_buf_init("b", 1);
  r = uv_fs_write(NULL, &write_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(1, r);
  ASSERT_EQ(1, write_req.result);
  uv_fs_req_cleanup(&write_req);

  iov = uv_buf_init("c", 1);
  r = uv_fs_write(NULL, &write_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(1, r);
  ASSERT_EQ(1, write_req.result);
  uv_fs_req_cleanup(&write_req);

  r = uv_fs_close(NULL, &close_req, open_req.result, NULL);
  ASSERT_OK(r);
  ASSERT_OK(close_req.result);
  uv_fs_req_cleanup(&close_req);

  /* Check contents */
  r = uv_fs_open(NULL, &open_req, file, UV_FS_O_RDONLY, S_IWUSR | S_IRUSR, NULL);
  ASSERT_GE(r, 0);
  ASSERT_GE(open_req.result, 0);
  uv_fs_req_cleanup(&open_req);

  iov = uv_buf_init(buf, sizeof(buf));
  r = uv_fs_read(NULL, &read_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(r, size);
  ASSERT_EQ(read_req.result, size);
  ASSERT_OK(strncmp(buf, expected, size));
  uv_fs_req_cleanup(&read_req);

  r = uv_fs_close(NULL, &close_req, open_req.result, NULL);
  ASSERT_OK(r);
  ASSERT_OK(close_req.result);
  uv_fs_req_cleanup(&close_req);

  cleanup();
}

static void writeFail(char *file, int error) {
  int r;

  refreshOpen(file);

  iov = uv_buf_init("z", 1);
  r = uv_fs_write(NULL, &write_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(r, error);
  ASSERT_EQ(write_req.result, error);
  uv_fs_req_cleanup(&write_req);

  iov = uv_buf_init("z", 1);
  r = uv_fs_write(NULL, &write_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(r, error);
  ASSERT_EQ(write_req.result, error);
  uv_fs_req_cleanup(&write_req);

  r = uv_fs_close(NULL, &close_req, open_req.result, NULL);
  ASSERT_OK(r);
  ASSERT_OK(close_req.result);
  uv_fs_req_cleanup(&close_req);

  cleanup();
}

static void readExpect(char *file, char *expected, int size) {
  int r;

  refreshOpen(file);

  iov = uv_buf_init(buf, sizeof(buf));
  r = uv_fs_read(NULL, &read_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(r, size);
  ASSERT_EQ(read_req.result, size);
  ASSERT_OK(strncmp(buf, expected, size));
  uv_fs_req_cleanup(&read_req);

  r = uv_fs_close(NULL, &close_req, open_req.result, NULL);
  ASSERT_OK(r);
  ASSERT_OK(close_req.result);
  uv_fs_req_cleanup(&close_req);

  cleanup();
}

static void readFail(char *file, int error) {
  int r;

  refreshOpen(file);

  iov = uv_buf_init(buf, sizeof(buf));
  r = uv_fs_read(NULL, &read_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(r, error);
  ASSERT_EQ(read_req.result, error);
  uv_fs_req_cleanup(&read_req);

  iov = uv_buf_init(buf, sizeof(buf));
  r = uv_fs_read(NULL, &read_req, open_req.result, &iov, 1, -1, NULL);
  ASSERT_EQ(r, error);
  ASSERT_EQ(read_req.result, error);
  uv_fs_req_cleanup(&read_req);

  r = uv_fs_close(NULL, &close_req, open_req.result, NULL);
  ASSERT_OK(r);
  ASSERT_OK(close_req.result);
  uv_fs_req_cleanup(&close_req);

  cleanup();
}

static void fs_open_flags(int add_flags) {
  /* Follow the order from
   * https://github.com/nodejs/node/blob/1a96abe849/lib/internal/fs/utils.js#L329-L354
   */

  /* r */
  flags = add_flags | UV_FS_O_RDONLY;
  openFail(absent_file, UV_ENOENT);
  writeFail(empty_file, UV_EBADF);
  readExpect(empty_file, "", 0);
  writeFail(dummy_file, UV_EBADF);
  readExpect(dummy_file, "a", 1);
  writeFail(empty_dir, UV_EBADF);
  readFail(empty_dir, UV_EISDIR);

  /* rs */
  flags = add_flags | UV_FS_O_RDONLY | UV_FS_O_SYNC;
  openFail(absent_file, UV_ENOENT);
  writeFail(empty_file, UV_EBADF);
  readExpect(empty_file, "", 0);
  writeFail(dummy_file, UV_EBADF);
  readExpect(dummy_file, "a", 1);
  writeFail(empty_dir, UV_EBADF);
  readFail(empty_dir, UV_EISDIR);

  /* r+ */
  flags = add_flags | UV_FS_O_RDWR;
  openFail(absent_file, UV_ENOENT);
  writeExpect(empty_file, "bc", 2);
  readExpect(empty_file, "", 0);
  writeExpect(dummy_file, "bc", 2);
  readExpect(dummy_file, "a", 1);
  writeFail(empty_dir, UV_EISDIR);
  readFail(empty_dir, UV_EISDIR);

  /* rs+ */
  flags = add_flags | UV_FS_O_RDWR | UV_FS_O_SYNC;
  openFail(absent_file, UV_ENOENT);
  writeExpect(empty_file, "bc", 2);
  readExpect(empty_file, "", 0);
  writeExpect(dummy_file, "bc", 2);
  readExpect(dummy_file, "a", 1);
  writeFail(empty_dir, UV_EISDIR);
  readFail(empty_dir, UV_EISDIR);

  /* w */
  flags = add_flags | UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_WRONLY;
  writeExpect(absent_file, "bc", 2);
  readFail(absent_file, UV_EBADF);
  writeExpect(empty_file, "bc", 2);
  readFail(empty_file, UV_EBADF);
  writeExpect(dummy_file, "bc", 2);
  readFail(dummy_file, UV_EBADF);
  openFail(empty_dir, UV_EISDIR);

  /* wx */
  flags = add_flags | UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_WRONLY |
    UV_FS_O_EXCL;
  writeExpect(absent_file, "bc", 2);
  readFail(absent_file, UV_EBADF);
  openFail(empty_file, UV_EEXIST);
  openFail(dummy_file, UV_EEXIST);
  openFail(empty_dir, UV_EEXIST);

  /* w+ */
  flags = add_flags | UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_RDWR;
  writeExpect(absent_file, "bc", 2);
  readExpect(absent_file, "", 0);
  writeExpect(empty_file, "bc", 2);
  readExpect(empty_file, "", 0);
  writeExpect(dummy_file, "bc", 2);
  readExpect(dummy_file, "", 0);
  openFail(empty_dir, UV_EISDIR);

  /* wx+ */
  flags = add_flags | UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_RDWR |
    UV_FS_O_EXCL;
  writeExpect(absent_file, "bc", 2);
  readExpect(absent_file, "", 0);
  openFail(empty_file, UV_EEXIST);
  openFail(dummy_file, UV_EEXIST);
  openFail(empty_dir, UV_EEXIST);

  /* a */
  flags = add_flags | UV_FS_O_APPEND | UV_FS_O_CREAT | UV_FS_O_WRONLY;
  writeExpect(absent_file, "bc", 2);
  readFail(absent_file, UV_EBADF);
  writeExpect(empty_file, "bc", 2);
  readFail(empty_file, UV_EBADF);
  writeExpect(dummy_file, "abc", 3);
  readFail(dummy_file, UV_EBADF);
  writeFail(empty_dir, UV_EISDIR);
  readFail(empty_dir, UV_EBADF);

  /* ax */
  flags = add_flags | UV_FS_O_APPEND | UV_FS_O_CREAT | UV_FS_O_WRONLY |
    UV_FS_O_EXCL;
  writeExpect(absent_file, "bc", 2);
  readFail(absent_file, UV_EBADF);
  openFail(empty_file, UV_EEXIST);
  openFail(dummy_file, UV_EEXIST);
  openFail(empty_dir, UV_EEXIST);

  /* as */
  flags = add_flags | UV_FS_O_APPEND | UV_FS_O_CREAT | UV_FS_O_WRONLY |
    UV_FS_O_SYNC;
  writeExpect(absent_file, "bc", 2);
  readFail(absent_file, UV_EBADF);
  writeExpect(empty_file, "bc", 2);
  readFail(empty_file, UV_EBADF);
  writeExpect(dummy_file, "abc", 3);
  readFail(dummy_file, UV_EBADF);
  writeFail(empty_dir, UV_EISDIR);
  readFail(empty_dir, UV_EBADF);

  /* a+ */
  flags = add_flags | UV_FS_O_APPEND | UV_FS_O_CREAT | UV_FS_O_RDWR;
  writeExpect(absent_file, "bc", 2);
  readExpect(absent_file, "", 0);
  writeExpect(empty_file, "bc", 2);
  readExpect(empty_file, "", 0);
  writeExpect(dummy_file, "abc", 3);
  readExpect(dummy_file, "a", 1);
  writeFail(empty_dir, UV_EISDIR);
  readFail(empty_dir, UV_EISDIR);

  /* ax+ */
  flags = add_flags | UV_FS_O_APPEND | UV_FS_O_CREAT | UV_FS_O_RDWR |
    UV_FS_O_EXCL;
  writeExpect(absent_file, "bc", 2);
  readExpect(absent_file, "", 0);
  openFail(empty_file, UV_EEXIST);
  openFail(dummy_file, UV_EEXIST);
  openFail(empty_dir, UV_EEXIST);

  /* as+ */
  flags = add_flags | UV_FS_O_APPEND | UV_FS_O_CREAT | UV_FS_O_RDWR |
    UV_FS_O_SYNC;
  writeExpect(absent_file, "bc", 2);
  readExpect(absent_file, "", 0);
  writeExpect(empty_file, "bc", 2);
  readExpect(empty_file, "", 0);
  writeExpect(dummy_file, "abc", 3);
  readExpect(dummy_file, "a", 1);
  writeFail(empty_dir, UV_EISDIR);
  readFail(empty_dir, UV_EISDIR);
}
TEST_IMPL(fs_open_flags) {
  setup();

  fs_open_flags(0);
  fs_open_flags(UV_FS_O_FILEMAP);

  /* Cleanup. */
  rmdir(empty_dir);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

#else

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif  /* ifndef _WIN32 */
