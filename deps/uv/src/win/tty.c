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

#include <assert.h>
#include <io.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef COMMON_LVB_REVERSE_VIDEO
# define COMMON_LVB_REVERSE_VIDEO 0x4000
#endif

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "stream-inl.h"
#include "req-inl.h"

#ifndef InterlockedOr
# define InterlockedOr _InterlockedOr
#endif

#define UNICODE_REPLACEMENT_CHARACTER (0xfffd)

#define ANSI_NORMAL           0x0000
#define ANSI_ESCAPE_SEEN      0x0002
#define ANSI_CSI              0x0004
#define ANSI_ST_CONTROL       0x0008
#define ANSI_IGNORE           0x0010
#define ANSI_IN_ARG           0x0020
#define ANSI_IN_STRING        0x0040
#define ANSI_BACKSLASH_SEEN   0x0080
#define ANSI_EXTENSION        0x0100
#define ANSI_DECSCUSR         0x0200

#define MAX_INPUT_BUFFER_LENGTH 8192
#define MAX_CONSOLE_CHAR 8192

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#define CURSOR_SIZE_SMALL     25
#define CURSOR_SIZE_LARGE     100

static void uv__tty_capture_initial_style(
    CONSOLE_SCREEN_BUFFER_INFO* screen_buffer_info,
    CONSOLE_CURSOR_INFO* cursor_info);
static void uv__tty_update_virtual_window(CONSOLE_SCREEN_BUFFER_INFO* info);
static int uv__cancel_read_console(uv_tty_t* handle);


/* Null uv_buf_t */
static const uv_buf_t uv_null_buf_ = { 0, NULL };

enum uv__read_console_status_e {
  NOT_STARTED,
  IN_PROGRESS,
  TRAP_REQUESTED,
  COMPLETED
};

static volatile LONG uv__read_console_status = NOT_STARTED;
static volatile LONG uv__restore_screen_state;
static CONSOLE_SCREEN_BUFFER_INFO uv__saved_screen_state;


/*
 * The console virtual window.
 *
 * Normally cursor movement in windows is relative to the console screen buffer,
 * e.g. the application is allowed to overwrite the 'history'. This is very
 * inconvenient, it makes absolute cursor movement pretty useless. There is
 * also the concept of 'client rect' which is defined by the actual size of
 * the console window and the scroll position of the screen buffer, but it's
 * very volatile because it changes when the user scrolls.
 *
 * To make cursor movement behave sensibly we define a virtual window to which
 * cursor movement is confined. The virtual window is always as wide as the
 * console screen buffer, but it's height is defined by the size of the
 * console window. The top of the virtual window aligns with the position
 * of the caret when the first stdout/err handle is created, unless that would
 * mean that it would extend beyond the bottom of the screen buffer -  in that
 * that case it's located as far down as possible.
 *
 * When the user writes a long text or many newlines, such that the output
 * reaches beyond the bottom of the virtual window, the virtual window is
 * shifted downwards, but not resized.
 *
 * Since all tty i/o happens on the same console, this window is shared
 * between all stdout/stderr handles.
 */

static int uv_tty_virtual_offset = -1;
static int uv_tty_virtual_height = -1;
static int uv_tty_virtual_width = -1;

/* The console window size
 * We keep this separate from uv_tty_virtual_*. We use those values to only
 * handle signalling SIGWINCH
 */

static HANDLE uv__tty_console_handle = INVALID_HANDLE_VALUE;
static int uv__tty_console_height = -1;
static int uv__tty_console_width = -1;
static HANDLE uv__tty_console_resized = INVALID_HANDLE_VALUE;
static uv_mutex_t uv__tty_console_resize_mutex;

static DWORD WINAPI uv__tty_console_resize_message_loop_thread(void* param);
static void CALLBACK uv__tty_console_resize_event(HWINEVENTHOOK hWinEventHook,
                                                  DWORD event,
                                                  HWND hwnd,
                                                  LONG idObject,
                                                  LONG idChild,
                                                  DWORD dwEventThread,
                                                  DWORD dwmsEventTime);
static DWORD WINAPI uv__tty_console_resize_watcher_thread(void* param);
static void uv__tty_console_signal_resize(void);

/* We use a semaphore rather than a mutex or critical section because in some
   cases (uv__cancel_read_console) we need take the lock in the main thread and
   release it in another thread. Using a semaphore ensures that in such
   scenario the main thread will still block when trying to acquire the lock. */
static uv_sem_t uv_tty_output_lock;

static WORD uv_tty_default_text_attributes =
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

static char uv_tty_default_fg_color = 7;
static char uv_tty_default_bg_color = 0;
static char uv_tty_default_fg_bright = 0;
static char uv_tty_default_bg_bright = 0;
static char uv_tty_default_inverse = 0;

static CONSOLE_CURSOR_INFO uv_tty_default_cursor_info;

/* Determine whether or not ANSI support is enabled. */
static BOOL uv__need_check_vterm_state = TRUE;
static uv_tty_vtermstate_t uv__vterm_state = UV_TTY_UNSUPPORTED;
static void uv__determine_vterm_state(HANDLE handle);

void uv__console_init(void) {
  if (uv_sem_init(&uv_tty_output_lock, 1))
    abort();
  uv__tty_console_handle = CreateFileW(L"CONOUT$",
                                       GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_WRITE,
                                       0,
                                       OPEN_EXISTING,
                                       0,
                                       0);
  if (uv__tty_console_handle != INVALID_HANDLE_VALUE) {
    CONSOLE_SCREEN_BUFFER_INFO sb_info;
    uv_mutex_init(&uv__tty_console_resize_mutex);
    if (GetConsoleScreenBufferInfo(uv__tty_console_handle, &sb_info)) {
      uv__tty_console_width = sb_info.dwSize.X;
      uv__tty_console_height = sb_info.srWindow.Bottom - sb_info.srWindow.Top + 1;
    }
    QueueUserWorkItem(uv__tty_console_resize_message_loop_thread,
                      NULL,
                      WT_EXECUTELONGFUNCTION);
  }
}


