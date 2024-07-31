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

#include "task.h"
#include "uv.h"

#include <io.h>
#include <windows.h>

#include <errno.h>
#include <string.h>

#define ESC "\033"
#define CSI ESC "["
#define ST ESC "\\"
#define BEL "\x07"
#define HELLO "Hello"

#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_BLACK 0
#define FOREGROUND_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)
#define FOREGROUND_CYAN (FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_MAGENTA (FOREGROUND_RED | FOREGROUND_BLUE)
#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_BLACK 0
#define BACKGROUND_YELLOW (BACKGROUND_RED | BACKGROUND_GREEN)
#define BACKGROUND_CYAN (BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_MAGENTA (BACKGROUND_RED | BACKGROUND_BLUE)

#define F_INTENSITY      1
#define FB_INTENSITY     2
#define B_INTENSITY      5
#define INVERSE          7
#define F_INTENSITY_OFF1 21
#define F_INTENSITY_OFF2 22
#define B_INTENSITY_OFF  25
#define INVERSE_OFF      27
#define F_BLACK          30
#define F_RED            31
#define F_GREEN          32
#define F_YELLOW         33
#define F_BLUE           34
#define F_MAGENTA        35
#define F_CYAN           36
#define F_WHITE          37
#define F_DEFAULT        39
#define B_BLACK          40
#define B_RED            41
#define B_GREEN          42
#define B_YELLOW         43
#define B_BLUE           44
#define B_MAGENTA        45
#define B_CYAN           46
#define B_WHITE          47
#define B_DEFAULT        49

#define CURSOR_SIZE_SMALL     25
#define CURSOR_SIZE_MIDDLE    50
#define CURSOR_SIZE_LARGE     100

struct screen_info {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  int top;
  int width;
  int height;
  int length;
  WORD default_attr;
};

struct captured_screen {
  char* text;
  WORD* attributes;
  struct screen_info si;
};

static void get_screen_info(uv_tty_t* tty_out, struct screen_info* si) {
  ASSERT(GetConsoleScreenBufferInfo(tty_out->handle, &(si->csbi)));
  si->width = si->csbi.dwSize.X;
  si->height = si->csbi.srWindow.Bottom - si->csbi.srWindow.Top + 1;
  si->length = si->width * si->height;
  si->default_attr = si->csbi.wAttributes;
  si->top = si->csbi.srWindow.Top;
}

static void set_cursor_position(uv_tty_t* tty_out, COORD pos) {
  HANDLE handle = tty_out->handle;
  CONSOLE_SCREEN_BUFFER_INFO info;
  ASSERT(GetConsoleScreenBufferInfo(handle, &info));
  pos.X -= 1;
  pos.Y += info.srWindow.Top - 1;
  ASSERT(SetConsoleCursorPosition(handle, pos));
}

static void get_cursor_position(uv_tty_t* tty_out, COORD* cursor_position) {
  HANDLE handle = tty_out->handle;
  CONSOLE_SCREEN_BUFFER_INFO info;
  ASSERT(GetConsoleScreenBufferInfo(handle, &info));
  cursor_position->X = info.dwCursorPosition.X + 1;
  cursor_position->Y = info.dwCursorPosition.Y - info.srWindow.Top + 1;
}

static void set_cursor_to_home(uv_tty_t* tty_out) {
  COORD origin = {1, 1};
  set_cursor_position(tty_out, origin);
}

static CONSOLE_CURSOR_INFO get_cursor_info(uv_tty_t* tty_out) {
  HANDLE handle = tty_out->handle;
  CONSOLE_CURSOR_INFO info;
  ASSERT(GetConsoleCursorInfo(handle, &info));
  return info;
}

static void set_cursor_size(uv_tty_t* tty_out, DWORD size) {
  CONSOLE_CURSOR_INFO info = get_cursor_info(tty_out);
  info.dwSize = size;
  ASSERT(SetConsoleCursorInfo(tty_out->handle, &info));
}

static DWORD get_cursor_size(uv_tty_t* tty_out) {
  return get_cursor_info(tty_out).dwSize;
}

static void set_cursor_visibility(uv_tty_t* tty_out, BOOL visible) {
  CONSOLE_CURSOR_INFO info = get_cursor_info(tty_out);
  info.bVisible = visible;
  ASSERT(SetConsoleCursorInfo(tty_out->handle, &info));
}

static BOOL get_cursor_visibility(uv_tty_t* tty_out) {
  return get_cursor_info(tty_out).bVisible;
}

static BOOL is_scrolling(uv_tty_t* tty_out, struct screen_info si) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  ASSERT(GetConsoleScreenBufferInfo(tty_out->handle, &info));
  return info.srWindow.Top != si.top;
}

