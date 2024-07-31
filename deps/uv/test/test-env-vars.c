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

#define BUF_SIZE 10

TEST_IMPL(env_vars) {
  const char* name = "UV_TEST_FOO";
  const char* name2 = "UV_TEST_FOO2";
  char buf[BUF_SIZE];
  size_t size;
  int i, r, envcount, found, found_win_special;
  uv_env_item_t* envitems;

  /* Reject invalid inputs when setting an environment variable */
  r = uv_os_setenv(NULL, "foo");
  ASSERT_EQ(r, UV_EINVAL);
  r = uv_os_setenv(name, NULL);
  ASSERT_EQ(r, UV_EINVAL);
  r = uv_os_setenv(NULL, NULL);
  ASSERT_EQ(r, UV_EINVAL);

  /* Reject invalid inputs when retrieving an environment variable */
  size = BUF_SIZE;
  r = uv_os_getenv(NULL, buf, &size);
  ASSERT_EQ(r, UV_EINVAL);
  r = uv_os_getenv(name, NULL, &size);
  ASSERT_EQ(r, UV_EINVAL);
  r = uv_os_getenv(name, buf, NULL);
  ASSERT_EQ(r, UV_EINVAL);
  size = 0;
  r = uv_os_getenv(name, buf, &size);
  ASSERT_EQ(r, UV_EINVAL);

  /* Reject invalid inputs when deleting an environment variable */
  r = uv_os_unsetenv(NULL);
  ASSERT_EQ(r, UV_EINVAL);

  /* Successfully set an environment variable */
  r = uv_os_setenv(name, "123456789");
  ASSERT_OK(r);

  /* Successfully read an environment variable */
  size = BUF_SIZE;
  buf[0] = '\0';
  r = uv_os_getenv(name, buf, &size);
  ASSERT_OK(r);
  ASSERT_OK(strcmp(buf, "123456789"));
  ASSERT_EQ(size, BUF_SIZE - 1);

  /* Return UV_ENOBUFS if the buffer cannot hold the environment variable */
  size = BUF_SIZE - 1;
  buf[0] = '\0';
  r = uv_os_getenv(name, buf, &size);
  ASSERT_EQ(r, UV_ENOBUFS);
  ASSERT_EQ(size, BUF_SIZE);

  /* Successfully delete an environment variable */
  r = uv_os_unsetenv(name);
  ASSERT_OK(r);

  /* Return UV_ENOENT retrieving an environment variable that does not exist */
  r = uv_os_getenv(name, buf, &size);
  ASSERT_EQ(r, UV_ENOENT);

  /* Successfully delete an environment variable that does not exist */
  r = uv_os_unsetenv(name);
  ASSERT_OK(r);

  /* Setting an environment variable to the empty string does not delete it. */
  r = uv_os_setenv(name, "");
  ASSERT_OK(r);
  size = BUF_SIZE;
  r = uv_os_getenv(name, buf, &size);
  ASSERT_OK(r);
  ASSERT_OK(size);
  ASSERT_OK(strlen(buf));

  /* Check getting all env variables. */
  r = uv_os_setenv(name, "123456789");
  ASSERT_OK(r);
  r = uv_os_setenv(name2, "");
  ASSERT_OK(r);
#ifdef _WIN32
  /* Create a special environment variable on Windows in case there are no
     naturally occurring ones. */
  r = uv_os_setenv("=Z:", "\\");
  ASSERT_OK(r);
#endif

  r = uv_os_environ(&envitems, &envcount);
  ASSERT_OK(r);
  ASSERT_GT(envcount, 0);

  found = 0;
  found_win_special = 0;

  for (i = 0; i < envcount; i++) {
    /* printf("Env: %s = %s\n", envitems[i].name, envitems[i].value); */
    if (strcmp(envitems[i].name, name) == 0) {
      found++;
      ASSERT_OK(strcmp(envitems[i].value, "123456789"));
    } else if (strcmp(envitems[i].name, name2) == 0) {
      found++;
      ASSERT_OK(strlen(envitems[i].value));
    } else if (envitems[i].name[0] == '=') {
      found_win_special++;
    }
  }

  ASSERT_EQ(2, found);
#ifdef _WIN32
  ASSERT_GT(found_win_special, 0);
#else
  /* There's no rule saying a key can't start with '='. */
  (void) &found_win_special;
#endif

  uv_os_free_environ(envitems, envcount);

  r = uv_os_unsetenv(name);
  ASSERT_OK(r);

  r = uv_os_unsetenv(name2);
  ASSERT_OK(r);

  for (i = 1; i <= 4; i++) {
    size_t n;
    char* p;

    n = i * 32768;
    size = n + 1;

    p = malloc(size);
    ASSERT_NOT_NULL(p);

    memset(p, 'x', n);
    p[n] = '\0';

    ASSERT_OK(uv_os_setenv(name, p));
    ASSERT_OK(uv_os_getenv(name, p, &size));
    ASSERT_EQ(n, size);

    for (n = 0; n < size; n++)
      ASSERT_EQ('x', p[n]);

    ASSERT_OK(uv_os_unsetenv(name));
    free(p);
  }

  return 0;
}