int uv_tty_init(uv_loop_t* loop, uv_tty_t* tty, uv_file fd, int unused) {
  BOOL readable;
  DWORD NumberOfEvents;
  HANDLE handle;
  CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;
  CONSOLE_CURSOR_INFO cursor_info;
  (void)unused;

  uv__once_init();
  handle = (HANDLE) uv__get_osfhandle(fd);
  if (handle == INVALID_HANDLE_VALUE)
    return UV_EBADF;

  if (fd <= 2) {
    /* In order to avoid closing a stdio file descriptor 0-2, duplicate the
     * underlying OS handle and forget about the original fd.
     * We could also opt to use the original OS handle and just never close it,
     * but then there would be no reliable way to cancel pending read operations
     * upon close.
     */
    if (!DuplicateHandle(INVALID_HANDLE_VALUE,
                         handle,
                         INVALID_HANDLE_VALUE,
                         &handle,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
      return uv_translate_sys_error(GetLastError());
    fd = -1;
  }

  readable = GetNumberOfConsoleInputEvents(handle, &NumberOfEvents);
  if (!readable) {
    /* Obtain the screen buffer info with the output handle. */
    if (!GetConsoleScreenBufferInfo(handle, &screen_buffer_info)) {
      return uv_translate_sys_error(GetLastError());
    }

    /* Obtain the cursor info with the output handle. */
    if (!GetConsoleCursorInfo(handle, &cursor_info)) {
      return uv_translate_sys_error(GetLastError());
    }

    /* Obtain the tty_output_lock because the virtual window state is shared
     * between all uv_tty_t handles. */
    uv_sem_wait(&uv_tty_output_lock);

    if (uv__need_check_vterm_state)
      uv__determine_vterm_state(handle);

    /* Remember the original console text attributes and cursor info. */
    uv__tty_capture_initial_style(&screen_buffer_info, &cursor_info);

    uv__tty_update_virtual_window(&screen_buffer_info);

    uv_sem_post(&uv_tty_output_lock);
  }


  uv__stream_init(loop, (uv_stream_t*) tty, UV_TTY);
  uv__connection_init((uv_stream_t*) tty);

  tty->handle = handle;
  tty->u.fd = fd;
  tty->reqs_pending = 0;
  tty->flags |= UV_HANDLE_BOUND;

  if (readable) {
    /* Initialize TTY input specific fields. */
    tty->flags |= UV_HANDLE_TTY_READABLE | UV_HANDLE_READABLE;
    /* TODO: remove me in v2.x. */
    tty->tty.rd.unused_ = NULL;
    tty->tty.rd.read_line_buffer = uv_null_buf_;
    tty->tty.rd.read_raw_wait = NULL;

    /* Init keycode-to-vt100 mapper state. */
    tty->tty.rd.last_key_len = 0;
    tty->tty.rd.last_key_offset = 0;
    tty->tty.rd.last_utf16_high_surrogate = 0;
    memset(&tty->tty.rd.last_input_record, 0, sizeof tty->tty.rd.last_input_record);
  } else {
    /* TTY output specific fields. */
    tty->flags |= UV_HANDLE_WRITABLE;

    /* Init utf8-to-utf16 conversion state. */
    tty->tty.wr.utf8_bytes_left = 0;
    tty->tty.wr.utf8_codepoint = 0;

    /* Initialize eol conversion state */
    tty->tty.wr.previous_eol = 0;

    /* Init ANSI parser state. */
    tty->tty.wr.ansi_parser_state = ANSI_NORMAL;
  }

  return 0;
}


/* Set the default console text attributes based on how the console was
 * configured when libuv started.
 */
static void uv__tty_capture_initial_style(
    CONSOLE_SCREEN_BUFFER_INFO* screen_buffer_info,
    CONSOLE_CURSOR_INFO* cursor_info) {
  static int style_captured = 0;

  /* Only do this once.
     Assumption: Caller has acquired uv_tty_output_lock. */
  if (style_captured)
    return;

  /* Save raw win32 attributes. */
  uv_tty_default_text_attributes = screen_buffer_info->wAttributes;

  /* Convert black text on black background to use white text. */
  if (uv_tty_default_text_attributes == 0)
    uv_tty_default_text_attributes = 7;

  /* Convert Win32 attributes to ANSI colors. */
  uv_tty_default_fg_color = 0;
  uv_tty_default_bg_color = 0;
  uv_tty_default_fg_bright = 0;
  uv_tty_default_bg_bright = 0;
  uv_tty_default_inverse = 0;

  if (uv_tty_default_text_attributes & FOREGROUND_RED)
    uv_tty_default_fg_color |= 1;

  if (uv_tty_default_text_attributes & FOREGROUND_GREEN)
    uv_tty_default_fg_color |= 2;

  if (uv_tty_default_text_attributes & FOREGROUND_BLUE)
    uv_tty_default_fg_color |= 4;

  if (uv_tty_default_text_attributes & BACKGROUND_RED)
    uv_tty_default_bg_color |= 1;

  if (uv_tty_default_text_attributes & BACKGROUND_GREEN)
    uv_tty_default_bg_color |= 2;

  if (uv_tty_default_text_attributes & BACKGROUND_BLUE)
    uv_tty_default_bg_color |= 4;

  if (uv_tty_default_text_attributes & FOREGROUND_INTENSITY)
    uv_tty_default_fg_bright = 1;

  if (uv_tty_default_text_attributes & BACKGROUND_INTENSITY)
    uv_tty_default_bg_bright = 1;

  if (uv_tty_default_text_attributes & COMMON_LVB_REVERSE_VIDEO)
    uv_tty_default_inverse = 1;

  /* Save the cursor size and the cursor state. */
  uv_tty_default_cursor_info = *cursor_info;

  style_captured = 1;
}


int uv_tty_set_mode(uv_tty_t* tty, uv_tty_mode_t mode) {
  DWORD flags;
  unsigned char was_reading;
  uv_alloc_cb alloc_cb;
  uv_read_cb read_cb;
  int err;

  if (!(tty->flags & UV_HANDLE_TTY_READABLE)) {
    return UV_EINVAL;
  }

  if (!!mode == !!(tty->flags & UV_HANDLE_TTY_RAW)) {
    return 0;
  }

  switch (mode) {
    case UV_TTY_MODE_NORMAL:
      flags = ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
      break;
    case UV_TTY_MODE_RAW:
      flags = ENABLE_WINDOW_INPUT;
      break;
    case UV_TTY_MODE_IO:
      return UV_ENOTSUP;
    default:
      return UV_EINVAL;
  }

  /* If currently reading, stop, and restart reading. */
  if (tty->flags & UV_HANDLE_READING) {
    was_reading = 1;
    alloc_cb = tty->alloc_cb;
    read_cb = tty->read_cb;
    err = uv__tty_read_stop(tty);
    if (err) {
      return uv_translate_sys_error(err);
    }
  } else {
    was_reading = 0;
    alloc_cb = NULL;
    read_cb = NULL;
  }

  uv_sem_wait(&uv_tty_output_lock);
  if (!SetConsoleMode(tty->handle, flags)) {
    err = uv_translate_sys_error(GetLastError());
    uv_sem_post(&uv_tty_output_lock);
    return err;
  }
  uv_sem_post(&uv_tty_output_lock);

  /* Update flag. */
  tty->flags &= ~UV_HANDLE_TTY_RAW;
  tty->flags |= mode ? UV_HANDLE_TTY_RAW : 0;

  /* If we just stopped reading, restart. */
  if (was_reading) {
    err = uv__tty_read_start(tty, alloc_cb, read_cb);
    if (err) {
      return uv_translate_sys_error(err);
    }
  }

  return 0;
}


int uv_tty_get_winsize(uv_tty_t* tty, int* width, int* height) {
  CONSOLE_SCREEN_BUFFER_INFO info;

  if (!GetConsoleScreenBufferInfo(tty->handle, &info)) {
    return uv_translate_sys_error(GetLastError());
  }

  uv_sem_wait(&uv_tty_output_lock);
  uv__tty_update_virtual_window(&info);
  uv_sem_post(&uv_tty_output_lock);

  *width = uv_tty_virtual_width;
  *height = uv_tty_virtual_height;

  return 0;
}


static void CALLBACK uv_tty_post_raw_read(void* data, BOOLEAN didTimeout) {
  uv_loop_t* loop;
  uv_tty_t* handle;
  uv_req_t* req;

  assert(data);
  assert(!didTimeout);

  req = (uv_req_t*) data;
  handle = (uv_tty_t*) req->data;
  loop = handle->loop;

  UnregisterWait(handle->tty.rd.read_raw_wait);
  handle->tty.rd.read_raw_wait = NULL;

  SET_REQ_SUCCESS(req);
  POST_COMPLETION_FOR_REQ(loop, req);
}


static void uv__tty_queue_read_raw(uv_loop_t* loop, uv_tty_t* handle) {
  uv_read_t* req;
  BOOL r;

  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));

  assert(handle->handle && handle->handle != INVALID_HANDLE_VALUE);

  handle->tty.rd.read_line_buffer = uv_null_buf_;

  req = &handle->read_req;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  r = RegisterWaitForSingleObject(&handle->tty.rd.read_raw_wait,
                                  handle->handle,
                                  uv_tty_post_raw_read,
                                  (void*) req,
                                  INFINITE,
                                  WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE);
  if (!r) {
    handle->tty.rd.read_raw_wait = NULL;
    SET_REQ_ERROR(req, GetLastError());
    uv__insert_pending_req(loop, (uv_req_t*)req);
  }

  handle->flags |= UV_HANDLE_READ_PENDING;
  handle->reqs_pending++;
}


static DWORD CALLBACK uv_tty_line_read_thread(void* data) {
  uv_loop_t* loop;
  uv_tty_t* handle;
  uv_req_t* req;
  DWORD bytes;
  size_t read_bytes;
  WCHAR utf16[MAX_INPUT_BUFFER_LENGTH / 3];
  DWORD chars;
  DWORD read_chars;
  LONG status;
  COORD pos;
  BOOL read_console_success;

  assert(data);

  req = (uv_req_t*) data;
  handle = (uv_tty_t*) req->data;
  loop = handle->loop;

  assert(handle->tty.rd.read_line_buffer.base != NULL);
  assert(handle->tty.rd.read_line_buffer.len > 0);

  /* ReadConsole can't handle big buffers. */
  if (handle->tty.rd.read_line_buffer.len < MAX_INPUT_BUFFER_LENGTH) {
    bytes = handle->tty.rd.read_line_buffer.len;
  } else {
    bytes = MAX_INPUT_BUFFER_LENGTH;
  }

  /* At last, unicode! One utf-16 codeunit never takes more than 3 utf-8
   * codeunits to encode. */
  chars = bytes / 3;

  status = InterlockedExchange(&uv__read_console_status, IN_PROGRESS);
  if (status == TRAP_REQUESTED) {
    SET_REQ_SUCCESS(req);
    InterlockedExchange(&uv__read_console_status, COMPLETED);
    req->u.io.overlapped.InternalHigh = 0;
    POST_COMPLETION_FOR_REQ(loop, req);
    return 0;
  }

  read_console_success = ReadConsoleW(handle->handle,
                                      (void*) utf16,
                                      chars,
                                      &read_chars,
                                      NULL);

  if (read_console_success) {
    read_bytes = bytes;
    uv_utf16_to_wtf8(utf16,
                     read_chars,
                     &handle->tty.rd.read_line_buffer.base,
                     &read_bytes);
    SET_REQ_SUCCESS(req);
    req->u.io.overlapped.InternalHigh = (DWORD) read_bytes;
  } else {
    SET_REQ_ERROR(req, GetLastError());
  }

  status = InterlockedExchange(&uv__read_console_status, COMPLETED);

  if (status ==  TRAP_REQUESTED) {
    /* If we canceled the read by sending a VK_RETURN event, restore the
       screen state to undo the visual effect of the VK_RETURN */
    if (read_console_success && InterlockedOr(&uv__restore_screen_state, 0)) {
      HANDLE active_screen_buffer;
      active_screen_buffer = CreateFileA("conout$",
                                         GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL,
                                         OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL,
                                         NULL);
      if (active_screen_buffer != INVALID_HANDLE_VALUE) {
        pos = uv__saved_screen_state.dwCursorPosition;

        /* If the cursor was at the bottom line of the screen buffer, the
           VK_RETURN would have caused the buffer contents to scroll up by one
           line. The right position to reset the cursor to is therefore one line
           higher */
        if (pos.Y == uv__saved_screen_state.dwSize.Y - 1)
          pos.Y--;

        SetConsoleCursorPosition(active_screen_buffer, pos);
        CloseHandle(active_screen_buffer);
      }
    }
    uv_sem_post(&uv_tty_output_lock);
  }
  POST_COMPLETION_FOR_REQ(loop, req);
  return 0;
}


static void uv__tty_queue_read_line(uv_loop_t* loop, uv_tty_t* handle) {
  uv_read_t* req;
  BOOL r;

  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));
  assert(handle->handle && handle->handle != INVALID_HANDLE_VALUE);

  req = &handle->read_req;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  handle->tty.rd.read_line_buffer = uv_buf_init(NULL, 0);
  handle->alloc_cb((uv_handle_t*) handle, 8192, &handle->tty.rd.read_line_buffer);
  if (handle->tty.rd.read_line_buffer.base == NULL ||
      handle->tty.rd.read_line_buffer.len == 0) {
    handle->read_cb((uv_stream_t*) handle,
                    UV_ENOBUFS,
                    &handle->tty.rd.read_line_buffer);
    return;
  }
  assert(handle->tty.rd.read_line_buffer.base != NULL);

  /* Reset flags  No locking is required since there cannot be a line read
     in progress. We are also relying on the memory barrier provided by
     QueueUserWorkItem*/
  uv__restore_screen_state = FALSE;
  uv__read_console_status = NOT_STARTED;
  r = QueueUserWorkItem(uv_tty_line_read_thread,
                        (void*) req,
                        WT_EXECUTELONGFUNCTION);
  if (!r) {
    SET_REQ_ERROR(req, GetLastError());
    uv__insert_pending_req(loop, (uv_req_t*)req);
  }

  handle->flags |= UV_HANDLE_READ_PENDING;
  handle->reqs_pending++;
}


static void uv__tty_queue_read(uv_loop_t* loop, uv_tty_t* handle) {
  if (handle->flags & UV_HANDLE_TTY_RAW) {
    uv__tty_queue_read_raw(loop, handle);
  } else {
    uv__tty_queue_read_line(loop, handle);
  }
}


