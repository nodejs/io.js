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

#include "uv.h"
#include "task.h"

uv_thread_t main_thread_id;
uv_thread_t subthreads[2];

static void check_thread(void* arg) {
  uv_thread_t *thread_id = arg;
  uv_thread_t self_id = uv_thread_self();
#ifdef _WIN32
  ASSERT_NOT_NULL(self_id);
#endif
  ASSERT_OK(uv_thread_equal(&main_thread_id, &self_id));
  *thread_id = uv_thread_self();
}

TEST_IMPL(thread_equal) {
  uv_thread_t threads[2];
  main_thread_id = uv_thread_self();
#ifdef _WIN32
  ASSERT_NOT_NULL(main_thread_id);
#endif
  ASSERT_NE(0, uv_thread_equal(&main_thread_id, &main_thread_id));
  ASSERT_OK(uv_thread_create(threads + 0, check_thread, subthreads + 0));
  ASSERT_OK(uv_thread_create(threads + 1, check_thread, subthreads + 1));
  ASSERT_OK(uv_thread_join(threads + 0));
  ASSERT_OK(uv_thread_join(threads + 1));
  ASSERT_OK(uv_thread_equal(subthreads + 0, subthreads + 1));
  return 0;
}