static void write_console(uv_tty_t* tty_out, char* src) {
  int r;
  uv_buf_t buf;

  buf.base = src;
  buf.len = strlen(buf.base);

  r = uv_try_write((uv_stream_t*) tty_out, &buf, 1);
  ASSERT_GE(r, 0);
  ASSERT_EQ((unsigned int) r, buf.len);
}

static void setup_screen(uv_tty_t* tty_out) {
  DWORD length, number_of_written;
  COORD origin;
  CONSOLE_SCREEN_BUFFER_INFO info;
  ASSERT(GetConsoleScreenBufferInfo(tty_out->handle, &info));
  length = info.dwSize.X * (info.srWindow.Bottom - info.srWindow.Top + 1);
  origin.X = 0;
  origin.Y = info.srWindow.Top;
  ASSERT(FillConsoleOutputCharacter(
         tty_out->handle, '.', length, origin, &number_of_written));
  ASSERT_EQ(length, number_of_written);
}

static void clear_screen(uv_tty_t* tty_out, struct screen_info* si) {
  DWORD length, number_of_written;
  COORD origin;
  CONSOLE_SCREEN_BUFFER_INFO info;
  ASSERT(GetConsoleScreenBufferInfo(tty_out->handle, &info));
  length = (info.srWindow.Bottom - info.srWindow.Top + 1) * info.dwSize.X - 1;
  origin.X = 0;
  origin.Y = info.srWindow.Top;
  FillConsoleOutputCharacterA(
      tty_out->handle, ' ', length, origin, &number_of_written);
  ASSERT_EQ(length, number_of_written);
  FillConsoleOutputAttribute(
      tty_out->handle, si->default_attr, length, origin, &number_of_written);
  ASSERT_EQ(length, number_of_written);
}

static void free_screen(struct captured_screen* cs) {
  free(cs->text);
  cs->text = NULL;
  free(cs->attributes);
  cs->attributes = NULL;
}

static void capture_screen(uv_tty_t* tty_out, struct captured_screen* cs) {
  DWORD length;
  COORD origin;
  get_screen_info(tty_out, &(cs->si));
  origin.X = 0;
  origin.Y = cs->si.csbi.srWindow.Top;
  cs->text = malloc(cs->si.length * sizeof(*cs->text));
  ASSERT_NOT_NULL(cs->text);
  cs->attributes = (WORD*) malloc(cs->si.length * sizeof(*cs->attributes));
  ASSERT_NOT_NULL(cs->attributes);
  ASSERT(ReadConsoleOutputCharacter(
         tty_out->handle, cs->text, cs->si.length, origin, &length));
  ASSERT_EQ((unsigned int) cs->si.length, length);
  ASSERT(ReadConsoleOutputAttribute(
         tty_out->handle, cs->attributes, cs->si.length, origin, &length));
  ASSERT_EQ((unsigned int) cs->si.length, length);
}

static void make_expect_screen_erase(struct captured_screen* cs,
                                     COORD cursor_position,
                                     int dir,
                                     BOOL entire_screen) {
  /* beginning of line */
  char* start;
  char* end;
  start = cs->text + cs->si.width * (cursor_position.Y - 1);
  if (dir == 0) {
    if (entire_screen) {
      /* erase to end of screen */
      end = cs->text + cs->si.length;
    } else {
      /* erase to end of line */
      end = start + cs->si.width;
    }
    /* erase from postition of cursor */
    start += cursor_position.X - 1;
  } else if (dir == 1) {
    /* erase to position of cursor */
    end = start + cursor_position.X;
    if (entire_screen) {
      /* erase form beginning of screen */
      start = cs->text;
    }
  } else if (dir == 2) {
    if (entire_screen) {
      /* erase form beginning of screen */
      start = cs->text;
      /* erase to end of screen */
      end = cs->text + cs->si.length;
    } else {
      /* erase to end of line */
      end = start + cs->si.width;
    }
  } else {
    ASSERT(FALSE);
  }
  ASSERT_PTR_LT(start, end);
  ASSERT_LE(end - cs->text, cs->si.length);
  for (; start < end; start++) {
    *start = ' ';
  }
}

static void make_expect_screen_write(struct captured_screen* cs,
                                     COORD cursor_position,
                                     const char* text) {
  /* position of cursor */
  char* start;
  start = cs->text + cs->si.width * (cursor_position.Y - 1) +
                cursor_position.X - 1;
  size_t length = strlen(text);
  size_t remain_length = cs->si.length - (cs->text - start);
  length = length > remain_length ? remain_length : length;
  memcpy(start, text, length);
}

static void make_expect_screen_set_attr(struct captured_screen* cs,
                                        COORD cursor_position,
                                        size_t length,
                                        WORD attr) {
  WORD* start;
  start = cs->attributes + cs->si.width * (cursor_position.Y - 1) +
                cursor_position.X - 1;
  size_t remain_length = cs->si.length - (cs->attributes - start);
  length = length > remain_length ? remain_length : length;
  while (length) {
    *start = attr;
    start++;
    length--;
  }
}

