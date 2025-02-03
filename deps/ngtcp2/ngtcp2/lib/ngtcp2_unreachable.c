/*
 * ngtcp2
 *
 * Copyright (c) 2022 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "ngtcp2_unreachable.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#  define NGTCP2_UNREACHABLE_LOG
#  include <unistd.h>
#elif defined(WIN32)
#  define NGTCP2_UNREACHABLE_LOG
#  include <io.h>
#endif /* defined(WIN32) */

void ngtcp2_unreachable_fail(const char *file, int line, const char *func) {
#ifdef NGTCP2_UNREACHABLE_LOG
  char *buf;
  size_t buflen;
  int rv;

#  define NGTCP2_UNREACHABLE_TEMPLATE "%s:%d %s: Unreachable.\n"

  rv = snprintf(NULL, 0, NGTCP2_UNREACHABLE_TEMPLATE, file, line, func);
  if (rv < 0) {
    abort();
  }

  /* here we explicitly use system malloc */
  buflen = (size_t)rv + 1;
  buf = malloc(buflen);
  if (buf == NULL) {
    abort();
  }

  rv = snprintf(buf, buflen, NGTCP2_UNREACHABLE_TEMPLATE, file, line, func);
  if (rv < 0) {
    abort();
  }

#  ifndef WIN32
  while (write(STDERR_FILENO, buf, (size_t)rv) == -1 && errno == EINTR)
    ;
#  else  /* defined(WIN32) */
  _write(_fileno(stderr), buf, (unsigned int)rv);
#  endif /* defined(WIN32) */

  free(buf);
#endif /* defined(NGTCP2_UNREACHABLE_LOG) */

  abort();
}