static const char* get_vt100_fn_key(DWORD code, char shift, char ctrl,
    size_t* len) {
#define VK_CASE(vk, normal_str, shift_str, ctrl_str, shift_ctrl_str)          \
    case (vk):                                                                \
      if (shift && ctrl) {                                                    \
        *len = sizeof shift_ctrl_str;                                         \
        return "\033" shift_ctrl_str;                                         \
      } else if (shift) {                                                     \
        *len = sizeof shift_str ;                                             \
        return "\033" shift_str;                                              \
      } else if (ctrl) {                                                      \
        *len = sizeof ctrl_str;                                               \
        return "\033" ctrl_str;                                               \
      } else {                                                                \
        *len = sizeof normal_str;                                             \
        return "\033" normal_str;                                             \
      }

  switch (code) {
    /* These mappings are the same as Cygwin's. Unmodified and alt-modified
     * keypad keys comply with linux console, modifiers comply with xterm
     * modifier usage. F1. f12 and shift-f1. f10 comply with linux console, f6.
     * f12 with and without modifiers comply with rxvt. */
    VK_CASE(VK_INSERT,  "[2~",  "[2;2~", "[2;5~", "[2;6~")
    VK_CASE(VK_END,     "[4~",  "[4;2~", "[4;5~", "[4;6~")
    VK_CASE(VK_DOWN,    "[B",   "[1;2B", "[1;5B", "[1;6B")
    VK_CASE(VK_NEXT,    "[6~",  "[6;2~", "[6;5~", "[6;6~")
    VK_CASE(VK_LEFT,    "[D",   "[1;2D", "[1;5D", "[1;6D")
    VK_CASE(VK_CLEAR,   "[G",   "[1;2G", "[1;5G", "[1;6G")
    VK_CASE(VK_RIGHT,   "[C",   "[1;2C", "[1;5C", "[1;6C")
    VK_CASE(VK_UP,      "[A",   "[1;2A", "[1;5A", "[1;6A")
    VK_CASE(VK_HOME,    "[1~",  "[1;2~", "[1;5~", "[1;6~")
    VK_CASE(VK_PRIOR,   "[5~",  "[5;2~", "[5;5~", "[5;6~")
    VK_CASE(VK_DELETE,  "[3~",  "[3;2~", "[3;5~", "[3;6~")
    VK_CASE(VK_NUMPAD0, "[2~",  "[2;2~", "[2;5~", "[2;6~")
    VK_CASE(VK_NUMPAD1, "[4~",  "[4;2~", "[4;5~", "[4;6~")
    VK_CASE(VK_NUMPAD2, "[B",   "[1;2B", "[1;5B", "[1;6B")
    VK_CASE(VK_NUMPAD3, "[6~",  "[6;2~", "[6;5~", "[6;6~")
    VK_CASE(VK_NUMPAD4, "[D",   "[1;2D", "[1;5D", "[1;6D")
    VK_CASE(VK_NUMPAD5, "[G",   "[1;2G", "[1;5G", "[1;6G")
    VK_CASE(VK_NUMPAD6, "[C",   "[1;2C", "[1;5C", "[1;6C")
    VK_CASE(VK_NUMPAD7, "[A",   "[1;2A", "[1;5A", "[1;6A")
    VK_CASE(VK_NUMPAD8, "[1~",  "[1;2~", "[1;5~", "[1;6~")
    VK_CASE(VK_NUMPAD9, "[5~",  "[5;2~", "[5;5~", "[5;6~")
    VK_CASE(VK_DECIMAL, "[3~",  "[3;2~", "[3;5~", "[3;6~")
    VK_CASE(VK_F1,      "[[A",  "[23~",  "[11^",  "[23^" )
    VK_CASE(VK_F2,      "[[B",  "[24~",  "[12^",  "[24^" )
    VK_CASE(VK_F3,      "[[C",  "[25~",  "[13^",  "[25^" )
    VK_CASE(VK_F4,      "[[D",  "[26~",  "[14^",  "[26^" )
    VK_CASE(VK_F5,      "[[E",  "[28~",  "[15^",  "[28^" )
    VK_CASE(VK_F6,      "[17~", "[29~",  "[17^",  "[29^" )
    VK_CASE(VK_F7,      "[18~", "[31~",  "[18^",  "[31^" )
    VK_CASE(VK_F8,      "[19~", "[32~",  "[19^",  "[32^" )
    VK_CASE(VK_F9,      "[20~", "[33~",  "[20^",  "[33^" )
    VK_CASE(VK_F10,     "[21~", "[34~",  "[21^",  "[34^" )
    VK_CASE(VK_F11,     "[23~", "[23$",  "[23^",  "[23@" )
    VK_CASE(VK_F12,     "[24~", "[24$",  "[24^",  "[24@" )

    default:
      *len = 0;
      return NULL;
  }
#undef VK_CASE
}


