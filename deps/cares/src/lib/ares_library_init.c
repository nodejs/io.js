/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) 2004 Daniel Stenberg
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

/* library-private global and unique instance vars */

#if defined(ANDROID) || defined(__ANDROID__)
#  include "ares_android.h"
#endif

/* library-private global vars with source visibility restricted to this file */

static unsigned int ares_initialized;
static int          ares_init_flags;

/* library-private global vars with visibility across the whole library */

/* Some systems may return either NULL or a valid pointer on malloc(0).  c-ares
 * should never call malloc(0) so lets return NULL so we're more likely to find
 * an issue if it were to occur. */

static void        *default_malloc(size_t size)
{
  if (size == 0) {
    return NULL;
  }
  return malloc(size);
}

#if defined(_WIN32)
/* We need indirections to handle Windows DLL rules. */
static void *default_realloc(void *p, size_t size)
{
  return realloc(p, size);
}

static void default_free(void *p)
{
  free(p);
}
#else
#  define default_realloc realloc
#  define default_free    free
#endif
void *(*ares_malloc)(size_t size)             = default_malloc;
void *(*ares_realloc)(void *ptr, size_t size) = default_realloc;
void (*ares_free)(void *ptr)                  = default_free;

void *ares_malloc_zero(size_t size)
{
  void *ptr = ares_malloc(size);
  if (ptr != NULL) {
    memset(ptr, 0, size);
  }

  return ptr;
}

void *ares_realloc_zero(void *ptr, size_t orig_size, size_t new_size)
{
  void *p = ares_realloc(ptr, new_size);
  if (p == NULL) {
    return NULL;
  }

  if (new_size > orig_size) {
    memset((unsigned char *)p + orig_size, 0, new_size - orig_size);
  }

  return p;
}

int ares_library_init(int flags)
{
  if (ares_initialized) {
    ares_initialized++;
    return ARES_SUCCESS;
  }
  ares_initialized++;

  /* NOTE: ARES_LIB_INIT_WIN32 flag no longer used */

  ares_init_flags = flags;

  return ARES_SUCCESS;
}

int ares_library_init_mem(int flags, void *(*amalloc)(size_t size),
                          void (*afree)(void *ptr),
                          void *(*arealloc)(void *ptr, size_t size))
{
  if (amalloc) {
    ares_malloc = amalloc;
  }
  if (arealloc) {
    ares_realloc = arealloc;
  }
  if (afree) {
    ares_free = afree;
  }
  return ares_library_init(flags);
}

void ares_library_cleanup(void)
{
  if (!ares_initialized) {
    return;
  }
  ares_initialized--;
  if (ares_initialized) {
    return;
  }

  /* NOTE: ARES_LIB_INIT_WIN32 flag no longer used */

#if defined(ANDROID) || defined(__ANDROID__)
  ares_library_cleanup_android();
#endif

  ares_init_flags = ARES_LIB_INIT_NONE;
  ares_malloc     = malloc;
  ares_realloc    = realloc;
  ares_free       = free;
}

int ares_library_initialized(void)
{
#ifdef USE_WINSOCK
  if (!ares_initialized) {
    return ARES_ENOTINITIALIZED;
  }
#endif
  return ARES_SUCCESS;
}
