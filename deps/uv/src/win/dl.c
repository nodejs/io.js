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
#include "internal.h"

static int uv__dlerror(uv_lib_t* lib, const char* filename, DWORD errorno);


int uv_dlopen(const char* filename, uv_lib_t* lib) {
  WCHAR filename_w[32768];
  ssize_t r;

  lib->handle = NULL;
  lib->errmsg = NULL;

  r = uv_wtf8_length_as_utf16(filename);
  if (r < 0)
    return uv__dlerror(lib, filename, ERROR_NO_UNICODE_TRANSLATION);
  if ((size_t) r > ARRAY_SIZE(filename_w))
    return uv__dlerror(lib, filename, ERROR_INSUFFICIENT_BUFFER);
  uv_wtf8_to_utf16(filename, filename_w, r);

  lib->handle = LoadLibraryExW(filename_w, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (lib->handle == NULL) {
    return uv__dlerror(lib, filename, GetLastError());
  }

  return 0;
}


void uv_dlclose(uv_lib_t* lib) {
  if (lib->errmsg) {
    LocalFree((void*)lib->errmsg);
    lib->errmsg = NULL;
  }

  if (lib->handle) {
    /* Ignore errors. No good way to signal them without leaking memory. */
    FreeLibrary(lib->handle);
    lib->handle = NULL;
  }
}


int uv_dlsym(uv_lib_t* lib, const char* name, void** ptr) {
  /* Cast though integer to suppress pedantic warning about forbidden cast. */
  *ptr = (void*)(uintptr_t) GetProcAddress(lib->handle, name);
  return uv__dlerror(lib, "", *ptr ? 0 : GetLastError());
}


const char* uv_dlerror(const uv_lib_t* lib) {
  return lib->errmsg ? lib->errmsg : "no error";
}


static void uv__format_fallback_error(uv_lib_t* lib, int errorno){
  static const CHAR fallback_error[] = "error: %1!d!";
  DWORD_PTR args[1];
  args[0] = (DWORD_PTR) errorno;

  FormatMessageA(FORMAT_MESSAGE_FROM_STRING |
                 FORMAT_MESSAGE_ARGUMENT_ARRAY |
                 FORMAT_MESSAGE_ALLOCATE_BUFFER,
                 fallback_error, 0, 0,
                 (LPSTR) &lib->errmsg,
                 0, (va_list*) args);
}



static int uv__dlerror(uv_lib_t* lib, const char* filename, DWORD errorno) {
  DWORD_PTR arg;
  DWORD res;
  char* msg;

  if (lib->errmsg) {
    LocalFree(lib->errmsg);
    lib->errmsg = NULL;
  }

  if (errorno == 0)
    return 0;

  res = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                       FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorno,
                       MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                       (LPSTR) &lib->errmsg, 0, NULL);

  if (!res && (GetLastError() == ERROR_MUI_FILE_NOT_FOUND ||
               GetLastError() == ERROR_RESOURCE_TYPE_NOT_FOUND)) {
    res = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                         FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorno,
                         0, (LPSTR) &lib->errmsg, 0, NULL);
  }

  if (res && errorno == ERROR_BAD_EXE_FORMAT && strstr(lib->errmsg, "%1")) {
    msg = lib->errmsg;
    lib->errmsg = NULL;
    arg = (DWORD_PTR) filename;
    res = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                         FORMAT_MESSAGE_ARGUMENT_ARRAY |
                         FORMAT_MESSAGE_FROM_STRING,
                         msg,
                         0, 0, (LPSTR) &lib->errmsg, 0, (va_list*) &arg);
    LocalFree(msg);
  }

  if (!res)
    uv__format_fallback_error(lib, errorno);

  return -1;
}