void uv_process_tty_read_raw_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* req) {
  /* Shortcut for handle->tty.rd.last_input_record.Event.KeyEvent. */
#define KEV handle->tty.rd.last_input_record.Event.KeyEvent

  DWORD records_left, records_read;
  uv_buf_t buf;
  _off_t buf_used;

  assert(handle->type == UV_TTY);
  assert(handle->flags & UV_HANDLE_TTY_READABLE);
  handle->flags &= ~UV_HANDLE_READ_PENDING;

  if (!(handle->flags & UV_HANDLE_READING) ||
      !(handle->flags & UV_HANDLE_TTY_RAW)) {
    goto out;
  }

  if (!REQ_SUCCESS(req)) {
    /* An error occurred while waiting for the event. */
    if ((handle->flags & UV_HANDLE_READING)) {
      handle->flags &= ~UV_HANDLE_READING;
      handle->read_cb((uv_stream_t*)handle,
                      uv_translate_sys_error(GET_REQ_ERROR(req)),
                      &uv_null_buf_);
    }
    goto out;
  }

  /* Fetch the number of events  */
  if (!GetNumberOfConsoleInputEvents(handle->handle, &records_left)) {
    handle->flags &= ~UV_HANDLE_READING;
    DECREASE_ACTIVE_COUNT(loop, handle);
    handle->read_cb((uv_stream_t*)handle,
                    uv_translate_sys_error(GetLastError()),
                    &uv_null_buf_);
    goto out;
  }

  /* Windows sends a lot of events that we're not interested in, so buf will be
   * allocated on demand, when there's actually something to emit. */
  buf = uv_null_buf_;
  buf_used = 0;

  while ((records_left > 0 || handle->tty.rd.last_key_len > 0) &&
         (handle->flags & UV_HANDLE_READING)) {
    if (handle->tty.rd.last_key_len == 0) {
      /* Read the next input record */
      if (!ReadConsoleInputW(handle->handle,
                             &handle->tty.rd.last_input_record,
                             1,
                             &records_read)) {
        handle->flags &= ~UV_HANDLE_READING;
        DECREASE_ACTIVE_COUNT(loop, handle);
        handle->read_cb((uv_stream_t*) handle,
                        uv_translate_sys_error(GetLastError()),
                        &buf);
        goto out;
      }
      records_left--;

      /* We might be not subscribed to EVENT_CONSOLE_LAYOUT or we might be
       * running under some TTY emulator that does not send those events. */
      if (handle->tty.rd.last_input_record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
        uv__tty_console_signal_resize();
      }

      /* Ignore other events that are not key events. */
      if (handle->tty.rd.last_input_record.EventType != KEY_EVENT) {
        continue;
      }

      /* Ignore keyup events, unless the left alt key was held and a valid
       * unicode character was emitted. */
      if (!KEV.bKeyDown &&
          (KEV.wVirtualKeyCode != VK_MENU ||
           KEV.uChar.UnicodeChar == 0)) {
        continue;
      }

      /* Ignore keypresses to numpad number keys if the left alt is held
       * because the user is composing a character, or windows simulating this.
       */
      if ((KEV.dwControlKeyState & LEFT_ALT_PRESSED) &&
          !(KEV.dwControlKeyState & ENHANCED_KEY) &&
          (KEV.wVirtualKeyCode == VK_INSERT ||
          KEV.wVirtualKeyCode == VK_END ||
          KEV.wVirtualKeyCode == VK_DOWN ||
          KEV.wVirtualKeyCode == VK_NEXT ||
          KEV.wVirtualKeyCode == VK_LEFT ||
          KEV.wVirtualKeyCode == VK_CLEAR ||
          KEV.wVirtualKeyCode == VK_RIGHT ||
          KEV.wVirtualKeyCode == VK_HOME ||
          KEV.wVirtualKeyCode == VK_UP ||
          KEV.wVirtualKeyCode == VK_PRIOR ||
          KEV.wVirtualKeyCode == VK_NUMPAD0 ||
          KEV.wVirtualKeyCode == VK_NUMPAD1 ||
          KEV.wVirtualKeyCode == VK_NUMPAD2 ||
          KEV.wVirtualKeyCode == VK_NUMPAD3 ||
          KEV.wVirtualKeyCode == VK_NUMPAD4 ||
          KEV.wVirtualKeyCode == VK_NUMPAD5 ||
          KEV.wVirtualKeyCode == VK_NUMPAD6 ||
          KEV.wVirtualKeyCode == VK_NUMPAD7 ||
          KEV.wVirtualKeyCode == VK_NUMPAD8 ||
          KEV.wVirtualKeyCode == VK_NUMPAD9)) {
        continue;
      }

      if (KEV.uChar.UnicodeChar != 0) {
        int prefix_len;
        size_t char_len;
        char* last_key_buf;

        /* Character key pressed */
        if (KEV.uChar.UnicodeChar >= 0xD800 &&
            KEV.uChar.UnicodeChar < 0xDC00) {
          /* UTF-16 high surrogate */
          handle->tty.rd.last_utf16_high_surrogate = KEV.uChar.UnicodeChar;
          continue;
        }

        /* Prefix with \u033 if alt was held, but alt was not used as part a
         * compose sequence. */
        if ((KEV.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
            && !(KEV.dwControlKeyState & (LEFT_CTRL_PRESSED |
            RIGHT_CTRL_PRESSED)) && KEV.bKeyDown) {
          handle->tty.rd.last_key[0] = '\033';
          prefix_len = 1;
        } else {
          prefix_len = 0;
        }

        char_len = sizeof handle->tty.rd.last_key;
        last_key_buf = &handle->tty.rd.last_key[prefix_len];
        if (handle->tty.rd.last_utf16_high_surrogate) {
          /* UTF-16 surrogate pair */
          WCHAR utf16_buffer[2];
          utf16_buffer[0] = handle->tty.rd.last_utf16_high_surrogate;
          utf16_buffer[1] = KEV.uChar.UnicodeChar;
          if (uv_utf16_to_wtf8(utf16_buffer,
                               2,
                               &last_key_buf,
                               &char_len))
            char_len = 0;
          handle->tty.rd.last_utf16_high_surrogate = 0;
        } else {
          /* Single UTF-16 character */
          if (uv_utf16_to_wtf8(&KEV.uChar.UnicodeChar,
                               1,
                               &last_key_buf,
                               &char_len))
            char_len = 0;
        }

        /* If the utf16 character(s) couldn't be converted something must be
         * wrong. */
        if (char_len == 0) {
          handle->flags &= ~UV_HANDLE_READING;
          DECREASE_ACTIVE_COUNT(loop, handle);
          handle->read_cb((uv_stream_t*) handle,
                          uv_translate_sys_error(GetLastError()),
                          &buf);
          goto out;
        }

        handle->tty.rd.last_key_len = (unsigned char) (prefix_len + char_len);
        handle->tty.rd.last_key_offset = 0;
        continue;

      } else {
        /* Function key pressed */
        const char* vt100;
        size_t prefix_len, vt100_len;

        vt100 = get_vt100_fn_key(KEV.wVirtualKeyCode,
                                  !!(KEV.dwControlKeyState & SHIFT_PRESSED),
                                  !!(KEV.dwControlKeyState & (
                                    LEFT_CTRL_PRESSED |
                                    RIGHT_CTRL_PRESSED)),
                                  &vt100_len);

        /* If we were unable to map to a vt100 sequence, just ignore. */
        if (!vt100) {
          continue;
        }

        /* Prefix with \x033 when the alt key was held. */
        if (KEV.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
          handle->tty.rd.last_key[0] = '\033';
          prefix_len = 1;
        } else {
          prefix_len = 0;
        }

        /* Copy the vt100 sequence to the handle buffer. */
        assert(prefix_len + vt100_len < sizeof handle->tty.rd.last_key);
        memcpy(&handle->tty.rd.last_key[prefix_len], vt100, vt100_len);

        handle->tty.rd.last_key_len = (unsigned char) (prefix_len + vt100_len);
        handle->tty.rd.last_key_offset = 0;
        continue;
      }
    } else {
      /* Copy any bytes left from the last keypress to the user buffer. */
      if (handle->tty.rd.last_key_offset < handle->tty.rd.last_key_len) {
        /* Allocate a buffer if needed */
        if (buf_used == 0) {
          buf = uv_buf_init(NULL, 0);
          handle->alloc_cb((uv_handle_t*) handle, 1024, &buf);
          if (buf.base == NULL || buf.len == 0) {
            handle->read_cb((uv_stream_t*) handle, UV_ENOBUFS, &buf);
            goto out;
          }
          assert(buf.base != NULL);
        }

        buf.base[buf_used++] = handle->tty.rd.last_key[handle->tty.rd.last_key_offset++];

        /* If the buffer is full, emit it */
        if ((size_t) buf_used == buf.len) {
          handle->read_cb((uv_stream_t*) handle, buf_used, &buf);
          buf = uv_null_buf_;
          buf_used = 0;
        }

        continue;
      }

      /* Apply dwRepeat from the last input record. */
      if (--KEV.wRepeatCount > 0) {
        handle->tty.rd.last_key_offset = 0;
        continue;
      }

      handle->tty.rd.last_key_len = 0;
      continue;
    }
  }

  /* Send the buffer back to the user */
  if (buf_used > 0) {
    handle->read_cb((uv_stream_t*) handle, buf_used, &buf);
  }

 out:
  /* Wait for more input events. */
  if ((handle->flags & UV_HANDLE_READING) &&
      !(handle->flags & UV_HANDLE_READ_PENDING)) {
    uv__tty_queue_read(loop, handle);
  }

  DECREASE_PENDING_REQ_COUNT(handle);

#undef KEV
}



void uv_process_tty_read_line_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* req) {
  uv_buf_t buf;

  assert(handle->type == UV_TTY);
  assert(handle->flags & UV_HANDLE_TTY_READABLE);

  buf = handle->tty.rd.read_line_buffer;

  handle->flags &= ~UV_HANDLE_READ_PENDING;
  handle->tty.rd.read_line_buffer = uv_null_buf_;

  if (!REQ_SUCCESS(req)) {
    /* Read was not successful */
    if (handle->flags & UV_HANDLE_READING) {
      /* Real error */
      handle->flags &= ~UV_HANDLE_READING;
      DECREASE_ACTIVE_COUNT(loop, handle);
      handle->read_cb((uv_stream_t*) handle,
                      uv_translate_sys_error(GET_REQ_ERROR(req)),
                      &buf);
    }
  } else {
    if (!(handle->flags & UV_HANDLE_CANCELLATION_PENDING) &&
        req->u.io.overlapped.InternalHigh != 0) {
      /* Read successful. TODO: read unicode, convert to utf-8 */
      DWORD bytes = req->u.io.overlapped.InternalHigh;
      handle->read_cb((uv_stream_t*) handle, bytes, &buf);
    }
    handle->flags &= ~UV_HANDLE_CANCELLATION_PENDING;
  }

  /* Wait for more input events. */
  if ((handle->flags & UV_HANDLE_READING) &&
      !(handle->flags & UV_HANDLE_READ_PENDING)) {
    uv__tty_queue_read(loop, handle);
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_tty_read_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* req) {
  assert(handle->type == UV_TTY);
  assert(handle->flags & UV_HANDLE_TTY_READABLE);

  /* If the read_line_buffer member is zero, it must have been an raw read.
   * Otherwise it was a line-buffered read. FIXME: This is quite obscure. Use a
   * flag or something. */
  if (handle->tty.rd.read_line_buffer.len == 0) {
    uv_process_tty_read_raw_req(loop, handle, req);
  } else {
    uv_process_tty_read_line_req(loop, handle, req);
  }
}


int uv__tty_read_start(uv_tty_t* handle, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb) {
  uv_loop_t* loop = handle->loop;

  if (!(handle->flags & UV_HANDLE_TTY_READABLE)) {
    return ERROR_INVALID_PARAMETER;
  }

  handle->flags |= UV_HANDLE_READING;
  INCREASE_ACTIVE_COUNT(loop, handle);
  handle->read_cb = read_cb;
  handle->alloc_cb = alloc_cb;

  /* If reading was stopped and then started again, there could still be a read
   * request pending. */
  if (handle->flags & UV_HANDLE_READ_PENDING) {
    return 0;
  }

  /* Maybe the user stopped reading half-way while processing key events.
   * Short-circuit if this could be the case. */
  if (handle->tty.rd.last_key_len > 0) {
    SET_REQ_SUCCESS(&handle->read_req);
    uv__insert_pending_req(handle->loop, (uv_req_t*) &handle->read_req);
    /* Make sure no attempt is made to insert it again until it's handled. */
    handle->flags |= UV_HANDLE_READ_PENDING;
    handle->reqs_pending++;
    return 0;
  }

  uv__tty_queue_read(loop, handle);

  return 0;
}


int uv__tty_read_stop(uv_tty_t* handle) {
  INPUT_RECORD record;
  DWORD written, err;

  handle->flags &= ~UV_HANDLE_READING;
  DECREASE_ACTIVE_COUNT(handle->loop, handle);

  if (!(handle->flags & UV_HANDLE_READ_PENDING))
    return 0;

  if (handle->flags & UV_HANDLE_TTY_RAW) {
    /* Cancel raw read. Write some bullshit event to force the console wait to
     * return. */
    memset(&record, 0, sizeof record);
    record.EventType = FOCUS_EVENT;
    if (!WriteConsoleInputW(handle->handle, &record, 1, &written)) {
      return GetLastError();
    }
  } else if (!(handle->flags & UV_HANDLE_CANCELLATION_PENDING)) {
    /* Cancel line-buffered read if not already pending */
    err = uv__cancel_read_console(handle);
    if (err)
      return err;

    handle->flags |= UV_HANDLE_CANCELLATION_PENDING;
  }

  return 0;
}

static int uv__cancel_read_console(uv_tty_t* handle) {
  HANDLE active_screen_buffer = INVALID_HANDLE_VALUE;
  INPUT_RECORD record;
  DWORD written;
  DWORD err = 0;
  LONG status;

  assert(!(handle->flags & UV_HANDLE_CANCELLATION_PENDING));

  /* Hold the output lock during the cancellation, to ensure that further
     writes don't interfere with the screen state. It will be the ReadConsole
     thread's responsibility to release the lock. */
  uv_sem_wait(&uv_tty_output_lock);
  status = InterlockedExchange(&uv__read_console_status, TRAP_REQUESTED);
  if (status != IN_PROGRESS) {
    /* Either we have managed to set a trap for the other thread before
       ReadConsole is called, or ReadConsole has returned because the user
       has pressed ENTER. In either case, there is nothing else to do. */
    uv_sem_post(&uv_tty_output_lock);
    return 0;
  }

  /* Save screen state before sending the VK_RETURN event */
  active_screen_buffer = CreateFileA("conout$",
                                     GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL,
                                     OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL,
                                     NULL);

  if (active_screen_buffer != INVALID_HANDLE_VALUE &&
      GetConsoleScreenBufferInfo(active_screen_buffer,
                                 &uv__saved_screen_state)) {
    InterlockedOr(&uv__restore_screen_state, 1);
  }

  /* Write enter key event to force the console wait to return. */
  record.EventType = KEY_EVENT;
  record.Event.KeyEvent.bKeyDown = TRUE;
  record.Event.KeyEvent.wRepeatCount = 1;
  record.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
  record.Event.KeyEvent.wVirtualScanCode =
    MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
  record.Event.KeyEvent.uChar.UnicodeChar = L'\r';
  record.Event.KeyEvent.dwControlKeyState = 0;
  if (!WriteConsoleInputW(handle->handle, &record, 1, &written))
    err = GetLastError();

  if (active_screen_buffer != INVALID_HANDLE_VALUE)
    CloseHandle(active_screen_buffer);

  return err;
}


static void uv__tty_update_virtual_window(CONSOLE_SCREEN_BUFFER_INFO* info) {
  uv_tty_virtual_width = info->dwSize.X;
  uv_tty_virtual_height = info->srWindow.Bottom - info->srWindow.Top + 1;

  /* Recompute virtual window offset row. */
  if (uv_tty_virtual_offset == -1) {
    uv_tty_virtual_offset = info->dwCursorPosition.Y;
  } else if (uv_tty_virtual_offset < info->dwCursorPosition.Y -
             uv_tty_virtual_height + 1) {
    /* If suddenly find the cursor outside of the virtual window, it must have
     * somehow scrolled. Update the virtual window offset. */
    uv_tty_virtual_offset = info->dwCursorPosition.Y -
                            uv_tty_virtual_height + 1;
  }
  if (uv_tty_virtual_offset + uv_tty_virtual_height > info->dwSize.Y) {
    uv_tty_virtual_offset = info->dwSize.Y - uv_tty_virtual_height;
  }
  if (uv_tty_virtual_offset < 0) {
    uv_tty_virtual_offset = 0;
  }
}


static COORD uv__tty_make_real_coord(uv_tty_t* handle,
    CONSOLE_SCREEN_BUFFER_INFO* info, int x, unsigned char x_relative, int y,
    unsigned char y_relative) {
  COORD result;

  uv__tty_update_virtual_window(info);

  /* Adjust y position */
  if (y_relative) {
    y = info->dwCursorPosition.Y + y;
  } else {
    y = uv_tty_virtual_offset + y;
  }
  /* Clip y to virtual client rectangle */
  if (y < uv_tty_virtual_offset) {
    y = uv_tty_virtual_offset;
  } else if (y >= uv_tty_virtual_offset + uv_tty_virtual_height) {
    y = uv_tty_virtual_offset + uv_tty_virtual_height - 1;
  }

  /* Adjust x */
  if (x_relative) {
    x = info->dwCursorPosition.X + x;
  }
  /* Clip x */
  if (x < 0) {
    x = 0;
  } else if (x >= uv_tty_virtual_width) {
    x = uv_tty_virtual_width - 1;
  }

  result.X = (unsigned short) x;
  result.Y = (unsigned short) y;
  return result;
}


static int uv__tty_emit_text(uv_tty_t* handle, WCHAR buffer[], DWORD length,
    DWORD* error) {
  DWORD written;

  if (*error != ERROR_SUCCESS) {
    return -1;
  }

  if (!WriteConsoleW(handle->handle,
                     (void*) buffer,
                     length,
                     &written,
                     NULL)) {
    *error = GetLastError();
    return -1;
  }

  return 0;
}


static int uv__tty_move_caret(uv_tty_t* handle, int x, unsigned char x_relative,
    int y, unsigned char y_relative, DWORD* error) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  COORD pos;

  if (*error != ERROR_SUCCESS) {
    return -1;
  }

 retry:
  if (!GetConsoleScreenBufferInfo(handle->handle, &info)) {
    *error = GetLastError();
  }

  pos = uv__tty_make_real_coord(handle, &info, x, x_relative, y, y_relative);

  if (!SetConsoleCursorPosition(handle->handle, pos)) {
    if (GetLastError() == ERROR_INVALID_PARAMETER) {
      /* The console may be resized - retry */
      goto retry;
    } else {
      *error = GetLastError();
      return -1;
    }
  }

  return 0;
}


