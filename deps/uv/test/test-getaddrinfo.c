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
#include <stdlib.h>

#define CONCURRENT_COUNT    10

static const char* name = "localhost";

static int getaddrinfo_cbs = 0;

/* data used for running multiple calls concurrently */
static uv_getaddrinfo_t* getaddrinfo_handle;
static uv_getaddrinfo_t getaddrinfo_handles[CONCURRENT_COUNT];
static int callback_counts[CONCURRENT_COUNT];
static int fail_cb_called;


static void getaddrinfo_fail_cb(uv_getaddrinfo_t* req,
                                int status,
                                struct addrinfo* res) {

  ASSERT_OK(fail_cb_called);
  ASSERT_LT(status, 0);
  ASSERT_NULL(res);
  uv_freeaddrinfo(res);  /* Should not crash. */
  fail_cb_called++;
}


static void getaddrinfo_basic_cb(uv_getaddrinfo_t* handle,
                                 int status,
                                 struct addrinfo* res) {
  ASSERT_PTR_EQ(handle, getaddrinfo_handle);
  getaddrinfo_cbs++;
  free(handle);
  uv_freeaddrinfo(res);
}


static void getaddrinfo_cuncurrent_cb(uv_getaddrinfo_t* handle,
                                      int status,
                                      struct addrinfo* res) {
  int i;
  int* data = (int*)handle->data;

  for (i = 0; i < CONCURRENT_COUNT; i++) {
    if (&getaddrinfo_handles[i] == handle) {
      ASSERT_EQ(i, *data);

      callback_counts[i]++;
      break;
    }
  }
  ASSERT (i < CONCURRENT_COUNT);

  free(data);
  uv_freeaddrinfo(res);

  getaddrinfo_cbs++;
}


TEST_IMPL(getaddrinfo_fail) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif
  
  uv_getaddrinfo_t req;

  ASSERT_EQ(UV_EINVAL, uv_getaddrinfo(uv_default_loop(),
                                      &req,
                                      (uv_getaddrinfo_cb) abort,
                                      NULL,
                                      NULL,
                                      NULL));

  /* Use a FQDN by ending in a period */
  ASSERT_OK(uv_getaddrinfo(uv_default_loop(),
                           &req,
                           getaddrinfo_fail_cb,
                           "example.invalid.",
                           NULL,
                           NULL));
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_EQ(1, fail_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(getaddrinfo_fail_sync) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif
  uv_getaddrinfo_t req;

  /* Use a FQDN by ending in a period */
  ASSERT_GT(0, uv_getaddrinfo(uv_default_loop(),
                              &req,
                              NULL,
                              "example.invalid.",
                              NULL,
                              NULL));
  uv_freeaddrinfo(req.addrinfo);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(getaddrinfo_basic) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  int r;
  getaddrinfo_handle = (uv_getaddrinfo_t*)malloc(sizeof(uv_getaddrinfo_t));

  r = uv_getaddrinfo(uv_default_loop(),
                     getaddrinfo_handle,
                     &getaddrinfo_basic_cb,
                     name,
                     NULL,
                     NULL);
  ASSERT_OK(r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, getaddrinfo_cbs);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(getaddrinfo_basic_sync) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif
  uv_getaddrinfo_t req;

  ASSERT_OK(uv_getaddrinfo(uv_default_loop(),
                           &req,
                           NULL,
                           name,
                           NULL,
                           NULL));
  uv_freeaddrinfo(req.addrinfo);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(getaddrinfo_concurrent) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif
  
  int i, r;
  int* data;

  for (i = 0; i < CONCURRENT_COUNT; i++) {
    callback_counts[i] = 0;

    data = (int*)malloc(sizeof(int));
    ASSERT_NOT_NULL(data);
    *data = i;
    getaddrinfo_handles[i].data = data;

    r = uv_getaddrinfo(uv_default_loop(),
                       &getaddrinfo_handles[i],
                       &getaddrinfo_cuncurrent_cb,
                       name,
                       NULL,
                       NULL);
    ASSERT_OK(r);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  for (i = 0; i < CONCURRENT_COUNT; i++) {
    ASSERT_EQ(1, callback_counts[i]);
  }

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
