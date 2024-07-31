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

#include <string.h>

#ifdef __APPLE__
# define NUM_ITERATIONS 5
#else
# define NUM_ITERATIONS 50
#endif

static const char* titles[] = {
  "8L2NY0Kdj0XyNFZnmUZigIOfcWjyNr0SkMmUhKw99VLUsZFrvCQQC3XIRfNR8pjyMjXObllled",
  "jUAcscJN49oLSN8GdmXj2Wo34XX2T2vp2j5khfajNQarlOulp57cE130yiY53ipJFnPyTn5i82",
  "9niCI5icXGFS72XudhXqo5alftmZ1tpE7B3cwUmrq0CCDjC84FzBNB8XAHqvpNQfI2QAQG6ztT",
  "n8qXVXuG6IEHDpabJgTEiwtpY6LHMZ8MgznnMpdHARu5EywufA6hcBaQfetb0YhEsK0ykDd7JU"
};

static void getter_thread_body(void* arg) {
  uv_sem_t* getter_sem;
  char buffer[512];
  size_t len;

  getter_sem = arg;

  while (UV_EAGAIN == uv_sem_trywait(getter_sem)) {
    ASSERT_OK(uv_get_process_title(buffer, sizeof(buffer)));

    /* The maximum size of the process title on some platforms depends on
     * the total size of the argv vector. It's therefore possible to read
     * back a title that's shorter than what we submitted.
     */
    len = strlen(buffer);
    ASSERT_GT(len, 0);

    ASSERT(
      0 == strncmp(buffer, titles[0], len) ||
      0 == strncmp(buffer, titles[1], len) ||
      0 == strncmp(buffer, titles[2], len) ||
      0 == strncmp(buffer, titles[3], len));

    uv_sleep(0);
  }
}


static void setter_thread_body(void* arg) {
  int i;

  for (i = 0; i < NUM_ITERATIONS; i++) {
    ASSERT_OK(uv_set_process_title(titles[0]));
    ASSERT_OK(uv_set_process_title(titles[1]));
    ASSERT_OK(uv_set_process_title(titles[2]));
    ASSERT_OK(uv_set_process_title(titles[3]));
  }
}


TEST_IMPL(process_title_threadsafe) {
  uv_thread_t setter_threads[4];
  uv_thread_t getter_thread;
  uv_sem_t getter_sem;
  int i;

#if defined(__sun) || defined(__CYGWIN__) || defined(__MSYS__) || \
    defined(__MVS__) || defined(__PASE__)
  RETURN_SKIP("uv_(get|set)_process_title is not implemented.");
#endif

  ASSERT_OK(uv_set_process_title(titles[0]));

  ASSERT_OK(uv_sem_init(&getter_sem, 0));
  ASSERT_OK(uv_thread_create(&getter_thread, getter_thread_body, &getter_sem));

  for (i = 0; i < (int) ARRAY_SIZE(setter_threads); i++)
    ASSERT_OK(uv_thread_create(&setter_threads[i], setter_thread_body, NULL));

  for (i = 0; i < (int) ARRAY_SIZE(setter_threads); i++)
    ASSERT_OK(uv_thread_join(&setter_threads[i]));

  uv_sem_post(&getter_sem);
  ASSERT_OK(uv_thread_join(&getter_thread));
  uv_sem_destroy(&getter_sem);

  return 0;
}