static int uv__tty_reset(uv_tty_t* handle, DWORD* error) {
  const COORD origin = {0, 0};
  const WORD char_attrs = uv_tty_default_text_attributes;
  CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;
  DWORD count, written;

  if (*error != ERROR_SUCCESS) {
    return -1;
  }

  /* Reset original text attributes. */
  if (!SetConsoleTextAttribute(handle->handle, char_attrs)) {
    *error = GetLastError();
    return -1;
  }

  /* Move the cursor position to (0, 0). */
  if (!SetConsoleCursorPosition(handle->handle, origin)) {
    *error = GetLastError();
    return -1;
  }

  /* Clear the screen buffer. */
 retry:
   if (!GetConsoleScreenBufferInfo(handle->handle, &screen_buffer_info)) {
     *error = GetLastError();
     return -1;
  }

  count = screen_buffer_info.dwSize.X * screen_buffer_info.dwSize.Y;

  if (!(FillConsoleOutputCharacterW(handle->handle,
                                    L'\x20',
                                    count,
                                    origin,
                                    &written) &&
        FillConsoleOutputAttribute(handle->handle,
                                   char_attrs,
                                   written,
                                   origin,
                                   &written))) {
    if (GetLastError() == ERROR_INVALID_PARAMETER) {
      /* The console may be resized - retry */
      goto retry;
    } else {
      *error = GetLastError();
      return -1;
    }
  }

  /* Move the virtual window up to the top. */
  uv_tty_virtual_offset = 0;
  uv__tty_update_virtual_window(&screen_buffer_info);

  /* Reset the cursor size and the cursor state. */
  if (!SetConsoleCursorInfo(handle->handle, &uv_tty_default_cursor_info)) {
    *error = GetLastError();
    return -1;
  }

  return 0;
}


static int uv__tty_clear(uv_tty_t* handle, int dir, char entire_screen,
    DWORD* error) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  COORD start, end;
  DWORD count, written;

  int x1, x2, y1, y2;
  int x1r, x2r, y1r, y2r;

  if (*error != ERROR_SUCCESS) {
    return -1;
  }

  if (dir == 0) {
    /* Clear from current position */
    x1 = 0;
    x1r = 1;
  } else {
    /* Clear from column 0 */
    x1 = 0;
    x1r = 0;
  }

  if (dir == 1) {
    /* Clear to current position */
    x2 = 0;
    x2r = 1;
  } else {
    /* Clear to end of row. We pretend the console is 65536 characters wide,
     * uv__tty_make_real_coord will clip it to the actual console width. */
    x2 = 0xffff;
    x2r = 0;
  }

  if (!entire_screen) {
    /* Stay on our own row */
    y1 = y2 = 0;
    y1r = y2r = 1;
  } else {
    /* Apply columns direction to row */
    y1 = x1;
    y1r = x1r;
    y2 = x2;
    y2r = x2r;
  }

 retry:
  if (!GetConsoleScreenBufferInfo(handle->handle, &info)) {
    *error = GetLastError();
    return -1;
  }

  start = uv__tty_make_real_coord(handle, &info, x1, x1r, y1, y1r);
  end = uv__tty_make_real_coord(handle, &info, x2, x2r, y2, y2r);
  count = (end.Y * info.dwSize.X + end.X) -
          (start.Y * info.dwSize.X + start.X) + 1;

  if (!(FillConsoleOutputCharacterW(handle->handle,
                              L'\x20',
                              count,
                              start,
                              &written) &&
        FillConsoleOutputAttribute(handle->handle,
                                   info.wAttributes,
                                   written,
                                   start,
                                   &written))) {
    if (GetLastError() == ERROR_INVALID_PARAMETER) {
      /* The console may be resized - retry */
      goto retry;
    } else {
      *error = GetLastError();
      return -1;
    }
  }

  return 0;
}

#define FLIP_FGBG                                                             \
    do {                                                                      \
      WORD fg = info.wAttributes & 0xF;                                       \
      WORD bg = info.wAttributes & 0xF0;                                      \
      info.wAttributes &= 0xFF00;                                             \
      info.wAttributes |= fg << 4;                                            \
      info.wAttributes |= bg >> 4;                                            \
    } while (0)

