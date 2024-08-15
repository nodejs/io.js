/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) The c-ares project and its contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ares_private.h"
#include "ares_strcasecmp.h"

#ifndef HAVE_STRCASECMP
int ares_strcasecmp(const char *a, const char *b)
{
#  if defined(HAVE_STRCMPI)
  return strcmpi(a, b);
#  elif defined(HAVE_STRICMP)
  return stricmp(a, b);
#  else
  size_t i;

  for (i = 0; i < (size_t)-1; i++) {
    int c1 = ares__tolower(a[i]);
    int c2 = ares__tolower(b[i]);
    if (c1 != c2) {
      return c1 - c2;
    }
    if (!c1) {
      break;
    }
  }
  return 0;
#  endif
}
#endif

#ifndef HAVE_STRNCASECMP
int ares_strncasecmp(const char *a, const char *b, size_t n)
{
#  if defined(HAVE_STRNCMPI)
  return strncmpi(a, b, n);
#  elif defined(HAVE_STRNICMP)
  return strnicmp(a, b, n);
#  else
  size_t i;

  for (i = 0; i < n; i++) {
    int c1 = ares__tolower(a[i]);
    int c2 = ares__tolower(b[i]);
    if (c1 != c2) {
      return c1 - c2;
    }
    if (!c1) {
      break;
    }
  }
  return 0;
#  endif
}
#endif
