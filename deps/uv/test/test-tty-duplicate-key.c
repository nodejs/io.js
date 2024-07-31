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

#include <errno.h>
#include <io.h>
#include <string.h>
#include <windows.h>

#define ESC "\x1b"
#define EUR_UTF8 "\xe2\x82\xac"
#define EUR_UNICODE 0x20AC


const char* expect_str = NULL;
ssize_t expect_nread = 0;

static void dump_str(const char* str, ssize_t len) {
  ssize_t i;
  for (i = 0; i < len; i++) {
    fprintf(stderr, "%#02x ", *(str + i));
  }
}

static void print_err_msg(const char* expect, ssize_t expect_len,
                          const char* found, ssize_t found_len) {
  fprintf(stderr, "expect ");
  dump_str(expect, expect_len);
  fprintf(stderr, ", but found ");
  dump_str(found, found_len);
  fprintf(stderr, "\n");
}

static void tty_alloc(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
  buf->base = malloc(size);
  ASSERT_NOT_NULL(buf->base);
  buf->len = size;
}

static void tty_read(uv_stream_t* tty_in, ssize_t nread, const uv_buf_t* buf) {
  if (nread > 0) {
    if (nread != expect_nread) {
      fprintf(stderr, "expected nread %ld, but found %ld\n",
              (long)expect_nread, (long)nread);
      print_err_msg(expect_str, expect_nread, buf->base, nread);
      ASSERT(FALSE);
    }
    if (strncmp(buf->base, expect_str, nread) != 0) {
      print_err_msg(expect_str, expect_nread, buf->base, nread);
      ASSERT(FALSE);
    }
    uv_close((uv_handle_t*) tty_in, NULL);
  } else {
    ASSERT_OK(nread);
  }
}

static void make_key_event_records(WORD virt_key, DWORD ctr_key_state,
                                   BOOL is_wsl, INPUT_RECORD* records) {
# define KEV(I) records[(I)].Event.KeyEvent
  BYTE kb_state[256] = {0};
  WCHAR buf[2];
  int ret;

  records[0].EventType = records[1].EventType = KEY_EVENT;
  KEV(0).bKeyDown = TRUE;
  KEV(1).bKeyDown = FALSE;
  KEV(0).wVirtualKeyCode = KEV(1).wVirtualKeyCode = virt_key;
  KEV(0).wRepeatCount = KEV(1).wRepeatCount = 1;
  KEV(0).wVirtualScanCode = KEV(1).wVirtualScanCode =
    MapVirtualKeyW(virt_key, MAPVK_VK_TO_VSC);
  KEV(0).dwControlKeyState = KEV(1).dwControlKeyState = ctr_key_state;
  if (ctr_key_state & LEFT_ALT_PRESSED) {
    kb_state[VK_LMENU] = 0x01;
  }
  if (ctr_key_state & RIGHT_ALT_PRESSED) {
    kb_state[VK_RMENU] = 0x01;
  }
  if (ctr_key_state & LEFT_CTRL_PRESSED) {
    kb_state[VK_LCONTROL] = 0x01;
  }
  if (ctr_key_state & RIGHT_CTRL_PRESSED) {
    kb_state[VK_RCONTROL] = 0x01;
  }
  if (ctr_key_state & SHIFT_PRESSED) {
    kb_state[VK_SHIFT] = 0x01;
  }
  ret = ToUnicode(virt_key, KEV(0).wVirtualScanCode, kb_state, buf, 2, 0);
  if (ret == 1) {
    if(!is_wsl &&
        ((ctr_key_state & LEFT_ALT_PRESSED) ||
         (ctr_key_state & RIGHT_ALT_PRESSED))) {
      /*
       * If ALT key is pressed, the UnicodeChar value of the keyup event is
       * set to 0 on nomal console. Emulate this behavior.
       * See https://github.com/Microsoft/console/issues/320
       */
      KEV(0).uChar.UnicodeChar = buf[0];
      KEV(1).uChar.UnicodeChar = 0;
    } else{
      /*
       * In WSL UnicodeChar is normally set. This behavior cause #2111.
       */
      KEV(0).uChar.UnicodeChar = KEV(1).uChar.UnicodeChar = buf[0];
    }
  } else {
    KEV(0).uChar.UnicodeChar = KEV(1).uChar.UnicodeChar = 0;
  }
# undef KEV
}