static int uv__tty_set_style(uv_tty_t* handle, DWORD* error) {
  unsigned short argc = handle->tty.wr.ansi_csi_argc;
  unsigned short* argv = handle->tty.wr.ansi_csi_argv;
  int i;
  CONSOLE_SCREEN_BUFFER_INFO info;

  char fg_color = -1, bg_color = -1;
  char fg_bright = -1, bg_bright = -1;
  char inverse = -1;

  if (argc == 0) {
    /* Reset mode */
    fg_color = uv_tty_default_fg_color;
    bg_color = uv_tty_default_bg_color;
    fg_bright = uv_tty_default_fg_bright;
    bg_bright = uv_tty_default_bg_bright;
    inverse = uv_tty_default_inverse;
  }

  for (i = 0; i < argc; i++) {
    short arg = argv[i];

    if (arg == 0) {
      /* Reset mode */
      fg_color = uv_tty_default_fg_color;
      bg_color = uv_tty_default_bg_color;
      fg_bright = uv_tty_default_fg_bright;
      bg_bright = uv_tty_default_bg_bright;
      inverse = uv_tty_default_inverse;

    } else if (arg == 1) {
      /* Foreground bright on */
      fg_bright = 1;

    } else if (arg == 2) {
      /* Both bright off */
      fg_bright = 0;
      bg_bright = 0;

    } else if (arg == 5) {
      /* Background bright on */
      bg_bright = 1;

    } else if (arg == 7) {
      /* Inverse: on */
      inverse = 1;

    } else if (arg == 21 || arg == 22) {
      /* Foreground bright off */
      fg_bright = 0;

    } else if (arg == 25) {
      /* Background bright off */
      bg_bright = 0;

    } else if (arg == 27) {
      /* Inverse: off */
      inverse = 0;

    } else if (arg >= 30 && arg <= 37) {
      /* Set foreground color */
      fg_color = arg - 30;

    } else if (arg == 39) {
      /* Default text color */
      fg_color = uv_tty_default_fg_color;
      fg_bright = uv_tty_default_fg_bright;

    } else if (arg >= 40 && arg <= 47) {
      /* Set background color */
      bg_color = arg - 40;

    } else if (arg ==  49) {
      /* Default background color */
      bg_color = uv_tty_default_bg_color;
      bg_bright = uv_tty_default_bg_bright;

    } else if (arg >= 90 && arg <= 97) {
      /* Set bold foreground color */
      fg_bright = 1;
      fg_color = arg - 90;

    } else if (arg >= 100 && arg <= 107) {
      /* Set bold background color */
      bg_bright = 1;
      bg_color = arg - 100;

    }
  }

  if (fg_color == -1 && bg_color == -1 && fg_bright == -1 &&
      bg_bright == -1 && inverse == -1) {
    /* Nothing changed */
    return 0;
  }

  if (!GetConsoleScreenBufferInfo(handle->handle, &info)) {
    *error = GetLastError();
    return -1;
  }

  if ((info.wAttributes & COMMON_LVB_REVERSE_VIDEO) > 0) {
    FLIP_FGBG;
  }

  if (fg_color != -1) {
    info.wAttributes &= ~(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    if (fg_color & 1) info.wAttributes |= FOREGROUND_RED;
    if (fg_color & 2) info.wAttributes |= FOREGROUND_GREEN;
    if (fg_color & 4) info.wAttributes |= FOREGROUND_BLUE;
  }

  if (fg_bright != -1) {
    if (fg_bright) {
      info.wAttributes |= FOREGROUND_INTENSITY;
    } else {
      info.wAttributes &= ~FOREGROUND_INTENSITY;
    }
  }

  if (bg_color != -1) {
    info.wAttributes &= ~(BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE);
    if (bg_color & 1) info.wAttributes |= BACKGROUND_RED;
    if (bg_color & 2) info.wAttributes |= BACKGROUND_GREEN;
    if (bg_color & 4) info.wAttributes |= BACKGROUND_BLUE;
  }

  if (bg_bright != -1) {
    if (bg_bright) {
      info.wAttributes |= BACKGROUND_INTENSITY;
    } else {
      info.wAttributes &= ~BACKGROUND_INTENSITY;
    }
  }

  if (inverse != -1) {
    if (inverse) {
      info.wAttributes |= COMMON_LVB_REVERSE_VIDEO;
    } else {
      info.wAttributes &= ~COMMON_LVB_REVERSE_VIDEO;
    }
  }

  if ((info.wAttributes & COMMON_LVB_REVERSE_VIDEO) > 0) {
    FLIP_FGBG;
  }

  if (!SetConsoleTextAttribute(handle->handle, info.wAttributes)) {
    *error = GetLastError();
    return -1;
  }

  return 0;
}


static int uv__tty_save_state(uv_tty_t* handle, unsigned char save_attributes,
    DWORD* error) {
  CONSOLE_SCREEN_BUFFER_INFO info;

  if (*error != ERROR_SUCCESS) {
    return -1;
  }

  if (!GetConsoleScreenBufferInfo(handle->handle, &info)) {
    *error = GetLastError();
    return -1;
  }

  uv__tty_update_virtual_window(&info);

  handle->tty.wr.saved_position.X = info.dwCursorPosition.X;
  handle->tty.wr.saved_position.Y = info.dwCursorPosition.Y -
        uv_tty_virtual_offset;
  handle->flags |= UV_HANDLE_TTY_SAVED_POSITION;

  if (save_attributes) {
    handle->tty.wr.saved_attributes = info.wAttributes &
        (FOREGROUND_INTENSITY | BACKGROUND_INTENSITY);
    handle->flags |= UV_HANDLE_TTY_SAVED_ATTRIBUTES;
  }

  return 0;
}


static int uv__tty_restore_state(uv_tty_t* handle,
    unsigned char restore_attributes, DWORD* error) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  WORD new_attributes;

  if (*error != ERROR_SUCCESS) {
    return -1;
  }

  if (handle->flags & UV_HANDLE_TTY_SAVED_POSITION) {
    if (uv__tty_move_caret(handle,
                          handle->tty.wr.saved_position.X,
                          0,
                          handle->tty.wr.saved_position.Y,
                          0,
                          error) != 0) {
      return -1;
    }
  }

  if (restore_attributes &&
      (handle->flags & UV_HANDLE_TTY_SAVED_ATTRIBUTES)) {
    if (!GetConsoleScreenBufferInfo(handle->handle, &info)) {
      *error = GetLastError();
      return -1;
    }

    new_attributes = info.wAttributes;
    new_attributes &= ~(FOREGROUND_INTENSITY | BACKGROUND_INTENSITY);
    new_attributes |= handle->tty.wr.saved_attributes;

    if (!SetConsoleTextAttribute(handle->handle, new_attributes)) {
      *error = GetLastError();
      return -1;
    }
  }

  return 0;
}

static int uv__tty_set_cursor_visibility(uv_tty_t* handle,
                                        BOOL visible,
                                        DWORD* error) {
  CONSOLE_CURSOR_INFO cursor_info;

  if (!GetConsoleCursorInfo(handle->handle, &cursor_info)) {
    *error = GetLastError();
    return -1;
  }

  cursor_info.bVisible = visible;

  if (!SetConsoleCursorInfo(handle->handle, &cursor_info)) {
    *error = GetLastError();
    return -1;
  }

  return 0;
}

static int uv__tty_set_cursor_shape(uv_tty_t* handle, int style, DWORD* error) {
  CONSOLE_CURSOR_INFO cursor_info;

  if (!GetConsoleCursorInfo(handle->handle, &cursor_info)) {
    *error = GetLastError();
    return -1;
  }

  if (style == 0) {
    cursor_info.dwSize = uv_tty_default_cursor_info.dwSize;
  } else if (style <= 2) {
    cursor_info.dwSize = CURSOR_SIZE_LARGE;
  } else {
    cursor_info.dwSize = CURSOR_SIZE_SMALL;
  }

  if (!SetConsoleCursorInfo(handle->handle, &cursor_info)) {
    *error = GetLastError();
    return -1;
  }

  return 0;
}


