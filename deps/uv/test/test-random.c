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

static char scratch[256];
static int random_cb_called;


static void random_cb(uv_random_t* req, int status, void* buf, size_t buflen) {
  char zero[sizeof(scratch)];

  memset(zero, 0, sizeof(zero));

  ASSERT_OK(status);
  ASSERT_PTR_EQ(buf, (void*) scratch);

  if (random_cb_called == 0) {
    ASSERT_OK(buflen);
    ASSERT_OK(memcmp(scratch, zero, sizeof(zero)));
  } else {
    ASSERT_EQ(buflen, sizeof(scratch));
    /* Buy a lottery ticket if you manage to trip this assertion. */
    ASSERT_NE(0, memcmp(scratch, zero, sizeof(zero)));
  }

  random_cb_called++;
}


TEST_IMPL(random_async) {
  uv_random_t req;
  uv_loop_t* loop;

  loop = uv_default_loop();
  ASSERT_EQ(UV_EINVAL, uv_random(loop, &req, scratch, sizeof(scratch), -1,
                                 random_cb));
  ASSERT_EQ(UV_E2BIG, uv_random(loop, &req, scratch, -1, -1, random_cb));

  ASSERT_OK(uv_random(loop, &req, scratch, 0, 0, random_cb));
  ASSERT_OK(random_cb_called);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(1, random_cb_called);

  ASSERT_OK(uv_random(loop, &req, scratch, sizeof(scratch), 0, random_cb));
  ASSERT_EQ(1, random_cb_called);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  ASSERT_EQ(2, random_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(random_sync) {
  char zero[256];
  char buf[256];

  ASSERT_EQ(UV_EINVAL, uv_random(NULL, NULL, buf, sizeof(buf), -1, NULL));
  ASSERT_EQ(UV_E2BIG, uv_random(NULL, NULL, buf, -1, -1, NULL));

  memset(buf, 0, sizeof(buf));
  ASSERT_OK(uv_random(NULL, NULL, buf, sizeof(buf), 0, NULL));

  /* Buy a lottery ticket if you manage to trip this assertion. */
  memset(zero, 0, sizeof(zero));
  ASSERT_NE(0, memcmp(buf, zero, sizeof(zero)));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
