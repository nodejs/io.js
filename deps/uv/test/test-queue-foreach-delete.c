/* Copyright The libuv project and contributors. All rights reserved.
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


/*
 * The idea behind the test is as follows.
 * Certain handle types are stored in a queue internally.
 * Extra care should be taken for removal of a handle from the queue while iterating over the queue.
 * (i.e., uv__queue_remove() called within uv__queue_foreach())
 * This usually happens when someone closes or stops a handle from within its callback.
 * So we need to check that we haven't screwed the queue on close/stop.
 * To do so we do the following (for each handle type):
 *  1. Create and start 3 handles (#0, #1, and #2).
 *
 *     The queue after the start() calls:
 *     ..=> [queue head] <=> [handle] <=> [handle #1] <=> [handle] <=..
 *
 *  2. Trigger handles to fire (for uv_idle_t, uv_prepare_t, and uv_check_t there is nothing to do).
 *
 *  3. In the callback for the first-executed handle (#0 or #2 depending on handle type)
 *     stop the handle and the next one (#1).
 *     (for uv_idle_t, uv_prepare_t, and uv_check_t callbacks are executed in the reverse order as they are start()'ed,
 *     so callback for handle #2 will be called first)
 *
 *     The queue after the stop() calls:
 *                                correct foreach "next"  |
 *                                                       \/
 *     ..=> [queue head] <==============================> [handle] <=..
 *          [          ] <-  [handle] <=> [handle #1]  -> [      ]
 *                                       /\
 *                  wrong foreach "next"  |
 *
 *  4. The callback for handle #1 shouldn't be called because the handle #1 is stopped in the previous step.
 *     However, if uv__queue_remove() is not handled properly within uv__queue_foreach(), the callback _will_
 *     be called.
 */

static const unsigned first_handle_number_idle     = 2;
static const unsigned first_handle_number_prepare  = 2;
static const unsigned first_handle_number_check    = 2;
#ifdef __linux__
static const unsigned first_handle_number_fs_event = 0;
#endif


#define DEFINE_GLOBALS_AND_CBS(name, ...)                                     \
  static uv_##name##_t (name)[3];                                             \
  static unsigned name##_cb_calls[3];                                         \
                                                                              \
  static void name##2_cb(__VA_ARGS__) {                                       \
    ASSERT_PTR_EQ(handle, &(name)[2]);                                        \
    if (first_handle_number_##name == 2) {                                    \
      uv_close((uv_handle_t*)&(name)[2], NULL);                               \
      uv_close((uv_handle_t*)&(name)[1], NULL);                               \
    }                                                                         \
    name##_cb_calls[2]++;                                                     \
  }                                                                           \
                                                                              \
  static void name##1_cb(__VA_ARGS__) {                                       \
    ASSERT_PTR_EQ(handle, &(name)[1]);                                        \
    ASSERT(0 && "Shouldn't be called" && (&name[0]));                         \
  }                                                                           \
                                                                              \
  static void name##0_cb(__VA_ARGS__) {                                       \
    ASSERT_PTR_EQ(handle, &(name)[0]);                                        \
    if (first_handle_number_##name == 0) {                                    \
      uv_close((uv_handle_t*)&(name)[0], NULL);                               \
      uv_close((uv_handle_t*)&(name)[1], NULL);                               \
    }                                                                         \
    name##_cb_calls[0]++;                                                     \
  }                                                                           \
                                                                              \
  static const uv_##name##_cb name##_cbs[] = {                                \
    name##0_cb,                                                               \
    name##1_cb,                                                               \
    name##2_cb,                                                               \
  };

#define INIT_AND_START(name, loop)                                            \
  do {                                                                        \
    size_t i;                                                                 \
    for (i = 0; i < ARRAY_SIZE(name); i++) {                                  \
      int r;                                                                  \
      r = uv_##name##_init((loop), &(name)[i]);                               \
      ASSERT_OK(r);                                                           \
                                                                              \
      r = uv_##name##_start(&(name)[i], name##_cbs[i]);                       \
      ASSERT_OK(r);                                                           \
    }                                                                         \
  } while (0)

#define END_ASSERTS(name)                                                     \
  do {                                                                        \
    ASSERT_EQ(1, name##_cb_calls[0]);                                         \
    ASSERT_OK(name##_cb_calls[1]);                                            \
    ASSERT_EQ(1, name##_cb_calls[2]);                                         \
  } while (0)

DEFINE_GLOBALS_AND_CBS(idle, uv_idle_t* handle)
DEFINE_GLOBALS_AND_CBS(prepare, uv_prepare_t* handle)
DEFINE_GLOBALS_AND_CBS(check, uv_check_t* handle)

#ifdef __linux__
DEFINE_GLOBALS_AND_CBS(fs_event,
                       uv_fs_event_t* handle,
                       const char* filename,
                       int events,
                       int status)

static const char watched_dir[] = ".";
static uv_timer_t timer;
static unsigned helper_timer_cb_calls;


static void init_and_start_fs_events(uv_loop_t* loop) {
  size_t i;
  for (i = 0; i < ARRAY_SIZE(fs_event); i++) {
    int r;
    r = uv_fs_event_init(loop, &fs_event[i]);
    ASSERT_OK(r);

    r = uv_fs_event_start(&fs_event[i],
                          (uv_fs_event_cb)fs_event_cbs[i],
                          watched_dir,
                          0);
    ASSERT_OK(r);
  }
}

static void helper_timer_cb(uv_timer_t* thandle) {
  int r;
  uv_fs_t fs_req;

  /* fire all fs_events */
  r = uv_fs_utime(thandle->loop, &fs_req, watched_dir, 0, 0, NULL);
  ASSERT_OK(r);
  ASSERT_OK(fs_req.result);
  ASSERT_EQ(fs_req.fs_type, UV_FS_UTIME);
  ASSERT_OK(strcmp(fs_req.path, watched_dir));
  uv_fs_req_cleanup(&fs_req);

  helper_timer_cb_calls++;
}
#endif


TEST_IMPL(queue_foreach_delete) {
  uv_loop_t* loop;
  int r;

  loop = uv_default_loop();

  INIT_AND_START(idle,    loop);
  INIT_AND_START(prepare, loop);
  INIT_AND_START(check,   loop);

#ifdef __linux__
  init_and_start_fs_events(loop);

  /* helper timer to trigger async and fs_event callbacks */
  r = uv_timer_init(loop, &timer);
  ASSERT_OK(r);

  r = uv_timer_start(&timer, helper_timer_cb, 0, 0);
  ASSERT_OK(r);
#endif

  r = uv_run(loop, UV_RUN_NOWAIT);
  ASSERT_EQ(1, r);

  END_ASSERTS(idle);
  END_ASSERTS(prepare);
  END_ASSERTS(check);

#ifdef __linux__
  ASSERT_EQ(1, helper_timer_cb_calls);
#endif

  MAKE_VALGRIND_HAPPY(loop);

  return 0;
}
