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

#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <stdio.h>
#include <process.h>
#if !defined(__MINGW32__)
# include <crtdbg.h>
#endif


#include "task.h"
#include "runner.h"


/*
 * Define the stuff that MinGW doesn't have
 */
#ifndef GetFileSizeEx
  WINBASEAPI BOOL WINAPI GetFileSizeEx(HANDLE hFile,
                                       PLARGE_INTEGER lpFileSize);
#endif


/* Do platform-specific initialization. */
void platform_init(int argc, char **argv) {
  /* Disable the "application crashed" popup. */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
      SEM_NOOPENFILEERRORBOX);
#if !defined(__MINGW32__)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif

  _setmode(0, _O_BINARY);
  _setmode(1, _O_BINARY);
  _setmode(2, _O_BINARY);

#ifdef _MSC_VER
  _set_fmode(_O_BINARY);
#else
  _fmode = _O_BINARY;
#endif

  /* Disable stdio output buffering. */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  strcpy(executable_path, argv[0]);
}


int process_start(char *name, char *part, process_info_t *p, int is_helper) {
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE nul = INVALID_HANDLE_VALUE;
  WCHAR path[MAX_PATH], filename[MAX_PATH];
  WCHAR image[MAX_PATH + 1];
  WCHAR args[MAX_PATH * 2];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  DWORD result;

  if (!is_helper) {
    /* Give the helpers time to settle. Race-y, fix this. */
    uv_sleep(250);
  }

  if (GetTempPathW(sizeof(path) / sizeof(WCHAR), (WCHAR*)&path) == 0)
    goto error;
  if (GetTempFileNameW((WCHAR*)&path, L"uv", 0, (WCHAR*)&filename) == 0)
    goto error;

  file = CreateFileW((WCHAR*)filename,
                     GENERIC_READ | GENERIC_WRITE,
                     0,
                     NULL,
                     CREATE_ALWAYS,
                     FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                     NULL);
  if (file == INVALID_HANDLE_VALUE)
    goto error;

  if (!SetHandleInformation(file, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
    goto error;

  nul = CreateFileA("nul",
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);
  if (nul == INVALID_HANDLE_VALUE)
    goto error;

  if (!SetHandleInformation(nul, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
    goto error;

  result = GetModuleFileNameW(NULL,
                              (WCHAR*) &image,
                              sizeof(image) / sizeof(WCHAR));
  if (result == 0 || result == sizeof(image))
    goto error;

  if (part) {
    if (_snwprintf((WCHAR*)args,
                   sizeof(args) / sizeof(WCHAR),
                   L"\"%s\" %S %S",
                   image,
                   name,
                   part) < 0) {
      goto error;
    }
  } else {
    if (_snwprintf((WCHAR*)args,
                   sizeof(args) / sizeof(WCHAR),
                   L"\"%s\" %S",
                   image,
                   name) < 0) {
      goto error;
    }
  }

  memset((void*)&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = file;
  si.hStdError = file;

  if (!CreateProcessW(image, args, NULL, NULL, TRUE,
                      0, NULL, NULL, &si, &pi))
    goto error;

  CloseHandle(pi.hThread);

  SetHandleInformation(nul, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(file, HANDLE_FLAG_INHERIT, 0);

  p->stdio_in = nul;
  p->stdio_out = file;
  p->process = pi.hProcess;
  p->name = part;

  return 0;

error:
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  if (nul != INVALID_HANDLE_VALUE)
    CloseHandle(nul);

  return -1;
}


/* Timeout is in msecs. Set timeout < 0 to never time out. Returns 0 when all
 * processes are terminated, -2 on timeout. */
int process_wait(process_info_t *vec, int n, int timeout) {
  int i;
  HANDLE handles[MAXIMUM_WAIT_OBJECTS];
  DWORD timeout_api, result;

  /* If there's nothing to wait for, return immediately. */
  if (n == 0)
    return 0;

  ASSERT_LE(n, MAXIMUM_WAIT_OBJECTS);

  for (i = 0; i < n; i++)
    handles[i] = vec[i].process;

  if (timeout >= 0) {
    timeout_api = (DWORD)timeout;
  } else {
    timeout_api = INFINITE;
  }

  result = WaitForMultipleObjects(n, handles, TRUE, timeout_api);

  if (result < WAIT_OBJECT_0 + n) {
    /* All processes are terminated. */
    return 0;
  }
  if (result == WAIT_TIMEOUT) {
    return -2;
  }
  return -1;
}


long int process_output_size(process_info_t *p) {
  LARGE_INTEGER size;
  if (!GetFileSizeEx(p->stdio_out, &size))
    return -1;
  return (long int)size.QuadPart;
}


int process_copy_output(process_info_t* p, FILE* stream) {
  char buf[1024];
  int fd, r;

  fd = _open_osfhandle((intptr_t)p->stdio_out, _O_RDONLY | _O_TEXT);
  if (fd == -1)
    return -1;

  r = _lseek(fd, 0, SEEK_SET);
  if (r < 0)
    return -1;

  while ((r = _read(fd, buf, sizeof(buf))) != 0)
    print_lines(buf, r, stream);

  _close(fd);
  return 0;
}


int process_read_last_line(process_info_t *p,
                           char * buffer,
                           size_t buffer_len) {
  DWORD size;
  DWORD read;
  DWORD start;
  OVERLAPPED overlapped;

  ASSERT_GT(buffer_len, 0);

  size = GetFileSize(p->stdio_out, NULL);
  if (size == INVALID_FILE_SIZE)
    return -1;

  if (size == 0) {
    buffer[0] = '\0';
    return 1;
  }

  memset(&overlapped, 0, sizeof overlapped);
  if (size >= buffer_len)
    overlapped.Offset = size - buffer_len - 1;

  if (!ReadFile(p->stdio_out, buffer, buffer_len - 1, &read, &overlapped))
    return -1;

  start = read;
  while (start-- > 0) {
    if (buffer[start] == '\n' || buffer[start] == '\r')
      break;
  }

  if (start > 0)
    memmove(buffer, buffer + start, read - start);

  buffer[read - start] = '\0';

  return 0;
}


char* process_get_name(process_info_t *p) {
  return p->name;
}


int process_terminate(process_info_t *p) {
  if (!TerminateProcess(p->process, 1))
    return -1;
  return 0;
}


int process_reap(process_info_t *p) {
  DWORD exitCode;
  if (!GetExitCodeProcess(p->process, &exitCode))
    return -1;
  return (int)exitCode;
}


void process_cleanup(process_info_t *p) {
  CloseHandle(p->process);
  CloseHandle(p->stdio_in);
}


static int clear_line(void) {
  HANDLE handle;
  CONSOLE_SCREEN_BUFFER_INFO info;
  COORD coord;
  DWORD written;

  handle = (HANDLE)_get_osfhandle(_fileno(stderr));
  if (handle == INVALID_HANDLE_VALUE)
    return -1;

  if (!GetConsoleScreenBufferInfo(handle, &info))
    return -1;

  coord = info.dwCursorPosition;
  if (coord.Y <= 0)
    return -1;

  coord.X = 0;

  if (!SetConsoleCursorPosition(handle, coord))
    return -1;

  if (!FillConsoleOutputCharacterW(handle,
                                   0x20,
                                   info.dwSize.X,
                                   coord,
                                   &written)) {
    return -1;
  }

  return 0;
}


void rewind_cursor() {
  if (clear_line() == -1) {
    /* If clear_line fails (stdout is not a console), print a newline. */
    fprintf(stderr, "\n");
  }
}