static BOOL compare_screen(uv_tty_t* tty_out,
                           struct captured_screen* actual,
                           struct captured_screen* expect) {
  int line, col;
  BOOL result = TRUE;
  int current = 0;
  ASSERT(actual->text);
  ASSERT(actual->attributes);
  ASSERT(expect->text);
  ASSERT(expect->attributes);
  if (actual->si.length != expect->si.length) {
    return FALSE;
  }
  if (actual->si.width != expect->si.width) {
    return FALSE;
  }
  if (actual->si.height != expect->si.height) {
    return FALSE;
  }
  while (current < actual->si.length) {
    if (*(actual->text + current) != *(expect->text + current)) {
      line = current / actual->si.width + 1;
      col = current - actual->si.width * (line - 1) + 1;
      fprintf(stderr,
              "line:%d col:%d expected character '%c' but found '%c'\n",
              line,
              col,
              *(expect->text + current),
              *(actual->text + current));
      result = FALSE;
    }
    if (*(actual->attributes + current) != *(expect->attributes + current)) {
      line = current / actual->si.width + 1;
      col = current - actual->si.width * (line - 1) + 1;
      fprintf(stderr,
              "line:%d col:%d expected attributes '%u' but found '%u'\n",
              line,
              col,
              *(expect->attributes + current),
              *(actual->attributes + current));
      result = FALSE;
    }
    current++;
  }
  clear_screen(tty_out, &expect->si);
  free_screen(expect);
  free_screen(actual);
  return result;
}

static void initialize_tty(uv_tty_t* tty_out) {
  int r;
  int ttyout_fd;
  /* Make sure we have an FD that refers to a tty */
  HANDLE handle;

  uv_tty_set_vterm_state(UV_TTY_UNSUPPORTED);

  handle = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL,
                                     CONSOLE_TEXTMODE_BUFFER,
                                     NULL);
  ASSERT_PTR_NE(handle, INVALID_HANDLE_VALUE);

  ttyout_fd = _open_osfhandle((intptr_t) handle, 0);
  ASSERT_GE(ttyout_fd, 0);
  ASSERT_EQ(UV_TTY, uv_guess_handle(ttyout_fd));
  r = uv_tty_init(uv_default_loop(), tty_out, ttyout_fd, 0); /* Writable. */
  ASSERT_OK(r);
}

static void terminate_tty(uv_tty_t* tty_out) {
  set_cursor_to_home(tty_out);
  uv_close((uv_handle_t*) tty_out, NULL);
}