static int uv__tty_write_bufs(uv_tty_t* handle,
                             const uv_buf_t bufs[],
                             unsigned int nbufs,
                             DWORD* error) {
  /* We can only write 8k characters at a time. Windows can't handle much more
   * characters in a single console write anyway. */
  WCHAR utf16_buf[MAX_CONSOLE_CHAR];
  DWORD utf16_buf_used = 0;
  unsigned int i;

#define FLUSH_TEXT()                                                \
  do {                                                              \
    if (utf16_buf_used > 0) {                                       \
      uv__tty_emit_text(handle, utf16_buf, utf16_buf_used, error);  \
      utf16_buf_used = 0;                                           \
    }                                                               \
  } while (0)

#define ENSURE_BUFFER_SPACE(wchars_needed)                          \
  if (wchars_needed > ARRAY_SIZE(utf16_buf) - utf16_buf_used) {     \
    FLUSH_TEXT();                                                   \
  }

  /* Cache for fast access */
  unsigned char utf8_bytes_left = handle->tty.wr.utf8_bytes_left;
  unsigned int utf8_codepoint = handle->tty.wr.utf8_codepoint;
  unsigned char previous_eol = handle->tty.wr.previous_eol;
  unsigned short ansi_parser_state = handle->tty.wr.ansi_parser_state;

  /* Store the error here. If we encounter an error, stop trying to do i/o but
   * keep parsing the buffer so we leave the parser in a consistent state. */
  *error = ERROR_SUCCESS;

  uv_sem_wait(&uv_tty_output_lock);

  for (i = 0; i < nbufs; i++) {
    uv_buf_t buf = bufs[i];
    unsigned int j;

    for (j = 0; j < buf.len; j++) {
      unsigned char c = buf.base[j];

      /* Run the character through the utf8 decoder We happily accept non
       * shortest form encodings and invalid code points - there's no real harm
       * that can be done. */
      if (utf8_bytes_left == 0) {
        /* Read utf-8 start byte */
        DWORD first_zero_bit;
        unsigned char not_c = ~c;
#ifdef _MSC_VER /* msvc */
        if (_BitScanReverse(&first_zero_bit, not_c)) {
#else /* assume gcc */
        if (c != 0) {
          first_zero_bit = (sizeof(int) * 8) - 1 - __builtin_clz(not_c);
#endif
          if (first_zero_bit == 7) {
            /* Ascii - pass right through */
            utf8_codepoint = (unsigned int) c;

          } else if (first_zero_bit <= 5) {
            /* Multibyte sequence */
            utf8_codepoint = (0xff >> (8 - first_zero_bit)) & c;
            utf8_bytes_left = (char) (6 - first_zero_bit);

          } else {
            /* Invalid continuation */
            utf8_codepoint = UNICODE_REPLACEMENT_CHARACTER;
          }

        } else {
          /* 0xff -- invalid */
          utf8_codepoint = UNICODE_REPLACEMENT_CHARACTER;
        }

      } else if ((c & 0xc0) == 0x80) {
        /* Valid continuation of utf-8 multibyte sequence */
        utf8_bytes_left--;
        utf8_codepoint <<= 6;
        utf8_codepoint |= ((unsigned int) c & 0x3f);

      } else {
        /* Start byte where continuation was expected. */
        utf8_bytes_left = 0;
        utf8_codepoint = UNICODE_REPLACEMENT_CHARACTER;
        /* Patch buf offset so this character will be parsed again as a start
         * byte. */
        j--;
      }

      /* Maybe we need to parse more bytes to find a character. */
      if (utf8_bytes_left != 0) {
        continue;
      }

      /* Parse vt100/ansi escape codes */
      if (uv__vterm_state == UV_TTY_SUPPORTED) {
        /* Pass through escape codes if conhost supports them. */
      } else if (ansi_parser_state == ANSI_NORMAL) {
        switch (utf8_codepoint) {
          case '\033':
            ansi_parser_state = ANSI_ESCAPE_SEEN;
            continue;

          case 0233:
            ansi_parser_state = ANSI_CSI;
            handle->tty.wr.ansi_csi_argc = 0;
            continue;
        }

      } else if (ansi_parser_state == ANSI_ESCAPE_SEEN) {
        switch (utf8_codepoint) {
          case '[':
            ansi_parser_state = ANSI_CSI;
            handle->tty.wr.ansi_csi_argc = 0;
            continue;

          case '^':
          case '_':
          case 'P':
          case ']':
            /* Not supported, but we'll have to parse until we see a stop code,
             * e. g. ESC \ or BEL. */
            ansi_parser_state = ANSI_ST_CONTROL;
            continue;

          case '\033':
            /* Ignore double escape. */
            continue;

          case 'c':
            /* Full console reset. */
            FLUSH_TEXT();
            uv__tty_reset(handle, error);
            ansi_parser_state = ANSI_NORMAL;
            continue;

          case '7':
            /* Save the cursor position and text attributes. */
            FLUSH_TEXT();
            uv__tty_save_state(handle, 1, error);
            ansi_parser_state = ANSI_NORMAL;
            continue;

          case '8':
            /* Restore the cursor position and text attributes */
            FLUSH_TEXT();
            uv__tty_restore_state(handle, 1, error);
            ansi_parser_state = ANSI_NORMAL;
            continue;

          default:
            if (utf8_codepoint >= '@' && utf8_codepoint <= '_') {
              /* Single-char control. */
              ansi_parser_state = ANSI_NORMAL;
              continue;
            } else {
              /* Invalid - proceed as normal, */
              ansi_parser_state = ANSI_NORMAL;
            }
        }

      } else if (ansi_parser_state == ANSI_IGNORE) {
        /* We're ignoring this command. Stop only on command character. */
        if (utf8_codepoint >= '@' && utf8_codepoint <= '~') {
          ansi_parser_state = ANSI_NORMAL;
        }
        continue;

      } else if (ansi_parser_state == ANSI_DECSCUSR) {
        /* So far we've the sequence `ESC [ arg space`, and we're waiting for
         * the final command byte. */
        if (utf8_codepoint >= '@' && utf8_codepoint <= '~') {
          /* Command byte */
          if (utf8_codepoint == 'q') {
            /* Change the cursor shape */
            int style = handle->tty.wr.ansi_csi_argc
              ? handle->tty.wr.ansi_csi_argv[0] : 1;
            if (style >= 0 && style <= 6) {
              FLUSH_TEXT();
              uv__tty_set_cursor_shape(handle, style, error);
            }
          }

          /* Sequence ended - go back to normal state. */
          ansi_parser_state = ANSI_NORMAL;
          continue;
        }
        /* Unexpected character, but sequence hasn't ended yet. Ignore the rest
         * of the sequence. */
        ansi_parser_state = ANSI_IGNORE;

      } else if (ansi_parser_state & ANSI_CSI) {
        /* So far we've seen `ESC [`, and we may or may not have already parsed
         * some of the arguments that follow. */

        if (utf8_codepoint >= '0' && utf8_codepoint <= '9') {
          /* Parse a numerical argument. */
          if (!(ansi_parser_state & ANSI_IN_ARG)) {
            /* We were not currently parsing a number, add a new one. */
            /* Check for that there are too many arguments. */
            if (handle->tty.wr.ansi_csi_argc >=
                ARRAY_SIZE(handle->tty.wr.ansi_csi_argv)) {
              ansi_parser_state = ANSI_IGNORE;
              continue;
            }
            ansi_parser_state |= ANSI_IN_ARG;
            handle->tty.wr.ansi_csi_argc++;
            handle->tty.wr.ansi_csi_argv[handle->tty.wr.ansi_csi_argc - 1] =
                (unsigned short) utf8_codepoint - '0';
            continue;

          } else {
            /* We were already parsing a number. Parse next digit. */
            uint32_t value = 10 *
                handle->tty.wr.ansi_csi_argv[handle->tty.wr.ansi_csi_argc - 1];

            /* Check for overflow. */
            if (value > UINT16_MAX) {
              ansi_parser_state = ANSI_IGNORE;
              continue;
            }

            handle->tty.wr.ansi_csi_argv[handle->tty.wr.ansi_csi_argc - 1] =
                (unsigned short) value + (utf8_codepoint - '0');
            continue;
          }

        } else if (utf8_codepoint == ';') {
          /* Denotes the end of an argument. */
          if (ansi_parser_state & ANSI_IN_ARG) {
            ansi_parser_state &= ~ANSI_IN_ARG;
            continue;

          } else {
            /* If ANSI_IN_ARG is not set, add another argument and default
             * it to 0. */

            /* Check for too many arguments */
            if (handle->tty.wr.ansi_csi_argc >=

                ARRAY_SIZE(handle->tty.wr.ansi_csi_argv)) {
              ansi_parser_state = ANSI_IGNORE;
              continue;
            }

            handle->tty.wr.ansi_csi_argc++;
            handle->tty.wr.ansi_csi_argv[handle->tty.wr.ansi_csi_argc - 1] = 0;
            continue;
          }

        } else if (utf8_codepoint == '?' &&
                   !(ansi_parser_state & ANSI_IN_ARG) &&
                   !(ansi_parser_state & ANSI_EXTENSION) &&
                   handle->tty.wr.ansi_csi_argc == 0) {
          /* Pass through '?' if it is the first character after CSI */
          /* This is an extension character from the VT100 codeset */
          /* that is supported and used by most ANSI terminals today. */
          ansi_parser_state |= ANSI_EXTENSION;
          continue;

        } else if (utf8_codepoint == ' ' &&
                   !(ansi_parser_state & ANSI_EXTENSION)) {
          /* We expect a command byte to follow after this space. The only
           * command that we current support is 'set cursor style'. */
          ansi_parser_state = ANSI_DECSCUSR;
          continue;

        } else if (utf8_codepoint >= '@' && utf8_codepoint <= '~') {
          /* Command byte */
          if (ansi_parser_state & ANSI_EXTENSION) {
            /* Sequence is `ESC [ ? args command`. */
            switch (utf8_codepoint) {
              case 'l':
                /* Hide the cursor */
                if (handle->tty.wr.ansi_csi_argc == 1 &&
                    handle->tty.wr.ansi_csi_argv[0] == 25) {
                  FLUSH_TEXT();
                  uv__tty_set_cursor_visibility(handle, 0, error);
                }
                break;

              case 'h':
                /* Show the cursor */
                if (handle->tty.wr.ansi_csi_argc == 1 &&
                    handle->tty.wr.ansi_csi_argv[0] == 25) {
                  FLUSH_TEXT();
                  uv__tty_set_cursor_visibility(handle, 1, error);
                }
                break;
            }

          } else {
            /* Sequence is `ESC [ args command`. */
            int x, y, d;
            switch (utf8_codepoint) {
              case 'A':
                /* cursor up */
                FLUSH_TEXT();
                y = -(handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 1);
                uv__tty_move_caret(handle, 0, 1, y, 1, error);
                break;

              case 'B':
                /* cursor down */
                FLUSH_TEXT();
                y = handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 1;
                uv__tty_move_caret(handle, 0, 1, y, 1, error);
                break;

              case 'C':
                /* cursor forward */
                FLUSH_TEXT();
                x = handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 1;
                uv__tty_move_caret(handle, x, 1, 0, 1, error);
                break;

              case 'D':
                /* cursor back */
                FLUSH_TEXT();
                x = -(handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 1);
                uv__tty_move_caret(handle, x, 1, 0, 1, error);
                break;

              case 'E':
                /* cursor next line */
                FLUSH_TEXT();
                y = handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 1;
                uv__tty_move_caret(handle, 0, 0, y, 1, error);
                break;

              case 'F':
                /* cursor previous line */
                FLUSH_TEXT();
                y = -(handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 1);
                uv__tty_move_caret(handle, 0, 0, y, 1, error);
                break;

              case 'G':
                /* cursor horizontal move absolute */
                FLUSH_TEXT();
                x = (handle->tty.wr.ansi_csi_argc >= 1 &&
                     handle->tty.wr.ansi_csi_argv[0])
                  ? handle->tty.wr.ansi_csi_argv[0] - 1 : 0;
                uv__tty_move_caret(handle, x, 0, 0, 1, error);
                break;

              case 'H':
              case 'f':
                /* cursor move absolute */
                FLUSH_TEXT();
                y = (handle->tty.wr.ansi_csi_argc >= 1 &&
                     handle->tty.wr.ansi_csi_argv[0])
                  ? handle->tty.wr.ansi_csi_argv[0] - 1 : 0;
                x = (handle->tty.wr.ansi_csi_argc >= 2 &&
                     handle->tty.wr.ansi_csi_argv[1])
                  ? handle->tty.wr.ansi_csi_argv[1] - 1 : 0;
                uv__tty_move_caret(handle, x, 0, y, 0, error);
                break;

              case 'J':
                /* Erase screen */
                FLUSH_TEXT();
                d = handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 0;
                if (d >= 0 && d <= 2) {
                  uv__tty_clear(handle, d, 1, error);
                }
                break;

              case 'K':
                /* Erase line */
                FLUSH_TEXT();
                d = handle->tty.wr.ansi_csi_argc
                  ? handle->tty.wr.ansi_csi_argv[0] : 0;
                if (d >= 0 && d <= 2) {
                  uv__tty_clear(handle, d, 0, error);
                }
                break;

              case 'm':
                /* Set style */
                FLUSH_TEXT();
                uv__tty_set_style(handle, error);
                break;

              case 's':
                /* Save the cursor position. */
                FLUSH_TEXT();
                uv__tty_save_state(handle, 0, error);
                break;

              case 'u':
                /* Restore the cursor position */
                FLUSH_TEXT();
                uv__tty_restore_state(handle, 0, error);
                break;
            }
          }

          /* Sequence ended - go back to normal state. */
          ansi_parser_state = ANSI_NORMAL;
          continue;

        } else {
          /* We don't support commands that use private mode characters or
           * intermediaries. Ignore the rest of the sequence. */
          ansi_parser_state = ANSI_IGNORE;
          continue;
        }

      } else if (ansi_parser_state & ANSI_ST_CONTROL) {
        /* Unsupported control code.
         * Ignore everything until we see `BEL` or `ESC \`. */
        if (ansi_parser_state & ANSI_IN_STRING) {
          if (!(ansi_parser_state & ANSI_BACKSLASH_SEEN)) {
            if (utf8_codepoint == '"') {
              ansi_parser_state &= ~ANSI_IN_STRING;
            } else if (utf8_codepoint == '\\') {
              ansi_parser_state |= ANSI_BACKSLASH_SEEN;
            }
          } else {
            ansi_parser_state &= ~ANSI_BACKSLASH_SEEN;
          }
        } else {
          if (utf8_codepoint == '\007' || (utf8_codepoint == '\\' &&
              (ansi_parser_state & ANSI_ESCAPE_SEEN))) {
            /* End of sequence */
            ansi_parser_state = ANSI_NORMAL;
          } else if (utf8_codepoint == '\033') {
            /* Escape character */
            ansi_parser_state |= ANSI_ESCAPE_SEEN;
          } else if (utf8_codepoint == '"') {
             /* String starting */
            ansi_parser_state |= ANSI_IN_STRING;
            ansi_parser_state &= ~ANSI_ESCAPE_SEEN;
            ansi_parser_state &= ~ANSI_BACKSLASH_SEEN;
          } else {
            ansi_parser_state &= ~ANSI_ESCAPE_SEEN;
          }
        }
        continue;
      } else {
        /* Inconsistent state */
        abort();
      }

      if (utf8_codepoint == 0x0a || utf8_codepoint == 0x0d) {
        /* EOL conversion - emit \r\n when we see \n. */

        if (utf8_codepoint == 0x0a && previous_eol != 0x0d) {
          /* \n was not preceded by \r; print \r\n. */
          ENSURE_BUFFER_SPACE(2);
          utf16_buf[utf16_buf_used++] = L'\r';
          utf16_buf[utf16_buf_used++] = L'\n';
        } else if (utf8_codepoint == 0x0d && previous_eol == 0x0a) {
          /* \n was followed by \r; do not print the \r, since the source was
           * either \r\n\r (so the second \r is redundant) or was \n\r (so the
           * \n was processed by the last case and an \r automatically
           * inserted). */
        } else {
          /* \r without \n; print \r as-is. */
          ENSURE_BUFFER_SPACE(1);
          utf16_buf[utf16_buf_used++] = (WCHAR) utf8_codepoint;
        }

        previous_eol = (char) utf8_codepoint;

      } else if (utf8_codepoint <= 0xffff) {
        /* Encode character into utf-16 buffer. */
        ENSURE_BUFFER_SPACE(1);
        utf16_buf[utf16_buf_used++] = (WCHAR) utf8_codepoint;
        previous_eol = 0;
      } else {
        ENSURE_BUFFER_SPACE(2);
        utf8_codepoint -= 0x10000;
        utf16_buf[utf16_buf_used++] = (WCHAR) (utf8_codepoint / 0x400 + 0xD800);
        utf16_buf[utf16_buf_used++] = (WCHAR) (utf8_codepoint % 0x400 + 0xDC00);
        previous_eol = 0;
      }
    }
  }

  /* Flush remaining characters */
  FLUSH_TEXT();

  /* Copy cached values back to struct. */
  handle->tty.wr.utf8_bytes_left = utf8_bytes_left;
  handle->tty.wr.utf8_codepoint = utf8_codepoint;
  handle->tty.wr.previous_eol = previous_eol;
  handle->tty.wr.ansi_parser_state = ansi_parser_state;

  uv_sem_post(&uv_tty_output_lock);

  if (*error == STATUS_SUCCESS) {
    return 0;
  } else {
    return -1;
  }

#undef FLUSH_TEXT
}


int uv__tty_write(uv_loop_t* loop,
                 uv_write_t* req,
                 uv_tty_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 uv_write_cb cb) {
  DWORD error;

  UV_REQ_INIT(req, UV_WRITE);
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;

  handle->reqs_pending++;
  handle->stream.conn.write_reqs_pending++;
  REGISTER_HANDLE_REQ(loop, handle);

  req->u.io.queued_bytes = 0;

  if (!uv__tty_write_bufs(handle, bufs, nbufs, &error)) {
    SET_REQ_SUCCESS(req);
  } else {
    SET_REQ_ERROR(req, error);
  }

  uv__insert_pending_req(loop, (uv_req_t*) req);

  return 0;
}


int uv__tty_try_write(uv_tty_t* handle,
                      const uv_buf_t bufs[],
                      unsigned int nbufs) {
  DWORD error;

  if (handle->stream.conn.write_reqs_pending > 0)
    return UV_EAGAIN;

  if (uv__tty_write_bufs(handle, bufs, nbufs, &error))
    return uv_translate_sys_error(error);

  return uv__count_bufs(bufs, nbufs);
}


void uv__process_tty_write_req(uv_loop_t* loop, uv_tty_t* handle,
  uv_write_t* req) {
  int err;

  handle->write_queue_size -= req->u.io.queued_bytes;
  UNREGISTER_HANDLE_REQ(loop, handle);

  if (req->cb) {
    err = GET_REQ_ERROR(req);
    req->cb(req, uv_translate_sys_error(err));
  }


  handle->stream.conn.write_reqs_pending--;
  if (handle->stream.conn.write_reqs_pending == 0 &&
      uv__is_stream_shutting(handle))
    uv__process_tty_shutdown_req(loop,
                                 handle,
                                 handle->stream.conn.shutdown_req);

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__tty_close(uv_tty_t* handle) {
  assert(handle->u.fd == -1 || handle->u.fd > 2);
  if (handle->flags & UV_HANDLE_READING)
    uv__tty_read_stop(handle);

  if (handle->u.fd == -1)
    CloseHandle(handle->handle);
  else
    _close(handle->u.fd);

  handle->u.fd = -1;
  handle->handle = INVALID_HANDLE_VALUE;
  handle->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
  uv__handle_closing(handle);

  if (handle->reqs_pending == 0)
    uv__want_endgame(handle->loop, (uv_handle_t*) handle);
}


void uv__process_tty_shutdown_req(uv_loop_t* loop, uv_tty_t* stream, uv_shutdown_t* req) {
  assert(stream->stream.conn.write_reqs_pending == 0);
  assert(req);

  stream->stream.conn.shutdown_req = NULL;
  UNREGISTER_HANDLE_REQ(loop, stream);

  /* TTY shutdown is really just a no-op */
  if (req->cb) {
    if (stream->flags & UV_HANDLE_CLOSING) {
      req->cb(req, UV_ECANCELED);
    } else {
      req->cb(req, 0);
    }
  }

  DECREASE_PENDING_REQ_COUNT(stream);
}


void uv__tty_endgame(uv_loop_t* loop, uv_tty_t* handle) {
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(handle->reqs_pending == 0);

  /* The wait handle used for raw reading should be unregistered when the
   * wait callback runs. */
  assert(!(handle->flags & UV_HANDLE_TTY_READABLE) ||
         handle->tty.rd.read_raw_wait == NULL);

  assert(!(handle->flags & UV_HANDLE_CLOSED));
  uv__handle_close(handle);
}


int uv_tty_reset_mode(void) {
  /* Not necessary to do anything. */
  return 0;
}

/* Determine whether or not this version of windows supports
 * proper ANSI color codes. Should be supported as of windows
 * 10 version 1511, build number 10.0.10586.
 */
static void uv__determine_vterm_state(HANDLE handle) {
  DWORD dwMode = 0;

  uv__need_check_vterm_state = FALSE;
  if (!GetConsoleMode(handle, &dwMode)) {
    return;
  }

  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  if (!SetConsoleMode(handle, dwMode)) {
    return;
  }

  uv__vterm_state = UV_TTY_SUPPORTED;
}

static DWORD WINAPI uv__tty_console_resize_message_loop_thread(void* param) {
  NTSTATUS status;
  ULONG_PTR conhost_pid;
  MSG msg;

  if (pSetWinEventHook == NULL || pNtQueryInformationProcess == NULL)
    return 0;

  status = pNtQueryInformationProcess(GetCurrentProcess(),
                                      ProcessConsoleHostProcess,
                                      &conhost_pid,
                                      sizeof(conhost_pid),
                                      NULL);

  if (!NT_SUCCESS(status)) {
    /* We couldn't retrieve our console host process, probably because this
     * is a 32-bit process running on 64-bit Windows. Fall back to receiving
     * console events from the input stream only. */
    return 0;
  }

  /* Ensure the PID is a multiple of 4, which is required by SetWinEventHook */
  conhost_pid &= ~(ULONG_PTR)0x3;

  uv__tty_console_resized = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (uv__tty_console_resized == NULL)
    return 0;
  if (QueueUserWorkItem(uv__tty_console_resize_watcher_thread,
                        NULL,
                        WT_EXECUTELONGFUNCTION) == 0)
    return 0;

  if (!pSetWinEventHook(EVENT_CONSOLE_LAYOUT,
                        EVENT_CONSOLE_LAYOUT,
                        NULL,
                        uv__tty_console_resize_event,
                        (DWORD)conhost_pid,
                        0,
                        WINEVENT_OUTOFCONTEXT))
    return 0;

  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}

static void CALLBACK uv__tty_console_resize_event(HWINEVENTHOOK hWinEventHook,
                                                  DWORD event,
                                                  HWND hwnd,
                                                  LONG idObject,
                                                  LONG idChild,
                                                  DWORD dwEventThread,
                                                  DWORD dwmsEventTime) {
  SetEvent(uv__tty_console_resized);
}

static DWORD WINAPI uv__tty_console_resize_watcher_thread(void* param) {
  for (;;) {
    /* Make sure to not overwhelm the system with resize events */
    Sleep(33);
    WaitForSingleObject(uv__tty_console_resized, INFINITE);
    ResetEvent(uv__tty_console_resized);
    uv__tty_console_signal_resize();
  }
  return 0;
}

static void uv__tty_console_signal_resize(void) {
  CONSOLE_SCREEN_BUFFER_INFO sb_info;
  int width, height;

  if (!GetConsoleScreenBufferInfo(uv__tty_console_handle, &sb_info))
    return;

  width = sb_info.dwSize.X;
  height = sb_info.srWindow.Bottom - sb_info.srWindow.Top + 1;

  uv_mutex_lock(&uv__tty_console_resize_mutex);
  if (width != uv__tty_console_width || height != uv__tty_console_height) {
    uv__tty_console_width = width;
    uv__tty_console_height = height;
    uv_mutex_unlock(&uv__tty_console_resize_mutex);
    uv__signal_dispatch(SIGWINCH);
  } else {
    uv_mutex_unlock(&uv__tty_console_resize_mutex);
  }
}

void uv_tty_set_vterm_state(uv_tty_vtermstate_t state) {
  uv_sem_wait(&uv_tty_output_lock);
  uv__need_check_vterm_state = FALSE;
  uv__vterm_state = state;
  uv_sem_post(&uv_tty_output_lock);
}

int uv_tty_get_vterm_state(uv_tty_vtermstate_t* state) {
  uv_sem_wait(&uv_tty_output_lock);
  *state = uv__vterm_state;
  uv_sem_post(&uv_tty_output_lock);
  return 0;
}
