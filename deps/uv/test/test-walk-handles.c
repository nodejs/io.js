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

#include <stdio.h>
#include <stdlib.h>

static char magic_cookie[] = "magic cookie";
static int seen_timer_handle;
static uv_timer_t timer;


static void walk_cb(uv_handle_t* handle, void* arg) {
  ASSERT_PTR_EQ(arg, (void*)magic_cookie);

  if (handle == (uv_handle_t*)&timer) {
    seen_timer_handle++;
  } else {
    ASSERT(0 && "unexpected handle");
  }
}


static void timer_cb(uv_timer_t* handle) {
  ASSERT_PTR_EQ(handle, &timer);

  uv_walk(handle->loop, walk_cb, magic_cookie);
  uv_close((uv_handle_t*)handle, NULL);
}


TEST_IMPL(walk_handles) {
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();

  r = uv_timer_init(loop, &timer);
  ASSERT_OK(r);

  r = uv_timer_start(&timer, timer_cb, 1, 0);
  ASSERT_OK(r);

  /* Start event loop, expect to see the timer handle in walk_cb. */
  ASSERT_OK(seen_timer_handle);
  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);
  ASSERT_EQ(1, seen_timer_handle);

  /* Loop is finished, walk_cb should not see our timer handle. */
  seen_timer_handle = 0;
  uv_walk(loop, walk_cb, magic_cookie);
  ASSERT_OK(seen_timer_handle);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