TEST_IMPL(tty_cursor_up) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* cursor up one times if omitted arguments */
  snprintf(buffer, sizeof(buffer), "%sA", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y - 1, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);

  /* cursor up nth times */
  cursor_pos_old = cursor_pos;
  snprintf(buffer, sizeof(buffer), "%s%dA", CSI, si.height / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y - si.height / 4, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);

  /* cursor up from Window top does nothing */
  cursor_pos_old.X = 1;
  cursor_pos_old.Y = 1;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sA", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);
  ASSERT(!is_scrolling(&tty_out, si));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_cursor_down) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* cursor down one times if omitted arguments */
  snprintf(buffer, sizeof(buffer), "%sB", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y + 1, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);

  /* cursor down nth times */
  cursor_pos_old = cursor_pos;
  snprintf(buffer, sizeof(buffer), "%s%dB", CSI, si.height / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y + si.height / 4, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);

  /* cursor down from bottom line does nothing */
  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sB", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);
  ASSERT(!is_scrolling(&tty_out, si));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_cursor_forward) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* cursor forward one times if omitted arguments */
  snprintf(buffer, sizeof(buffer), "%sC", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X + 1, cursor_pos.X);

  /* cursor forward nth times */
  cursor_pos_old = cursor_pos;
  snprintf(buffer, sizeof(buffer), "%s%dC", CSI, si.width / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X + si.width / 4, cursor_pos.X);

  /* cursor forward from end of line does nothing*/
  cursor_pos_old.X = si.width;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sC", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);

  /* cursor forward from end of screen does nothing */
  cursor_pos_old.X = si.width;
  cursor_pos_old.Y = si.height;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sC", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);
  ASSERT(!is_scrolling(&tty_out, si));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_cursor_back) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* cursor back one times if omitted arguments */
  snprintf(buffer, sizeof(buffer), "%sD", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X - 1, cursor_pos.X);

  /* cursor back nth times */
  cursor_pos_old = cursor_pos;
  snprintf(buffer, sizeof(buffer), "%s%dD", CSI, si.width / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X - si.width / 4, cursor_pos.X);

  /* cursor back from beginning of line does nothing */
  cursor_pos_old.X = 1;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sD", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(cursor_pos_old.X, cursor_pos.X);

  /* cursor back from top of screen does nothing */
  cursor_pos_old.X = 1;
  cursor_pos_old.Y = 1;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sD", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(1, cursor_pos.Y);
  ASSERT_EQ(1, cursor_pos.X);
  ASSERT(!is_scrolling(&tty_out, si));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_cursor_next_line) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* cursor next line one times if omitted arguments */
  snprintf(buffer, sizeof(buffer), "%sE", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y + 1, cursor_pos.Y);
  ASSERT_EQ(1, cursor_pos.X);

  /* cursor next line nth times */
  cursor_pos_old = cursor_pos;
  snprintf(buffer, sizeof(buffer), "%s%dE", CSI, si.height / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y + si.height / 4, cursor_pos.Y);
  ASSERT_EQ(1, cursor_pos.X);

  /* cursor next line from buttom row moves beginning of line */
  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sE", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);
  ASSERT_EQ(1, cursor_pos.X);
  ASSERT(!is_scrolling(&tty_out, si));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_cursor_previous_line) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* cursor previous line one times if omitted arguments */
  snprintf(buffer, sizeof(buffer), "%sF", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y - 1, cursor_pos.Y);
  ASSERT_EQ(1, cursor_pos.X);

  /* cursor previous line nth times */
  cursor_pos_old = cursor_pos;
  snprintf(buffer, sizeof(buffer), "%s%dF", CSI, si.height / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos_old.Y - si.height / 4, cursor_pos.Y);
  ASSERT_EQ(1, cursor_pos.X);

  /* cursor previous line from top of screen does nothing */
  cursor_pos_old.X = 1;
  cursor_pos_old.Y = 1;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer, sizeof(buffer), "%sD", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(1, cursor_pos.Y);
  ASSERT_EQ(1, cursor_pos.X);
  ASSERT(!is_scrolling(&tty_out, si));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_cursor_horizontal_move_absolute) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* Move to beginning of line if omitted argument */
  snprintf(buffer, sizeof(buffer), "%sG", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(1, cursor_pos.X);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);

  /* Move cursor to nth character */
  snprintf(buffer, sizeof(buffer), "%s%dG", CSI, si.width / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(si.width / 4, cursor_pos.X);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);

  /* Moving out of screen will fit within screen */
  snprintf(buffer, sizeof(buffer), "%s%dG", CSI, si.width + 1);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(si.width, cursor_pos.X);
  ASSERT_EQ(cursor_pos_old.Y, cursor_pos.Y);

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_cursor_move_absolute) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos.X = si.width / 2;
  cursor_pos.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos);

  /* Move the cursor to home if omitted arguments */
  snprintf(buffer, sizeof(buffer), "%sH", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(1, cursor_pos.X);
  ASSERT_EQ(1, cursor_pos.Y);

  /* Move the cursor to the middle of the screen */
  snprintf(
      buffer, sizeof(buffer), "%s%d;%df", CSI, si.height / 2, si.width / 2);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(si.width / 2, cursor_pos.X);
  ASSERT_EQ(si.height / 2, cursor_pos.Y);

  /* Moving out of screen will fit within screen */
  snprintf(
      buffer, sizeof(buffer), "%s%d;%df", CSI, si.height / 2, si.width + 1);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(si.width, cursor_pos.X);
  ASSERT_EQ(si.height / 2, cursor_pos.Y);

  snprintf(
      buffer, sizeof(buffer), "%s%d;%df", CSI, si.height + 1, si.width / 2);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(si.width / 2, cursor_pos.X);
  ASSERT_EQ(si.height, cursor_pos.Y);
  ASSERT(!is_scrolling(&tty_out, si));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_hide_show_cursor) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  char buffer[1024];
  BOOL saved_cursor_visibility;

  loop = uv_default_loop();

  initialize_tty(&tty_out);

  saved_cursor_visibility = get_cursor_visibility(&tty_out);

  /* Hide the cursor */
  set_cursor_visibility(&tty_out, TRUE);
  snprintf(buffer, sizeof(buffer), "%s?25l", CSI);
  write_console(&tty_out, buffer);
  ASSERT(!get_cursor_visibility(&tty_out));

  /* Show the cursor */
  set_cursor_visibility(&tty_out, FALSE);
  snprintf(buffer, sizeof(buffer), "%s?25h", CSI);
  write_console(&tty_out, buffer);
  ASSERT(get_cursor_visibility(&tty_out));

  set_cursor_visibility(&tty_out, saved_cursor_visibility);
  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_erase) {
  int dir;
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos;
  char buffer[1024];
  struct captured_screen actual = {0}, expect = {0};

  loop = uv_default_loop();

  initialize_tty(&tty_out);

  /* Erase to below if omitted argument */
  dir = 0;
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  cursor_pos.X = expect.si.width / 2;
  cursor_pos.Y = expect.si.height / 2;
  make_expect_screen_erase(&expect, cursor_pos, dir, TRUE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%sJ", CSI);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Erase to below(dir = 0) */
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  make_expect_screen_erase(&expect, cursor_pos, dir, TRUE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s%dJ", CSI, dir);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Erase to above */
  dir = 1;
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  make_expect_screen_erase(&expect, cursor_pos, dir, TRUE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s%dJ", CSI, dir);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Erase All */
  dir = 2;
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  make_expect_screen_erase(&expect, cursor_pos, dir, TRUE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s%dJ", CSI, dir);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_erase_line) {
  int dir;
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos;
  char buffer[1024];
  struct captured_screen actual = {0}, expect = {0};

  loop = uv_default_loop();

  initialize_tty(&tty_out);

  /* Erase to right if omitted arguments */
  dir = 0;
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  cursor_pos.X = expect.si.width / 2;
  cursor_pos.Y = expect.si.height / 2;
  make_expect_screen_erase(&expect, cursor_pos, dir, FALSE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%sK", CSI);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Erase to right(dir = 0) */
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  make_expect_screen_erase(&expect, cursor_pos, dir, FALSE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s%dK", CSI, dir);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Erase to Left */
  dir = 1;
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  make_expect_screen_erase(&expect, cursor_pos, dir, FALSE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s%dK", CSI, dir);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Erase All */
  dir = 2;
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  make_expect_screen_erase(&expect, cursor_pos, dir, FALSE);

  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s%dK", CSI, dir);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_set_cursor_shape) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  DWORD saved_cursor_size;
  char buffer[1024];

  loop = uv_default_loop();

  initialize_tty(&tty_out);

  saved_cursor_size = get_cursor_size(&tty_out);

  /* cursor size large if omitted arguments */
  set_cursor_size(&tty_out, CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_LARGE);

  /* cursor size large */
  set_cursor_size(&tty_out, CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s1 q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_LARGE);
  set_cursor_size(&tty_out, CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s2 q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_LARGE);

  /* cursor size small */
  set_cursor_size(&tty_out, CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s3 q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_SMALL);
  set_cursor_size(&tty_out, CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s6 q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_SMALL);

  /* Nothing occurs with arguments outside valid range */
  set_cursor_size(&tty_out, CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s7 q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_MIDDLE);

  /* restore cursor size if arguments is zero */
  snprintf(buffer, sizeof(buffer), "%s0 q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), saved_cursor_size);

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_set_style) {
#if _MSC_VER >= 1920 && _MSC_VER <= 1929
  RETURN_SKIP("Broken on Microsoft Visual Studio 2019, to be investigated. "
              "See: https://github.com/libuv/libuv/issues/3304");
#else

  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos;
  char buffer[1024];
  struct captured_screen actual = {0}, expect = {0};
  WORD fg, bg;
  WORD fg_attrs[9][2] = {{F_BLACK, FOREGROUND_BLACK},
                         {F_RED, FOREGROUND_RED},
                         {F_GREEN, FOREGROUND_GREEN},
                         {F_YELLOW, FOREGROUND_YELLOW},
                         {F_BLUE, FOREGROUND_BLUE},
                         {F_MAGENTA, FOREGROUND_MAGENTA},
                         {F_CYAN, FOREGROUND_CYAN},
                         {F_WHITE, FOREGROUND_WHITE},
                         {F_DEFAULT, 0}};
  WORD bg_attrs[9][2] = {{B_DEFAULT, 0},
                         {B_BLACK, BACKGROUND_BLACK},
                         {B_RED, BACKGROUND_RED},
                         {B_GREEN, BACKGROUND_GREEN},
                         {B_YELLOW, BACKGROUND_YELLOW},
                         {B_BLUE, BACKGROUND_BLUE},
                         {B_MAGENTA, BACKGROUND_MAGENTA},
                         {B_CYAN, BACKGROUND_CYAN},
                         {B_WHITE, BACKGROUND_WHITE}};
  WORD attr;
  int i, length;

  loop = uv_default_loop();

  initialize_tty(&tty_out);

  capture_screen(&tty_out, &expect);
  fg_attrs[8][1] = expect.si.default_attr & FOREGROUND_WHITE;
  bg_attrs[0][1] = expect.si.default_attr & BACKGROUND_WHITE;

  /* Set foreground color */
  length = ARRAY_SIZE(fg_attrs);
  for (i = 0; i < length; i++) {
    capture_screen(&tty_out, &expect);
    cursor_pos.X = expect.si.width / 2;
    cursor_pos.Y = expect.si.height / 2;
    attr = (expect.si.default_attr & ~FOREGROUND_WHITE) | fg_attrs[i][1];
    make_expect_screen_write(&expect, cursor_pos, HELLO);
    make_expect_screen_set_attr(&expect, cursor_pos, strlen(HELLO), attr);

    set_cursor_position(&tty_out, cursor_pos);
    snprintf(
        buffer, sizeof(buffer), "%s%dm%s%sm", CSI, fg_attrs[i][0], HELLO, CSI);
    write_console(&tty_out, buffer);
    capture_screen(&tty_out, &actual);

    ASSERT(compare_screen(&tty_out, &actual, &expect));
  }

  /* Set background color */
  length = ARRAY_SIZE(bg_attrs);
  for (i = 0; i < length; i++) {
    capture_screen(&tty_out, &expect);
    cursor_pos.X = expect.si.width / 2;
    cursor_pos.Y = expect.si.height / 2;
    attr = (expect.si.default_attr & ~BACKGROUND_WHITE) | bg_attrs[i][1];
    make_expect_screen_write(&expect, cursor_pos, HELLO);
    make_expect_screen_set_attr(&expect, cursor_pos, strlen(HELLO), attr);

    set_cursor_position(&tty_out, cursor_pos);
    snprintf(
        buffer, sizeof(buffer), "%s%dm%s%sm", CSI, bg_attrs[i][0], HELLO, CSI);
    write_console(&tty_out, buffer);
    capture_screen(&tty_out, &actual);

    ASSERT(compare_screen(&tty_out, &actual, &expect));
  }

  /* Set foreground and background color */
  ASSERT_EQ(ARRAY_SIZE(fg_attrs), ARRAY_SIZE(bg_attrs));
  length = ARRAY_SIZE(bg_attrs);
  for (i = 0; i < length; i++) {
    capture_screen(&tty_out, &expect);
    cursor_pos.X = expect.si.width / 2;
    cursor_pos.Y = expect.si.height / 2;
    attr = expect.si.default_attr & ~FOREGROUND_WHITE & ~BACKGROUND_WHITE;
    attr |= fg_attrs[i][1] | bg_attrs[i][1];
    make_expect_screen_write(&expect, cursor_pos, HELLO);
    make_expect_screen_set_attr(&expect, cursor_pos, strlen(HELLO), attr);

    set_cursor_position(&tty_out, cursor_pos);
    snprintf(buffer,
             sizeof(buffer),
             "%s%d;%dm%s%sm",
             CSI,
             bg_attrs[i][0],
             fg_attrs[i][0],
             HELLO,
             CSI);
    write_console(&tty_out, buffer);
    capture_screen(&tty_out, &actual);

    ASSERT(compare_screen(&tty_out, &actual, &expect));
  }

  /* Set foreground bright on */
  capture_screen(&tty_out, &expect);
  cursor_pos.X = expect.si.width / 2;
  cursor_pos.Y = expect.si.height / 2;
  set_cursor_position(&tty_out, cursor_pos);
  attr = expect.si.default_attr;
  attr |= FOREGROUND_INTENSITY;
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  make_expect_screen_set_attr(&expect, cursor_pos, strlen(HELLO), attr);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  make_expect_screen_set_attr(&expect, cursor_pos, strlen(HELLO), attr);

  snprintf(buffer,
           sizeof(buffer),
           "%s%dm%s%s%dm%s%dm%s%s%dm",
           CSI,
           F_INTENSITY,
           HELLO,
           CSI,
           F_INTENSITY_OFF1,
           CSI,
           F_INTENSITY,
           HELLO,
           CSI,
           F_INTENSITY_OFF2);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Set background bright on */
  capture_screen(&tty_out, &expect);
  cursor_pos.X = expect.si.width / 2;
  cursor_pos.Y = expect.si.height / 2;
  set_cursor_position(&tty_out, cursor_pos);
  attr = expect.si.default_attr;
  attr |= BACKGROUND_INTENSITY;
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  make_expect_screen_set_attr(&expect, cursor_pos, strlen(HELLO), attr);

  snprintf(buffer,
           sizeof(buffer),
           "%s%dm%s%s%dm",
           CSI,
           B_INTENSITY,
           HELLO,
           CSI,
           B_INTENSITY_OFF);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Inverse */
  capture_screen(&tty_out, &expect);
  cursor_pos.X = expect.si.width / 2;
  cursor_pos.Y = expect.si.height / 2;
  set_cursor_position(&tty_out, cursor_pos);
  attr = expect.si.default_attr;
  fg = attr & FOREGROUND_WHITE;
  bg = attr & BACKGROUND_WHITE;
  attr &= (~FOREGROUND_WHITE & ~BACKGROUND_WHITE);
  attr |= COMMON_LVB_REVERSE_VIDEO;
  attr |= fg << 4;
  attr |= bg >> 4;
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  make_expect_screen_set_attr(&expect, cursor_pos, strlen(HELLO), attr);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);

  snprintf(buffer,
           sizeof(buffer),
           "%s%dm%s%s%dm%s",
           CSI,
           INVERSE,
           HELLO,
           CSI,
           INVERSE_OFF,
           HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);

  ASSERT(compare_screen(&tty_out, &actual, &expect));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}


TEST_IMPL(tty_save_restore_cursor_position) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  char buffer[1024];
  struct screen_info si;

  loop = uv_default_loop();

  initialize_tty(&tty_out);
  get_screen_info(&tty_out, &si);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* save the cursor position */
  snprintf(buffer, sizeof(buffer), "%ss", CSI);
  write_console(&tty_out, buffer);

  cursor_pos.X = si.width / 4;
  cursor_pos.Y = si.height / 4;
  set_cursor_position(&tty_out, cursor_pos);

  /* restore the cursor position */
  snprintf(buffer, sizeof(buffer), "%su", CSI);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos.X, cursor_pos_old.X);
  ASSERT_EQ(cursor_pos.Y, cursor_pos_old.Y);

  cursor_pos_old.X = si.width / 2;
  cursor_pos_old.Y = si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);

  /* save the cursor position */
  snprintf(buffer, sizeof(buffer), "%s7", ESC);
  write_console(&tty_out, buffer);

  cursor_pos.X = si.width / 4;
  cursor_pos.Y = si.height / 4;
  set_cursor_position(&tty_out, cursor_pos);

  /* restore the cursor position */
  snprintf(buffer, sizeof(buffer), "%s8", ESC);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(cursor_pos.X, cursor_pos_old.X);
  ASSERT_EQ(cursor_pos.Y, cursor_pos_old.Y);

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_full_reset) {
  uv_tty_t tty_out;
  uv_loop_t* loop;
  char buffer[1024];
  struct captured_screen actual = {0}, expect = {0};
  COORD cursor_pos;
  DWORD saved_cursor_size;
  BOOL saved_cursor_visibility;

  loop = uv_default_loop();

  initialize_tty(&tty_out);

  capture_screen(&tty_out, &expect);
  setup_screen(&tty_out);
  cursor_pos.X = expect.si.width;
  cursor_pos.Y = expect.si.height;
  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s%d;%dm%s", CSI, F_CYAN, B_YELLOW, HELLO);
  saved_cursor_size = get_cursor_size(&tty_out);
  set_cursor_size(&tty_out,
                  saved_cursor_size == CURSOR_SIZE_LARGE ? CURSOR_SIZE_SMALL
                                                         : CURSOR_SIZE_LARGE);
  saved_cursor_visibility = get_cursor_visibility(&tty_out);
  set_cursor_visibility(&tty_out, saved_cursor_visibility ? FALSE : TRUE);
  write_console(&tty_out, buffer);
  snprintf(buffer, sizeof(buffer), "%sc", ESC);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));
  ASSERT_EQ(get_cursor_size(&tty_out), saved_cursor_size);
  ASSERT_EQ(get_cursor_visibility(&tty_out), saved_cursor_visibility);
  ASSERT_OK(actual.si.csbi.srWindow.Top);

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


TEST_IMPL(tty_escape_sequence_processing) {
#if _MSC_VER >= 1920 && _MSC_VER <= 1929
  RETURN_SKIP("Broken on Microsoft Visual Studio 2019, to be investigated. "
              "See: https://github.com/libuv/libuv/issues/3304");
#else
  uv_tty_t tty_out;
  uv_loop_t* loop;
  COORD cursor_pos, cursor_pos_old;
  DWORD saved_cursor_size;
  char buffer[1024];
  struct captured_screen actual = {0}, expect = {0};
  int dir;

  loop = uv_default_loop();

  initialize_tty(&tty_out);

  /* CSI + finally byte does not output anything */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer, sizeof(buffer), "%s@%s%s~%s", CSI, HELLO, CSI, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* CSI(C1) + finally byte does not output anything */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer, sizeof(buffer), "\xC2\x9B@%s\xC2\x9B~%s", HELLO, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* CSI + intermediate byte + finally byte does not output anything */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer, sizeof(buffer), "%s @%s%s/~%s", CSI, HELLO, CSI, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* CSI + parameter byte + finally byte does not output anything */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  snprintf(buffer,
           sizeof(buffer),
           "%s0@%s%s>~%s%s?~%s",
           CSI,
           HELLO,
           CSI,
           HELLO,
           CSI,
           HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* ESC Single-char control does not output anyghing */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer, sizeof(buffer), "%s @%s%s/~%s", CSI, HELLO, CSI, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Nothing is output from ESC + ^, _, P, ] to BEL or ESC \ */
  /* Operaging System Command */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer, sizeof(buffer), "%s]0;%s%s%s", ESC, HELLO, BEL, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));
  /* Device Control Sequence */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer, sizeof(buffer), "%sP$m%s%s", ESC, ST, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));
  /* Privacy Message */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer,
           sizeof(buffer),
           "%s^\"%s\\\"%s\"%s%s",
           ESC,
           HELLO,
           HELLO,
           ST,
           HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));
  /* Application Program Command */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer,
           sizeof(buffer),
           "%s_\"%s%s%s\"%s%s",
           ESC,
           HELLO,
           ST,
           HELLO,
           BEL,
           HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Ignore double escape */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  cursor_pos.X += strlen(HELLO);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer,
           sizeof(buffer),
           "%s%s@%s%s%s~%s",
           ESC,
           CSI,
           HELLO,
           ESC,
           CSI,
           HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Ignored if argument overflow */
  set_cursor_to_home(&tty_out);
  snprintf(buffer, sizeof(buffer), "%s1;%dH", CSI, UINT16_MAX + 1);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(1, cursor_pos.X);
  ASSERT_EQ(1, cursor_pos.Y);

  /* Too many argument are ignored */
  cursor_pos.X = 1;
  cursor_pos.Y = 1;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer,
           sizeof(buffer),
           "%s%d;%d;%d;%d;%dm%s%sm",
           CSI,
           F_RED,
           F_INTENSITY,
           INVERSE,
           B_CYAN,
           B_INTENSITY_OFF,
           HELLO,
           CSI);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* In the case of DECSCUSR, the others are ignored */
  set_cursor_to_home(&tty_out);
  snprintf(buffer,
           sizeof(buffer),
           "%s%d;%d H",
           CSI,
           expect.si.height / 2,
           expect.si.width / 2);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT_EQ(1, cursor_pos.X);
  ASSERT_EQ(1, cursor_pos.Y);

  /* Invalid sequence are ignored */
  saved_cursor_size = get_cursor_size(&tty_out);
  set_cursor_size(&tty_out, CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s 1q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_MIDDLE);
  snprintf(buffer, sizeof(buffer), "%s 1 q", CSI);
  write_console(&tty_out, buffer);
  ASSERT_EQ(get_cursor_size(&tty_out), CURSOR_SIZE_MIDDLE);
  set_cursor_size(&tty_out, saved_cursor_size);

  /* #1874 2. */
  snprintf(buffer, sizeof(buffer), "%s??25l", CSI);
  write_console(&tty_out, buffer);
  ASSERT(get_cursor_visibility(&tty_out));
  snprintf(buffer, sizeof(buffer), "%s25?l", CSI);
  write_console(&tty_out, buffer);
  ASSERT(get_cursor_visibility(&tty_out));
  cursor_pos_old.X = expect.si.width / 2;
  cursor_pos_old.Y = expect.si.height / 2;
  set_cursor_position(&tty_out, cursor_pos_old);
  snprintf(buffer,
           sizeof(buffer),
           "%s??%d;%df",
           CSI,
           expect.si.height / 4,
           expect.si.width / 4);
  write_console(&tty_out, buffer);
  get_cursor_position(&tty_out, &cursor_pos);
  ASSERT(cursor_pos.X = cursor_pos_old.X);
  ASSERT(cursor_pos.Y = cursor_pos_old.Y);
  set_cursor_to_home(&tty_out);

  /* CSI 25 l does nothing (#1874 4.) */
  snprintf(buffer, sizeof(buffer), "%s25l", CSI);
  write_console(&tty_out, buffer);
  ASSERT(get_cursor_visibility(&tty_out));

  /* Unsupported sequences are ignored(#1874 5.) */
  dir = 2;
  setup_screen(&tty_out);
  capture_screen(&tty_out, &expect);
  set_cursor_position(&tty_out, cursor_pos);
  snprintf(buffer, sizeof(buffer), "%s?%dJ", CSI, dir);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  /* Finally byte immedately after CSI [ are also output(#1874 1.) */
  cursor_pos.X = expect.si.width / 2;
  cursor_pos.Y = expect.si.height / 2;
  set_cursor_position(&tty_out, cursor_pos);
  capture_screen(&tty_out, &expect);
  make_expect_screen_write(&expect, cursor_pos, HELLO);
  snprintf(buffer, sizeof(buffer), "%s[%s", CSI, HELLO);
  write_console(&tty_out, buffer);
  capture_screen(&tty_out, &actual);
  ASSERT(compare_screen(&tty_out, &actual, &expect));

  terminate_tty(&tty_out);

  uv_run(loop, UV_RUN_DEFAULT);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
#endif
}

#else

typedef int file_has_no_tests; /* ISO C forbids an empty translation unit. */

#endif  /* ifdef _WIN32 */