TEST_IMPL(tty_duplicate_vt100_fn_key) {
  int r;
  int ttyin_fd;
  uv_tty_t tty_in;
  uv_loop_t* loop;
  HANDLE handle;
  INPUT_RECORD records[2];
  DWORD written;

  loop = uv_default_loop();

  /* Make sure we have an FD that refers to a tty */
  handle = CreateFileA("conin$",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
  ASSERT_PTR_NE(handle, INVALID_HANDLE_VALUE);
  ttyin_fd = _open_osfhandle((intptr_t) handle, 0);
  ASSERT_GE(ttyin_fd, 0);
  ASSERT_EQ(UV_TTY, uv_guess_handle(ttyin_fd));

  r = uv_tty_init(uv_default_loop(), &tty_in, ttyin_fd, 1);  /* Readable. */
  ASSERT_OK(r);
  ASSERT(uv_is_readable((uv_stream_t*) &tty_in));
  ASSERT(!uv_is_writable((uv_stream_t*) &tty_in));

  r = uv_read_start((uv_stream_t*)&tty_in, tty_alloc, tty_read);
  ASSERT_OK(r);

  expect_str = ESC"[[A";
  expect_nread = strlen(expect_str);

  /* Turn on raw mode. */
  r = uv_tty_set_mode(&tty_in, UV_TTY_MODE_RAW);
  ASSERT_OK(r);

  /*
   * Send F1 keystrokes. Test of issue cause by #2114 that vt100 fn key
   * duplicate.
   */
  make_key_event_records(VK_F1, 0, TRUE, records);
  WriteConsoleInputW(handle, records, ARRAY_SIZE(records), &written);
  ASSERT_EQ(written, ARRAY_SIZE(records));

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

TEST_IMPL(tty_duplicate_alt_modifier_key) {
  int r;
  int ttyin_fd;
  uv_tty_t tty_in;
  uv_loop_t* loop;
  HANDLE handle;
  INPUT_RECORD records[2];
  INPUT_RECORD alt_records[2];
  DWORD written;

  loop = uv_default_loop();

  /* Make sure we have an FD that refers to a tty */
  handle = CreateFileA("conin$",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
  ASSERT_PTR_NE(handle, INVALID_HANDLE_VALUE);
  ttyin_fd = _open_osfhandle((intptr_t) handle, 0);
  ASSERT_GE(ttyin_fd, 0);
  ASSERT_EQ(UV_TTY, uv_guess_handle(ttyin_fd));

  r = uv_tty_init(uv_default_loop(), &tty_in, ttyin_fd, 1);  /* Readable. */
  ASSERT_OK(r);
  ASSERT(uv_is_readable((uv_stream_t*) &tty_in));
  ASSERT(!uv_is_writable((uv_stream_t*) &tty_in));

  r = uv_read_start((uv_stream_t*)&tty_in, tty_alloc, tty_read);
  ASSERT_OK(r);

  expect_str = ESC"a"ESC"a";
  expect_nread = strlen(expect_str);

  /* Turn on raw mode. */
  r = uv_tty_set_mode(&tty_in, UV_TTY_MODE_RAW);
  ASSERT_OK(r);

  /* Emulate transmission of M-a at normal console */
  make_key_event_records(VK_MENU, 0, TRUE, alt_records);
  WriteConsoleInputW(handle, &alt_records[0], 1, &written);
  ASSERT_EQ(1, written);
  make_key_event_records(L'A', LEFT_ALT_PRESSED, FALSE, records);
  WriteConsoleInputW(handle, records, ARRAY_SIZE(records), &written);
  ASSERT_EQ(2, written);
  WriteConsoleInputW(handle, &alt_records[1], 1, &written);
  ASSERT_EQ(1, written);

  /* Emulate transmission of M-a at WSL(#2111) */
  make_key_event_records(VK_MENU, 0, TRUE, alt_records);
  WriteConsoleInputW(handle, &alt_records[0], 1, &written);
  ASSERT_EQ(1, written);
  make_key_event_records(L'A', LEFT_ALT_PRESSED, TRUE, records);
  WriteConsoleInputW(handle, records, ARRAY_SIZE(records), &written);
  ASSERT_EQ(2, written);
  WriteConsoleInputW(handle, &alt_records[1], 1, &written);
  ASSERT_EQ(1, written);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

TEST_IMPL(tty_composing_character) {
  int r;
  int ttyin_fd;
  uv_tty_t tty_in;
  uv_loop_t* loop;
  HANDLE handle;
  INPUT_RECORD records[2];
  INPUT_RECORD alt_records[2];
  DWORD written;

  loop = uv_default_loop();

  /* Make sure we have an FD that refers to a tty */
  handle = CreateFileA("conin$",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
  ASSERT_PTR_NE(handle, INVALID_HANDLE_VALUE);
  ttyin_fd = _open_osfhandle((intptr_t) handle, 0);
  ASSERT_GE(ttyin_fd, 0);
  ASSERT_EQ(UV_TTY, uv_guess_handle(ttyin_fd));

  r = uv_tty_init(uv_default_loop(), &tty_in, ttyin_fd, 1);  /* Readable. */
  ASSERT_OK(r);
  ASSERT(uv_is_readable((uv_stream_t*) &tty_in));
  ASSERT(!uv_is_writable((uv_stream_t*) &tty_in));

  r = uv_read_start((uv_stream_t*)&tty_in, tty_alloc, tty_read);
  ASSERT_OK(r);

  expect_str = EUR_UTF8;
  expect_nread = strlen(expect_str);

  /* Turn on raw mode. */
  r = uv_tty_set_mode(&tty_in, UV_TTY_MODE_RAW);
  ASSERT_OK(r);

  /* Emulate EUR inputs by LEFT ALT+NUMPAD ASCII KeyComos */
  make_key_event_records(VK_MENU, 0, FALSE, alt_records);
  alt_records[1].Event.KeyEvent.uChar.UnicodeChar = EUR_UNICODE;
  WriteConsoleInputW(handle, &alt_records[0], 1, &written);
  make_key_event_records(VK_NUMPAD0, LEFT_ALT_PRESSED, FALSE, records);
  WriteConsoleInputW(handle, records, ARRAY_SIZE(records), &written);
  ASSERT_EQ(written, ARRAY_SIZE(records));
  make_key_event_records(VK_NUMPAD1, LEFT_ALT_PRESSED, FALSE, records);
  WriteConsoleInputW(handle, records, ARRAY_SIZE(records), &written);
  ASSERT_EQ(written, ARRAY_SIZE(records));
  make_key_event_records(VK_NUMPAD2, LEFT_ALT_PRESSED, FALSE, records);
  WriteConsoleInputW(handle, records, ARRAY_SIZE(records), &written);
  ASSERT_EQ(written, ARRAY_SIZE(records));
  make_key_event_records(VK_NUMPAD8, LEFT_ALT_PRESSED, FALSE, records);
  WriteConsoleInputW(handle, records, ARRAY_SIZE(records), &written);
  ASSERT_EQ(written, ARRAY_SIZE(records));
  WriteConsoleInputW(handle, &alt_records[1], 1, &written);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}

#else

typedef int file_has_no_tests;  /* ISO C forbids an empty translation unit. */

#endif  /* ifndef _WIN32 */
