/* Copyright (c) 2014, Ben Noordhuis <info@bnoordhuis.nl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "uv.h"
#include "task.h"

static void timer_cb(uv_timer_t* handle) {
  uv_close((uv_handle_t*) handle, NULL);
}


TEST_IMPL(loop_configure) {
  uv_timer_t timer_handle;
  uv_loop_t loop;
  ASSERT_OK(uv_loop_init(&loop));
#ifdef _WIN32
  ASSERT_EQ(UV_ENOSYS, uv_loop_configure(&loop, UV_LOOP_BLOCK_SIGNAL, 0));
#else
  ASSERT_OK(uv_loop_configure(&loop, UV_LOOP_BLOCK_SIGNAL, SIGPROF));
#endif
  ASSERT_OK(uv_timer_init(&loop, &timer_handle));
  ASSERT_OK(uv_timer_start(&timer_handle, timer_cb, 10, 0));
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_OK(uv_loop_close(&loop));
  return 0;
}
