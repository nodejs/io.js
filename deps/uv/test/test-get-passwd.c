/* Copyright libuv contributors. All rights reserved.
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
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#endif

TEST_IMPL(get_passwd) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  uv_passwd_t pwd;
  size_t len;
  int r;

  /* Test the normal case */
  r = uv_os_get_passwd(&pwd);
  ASSERT_OK(r);
  len = strlen(pwd.username);
  ASSERT_GT(len, 0);

#ifdef _WIN32
  ASSERT_NULL(pwd.shell);
#else
  len = strlen(pwd.shell);
# ifndef __PASE__
  ASSERT_GT(len, 0);
# endif
#endif

  len = strlen(pwd.homedir);
  ASSERT_GT(len, 0);

#ifdef _WIN32
  if (len == 3 && pwd.homedir[1] == ':')
    ASSERT_EQ(pwd.homedir[2], '\\');
  else
    ASSERT_NE(pwd.homedir[len - 1], '\\');
#else
  if (len == 1)
    ASSERT_EQ(pwd.homedir[0], '/');
  else
    ASSERT_NE(pwd.homedir[len - 1], '/');
#endif

#ifdef _WIN32
  ASSERT_EQ(pwd.uid, (unsigned)-1);
  ASSERT_EQ(pwd.gid, (unsigned)-1);
#else
  ASSERT_NE(pwd.uid, (unsigned)-1);
  ASSERT_NE(pwd.gid, (unsigned)-1);
  ASSERT_EQ(pwd.uid, geteuid());
  if (pwd.uid != 0 && pwd.gid != getgid())
    /* This will be likely true, as only root could have changed it. */
    ASSERT_EQ(pwd.gid, getegid());
#endif

  /* Test uv_os_free_passwd() */
  uv_os_free_passwd(&pwd);

  ASSERT_NULL(pwd.username);
  ASSERT_NULL(pwd.shell);
  ASSERT_NULL(pwd.homedir);

  /* Test a double free */
  uv_os_free_passwd(&pwd);

  ASSERT_NULL(pwd.username);
  ASSERT_NULL(pwd.shell);
  ASSERT_NULL(pwd.homedir);

  /* Test invalid input */
  r = uv_os_get_passwd(NULL);
  ASSERT_EQ(r, UV_EINVAL);

  return 0;
}


TEST_IMPL(get_passwd2) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  uv_passwd_t pwd;
  uv_passwd_t pwd2;
  size_t len;
  int r;

  /* Test the normal case */
  r = uv_os_get_passwd(&pwd);
  ASSERT_OK(r);

  r = uv_os_get_passwd2(&pwd2, pwd.uid);

#ifdef _WIN32
  ASSERT_EQ(r, UV_ENOTSUP);
  (void) &len;

#else
  ASSERT_OK(r);
  ASSERT_EQ(pwd.uid, pwd2.uid);
  ASSERT_STR_EQ(pwd.username, pwd2.username);
  ASSERT_STR_EQ(pwd.shell, pwd2.shell);
  ASSERT_STR_EQ(pwd.homedir, pwd2.homedir);
  uv_os_free_passwd(&pwd2);

  r = uv_os_get_passwd2(&pwd2, 0);
  ASSERT_OK(r);

  len = strlen(pwd2.username);
  ASSERT_GT(len, 0);
#if defined(__PASE__)
  // uid 0 is qsecofr on IBM i
  ASSERT_STR_EQ(pwd2.username, "qsecofr");
#else
  ASSERT_STR_EQ(pwd2.username, "root");
#endif
  len = strlen(pwd2.homedir);
# ifndef __PASE__
  ASSERT_GT(len, 0);
#endif
  len = strlen(pwd2.shell);
# ifndef __PASE__
  ASSERT_GT(len, 0);
# endif

  uv_os_free_passwd(&pwd2);
#endif

  uv_os_free_passwd(&pwd);

  /* Test invalid input */
  r = uv_os_get_passwd2(NULL, pwd.uid);
#ifdef _WIN32
  ASSERT_EQ(r, UV_ENOTSUP);
#else
  ASSERT_EQ(r, UV_EINVAL);
#endif

  return 0;
}


TEST_IMPL(get_group) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  uv_passwd_t pwd;
  uv_group_t grp;
  size_t len;
  int r;

  r = uv_os_get_passwd(&pwd);
  ASSERT_OK(r);

  r = uv_os_get_group(&grp, pwd.gid);

#ifdef _WIN32
  ASSERT_EQ(r, UV_ENOTSUP);
  (void) &len;

#else
  ASSERT_OK(r);
  ASSERT_EQ(pwd.gid, grp.gid);

  len = strlen(grp.groupname);
  ASSERT_GT(len, 0);

  uv_os_free_group(&grp);
#endif

  uv_os_free_passwd(&pwd);

  /* Test invalid input */
  r = uv_os_get_group(NULL, pwd.gid);
#ifdef _WIN32
  ASSERT_EQ(r, UV_ENOTSUP);
#else
  ASSERT_EQ(r, UV_EINVAL);
#endif

  return 0;
}
