/* MIT License
 *
 * Copyright (c) 2023 Brad House
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
#include <stdlib.h>

/* Older MacOS versions require including AvailabilityMacros.h before
 * sys/random.h */
#ifdef HAVE_AVAILABILITYMACROS_H
#  include <AvailabilityMacros.h>
#endif

#ifdef HAVE_SYS_RANDOM_H
#  include <sys/random.h>
#endif


typedef enum {
  ARES_RAND_OS   = 1 << 0, /* OS-provided such as RtlGenRandom or arc4random */
  ARES_RAND_FILE = 1 << 1, /* OS file-backed random number generator */
  ARES_RAND_RC4  = 1 << 2  /* Internal RC4 based PRNG */
} ares_rand_backend;

#define ARES_RC4_KEY_LEN 32 /* 256 bits */

typedef struct ares_rand_rc4 {
  unsigned char S[256];
  size_t        i;
  size_t        j;
} ares_rand_rc4;

static unsigned int ares_u32_from_ptr(void *addr)
{
  /* LCOV_EXCL_START: FallbackCode */
  if (ares_is_64bit()) {
    return (unsigned int)((((ares_uint64_t)addr >> 32) & 0xFFFFFFFF) |
                          ((ares_uint64_t)addr & 0xFFFFFFFF));
  }
  return (unsigned int)((size_t)addr & 0xFFFFFFFF);
  /* LCOV_EXCL_STOP */
}

/* initialize an rc4 key as the last possible fallback. */
static void ares_rc4_generate_key(ares_rand_rc4 *rc4_state, unsigned char *key,
                                  size_t key_len)
{
  /* LCOV_EXCL_START: FallbackCode */
  size_t         i;
  size_t         len = 0;
  unsigned int   data;
  ares_timeval_t tv;

  if (key_len != ARES_RC4_KEY_LEN) {
    return;
  }

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  /* For fuzzing, random should be deterministic */
  srand(0);
#else
  /* Randomness is hard to come by.  Maybe the system randomizes heap and stack
   * addresses. Maybe the current timestamp give us some randomness. Use
   * rc4_state (heap), &i (stack), and ares_tvnow()
   */
  data = ares_u32_from_ptr(rc4_state);
  memcpy(key + len, &data, sizeof(data));
  len += sizeof(data);

  data = ares_u32_from_ptr(&i);
  memcpy(key + len, &data, sizeof(data));
  len += sizeof(data);

  ares_tvnow(&tv);
  data = (unsigned int)((tv.sec | tv.usec) & 0xFFFFFFFF);
  memcpy(key + len, &data, sizeof(data));
  len += sizeof(data);

  srand(ares_u32_from_ptr(rc4_state) | ares_u32_from_ptr(&i) |
        (unsigned int)((tv.sec | tv.usec) & 0xFFFFFFFF));
#endif

  for (i = len; i < key_len; i++) {
    key[i] = (unsigned char)(rand() % 256); /* LCOV_EXCL_LINE */
  }
  /* LCOV_EXCL_STOP */
}

#define ARES_SWAP_BYTE(a, b)           \
  do {                                 \
    unsigned char swapByte = *(a);     \
    *(a)                   = *(b);     \
    *(b)                   = swapByte; \
  } while (0)

static void ares_rc4_init(ares_rand_rc4 *rc4_state)
{
  /* LCOV_EXCL_START: FallbackCode */
  unsigned char key[ARES_RC4_KEY_LEN];
  size_t        i;
  size_t        j;

  ares_rc4_generate_key(rc4_state, key, sizeof(key));

  for (i = 0; i < sizeof(rc4_state->S); i++) {
    rc4_state->S[i] = i & 0xFF;
  }

  for (i = 0, j = 0; i < 256; i++) {
    j = (j + rc4_state->S[i] + key[i % sizeof(key)]) % 256;
    ARES_SWAP_BYTE(&rc4_state->S[i], &rc4_state->S[j]);
  }

  rc4_state->i = 0;
  rc4_state->j = 0;
  /* LCOV_EXCL_STOP */
}

/* Just outputs the key schedule, no need to XOR with any data since we have
 * none */
static void ares_rc4_prng(ares_rand_rc4 *rc4_state, unsigned char *buf,
                          size_t len)
{
  /* LCOV_EXCL_START: FallbackCode */
  unsigned char *S = rc4_state->S;
  size_t         i = rc4_state->i;
  size_t         j = rc4_state->j;
  size_t         cnt;

  for (cnt = 0; cnt < len; cnt++) {
    i = (i + 1) % 256;
    j = (j + S[i]) % 256;

    ARES_SWAP_BYTE(&S[i], &S[j]);
    buf[cnt] = S[(S[i] + S[j]) % 256];
  }

  rc4_state->i = i;
  rc4_state->j = j;
  /* LCOV_EXCL_STOP */
}

struct ares_rand_state {
  ares_rand_backend type;
  ares_rand_backend bad_backends;

  union {
    FILE         *rand_file;
    ares_rand_rc4 rc4;
  } state;

  /* Since except for RC4, random data will likely result in a syscall, lets
   * pre-pull 256 bytes at a time.  Every query will pull 2 bytes off this so
   * that means we should only need a syscall every 128 queries. 256bytes
   * appears to be a sweet spot that may be able to be served without
   * interruption */
  unsigned char cache[256];
  size_t        cache_remaining;
};

/* Define RtlGenRandom = SystemFunction036.  This is in advapi32.dll.  There is
 * no need to dynamically load this, other software used widely does not.
 * http://blogs.msdn.com/michael_howard/archive/2005/01/14/353379.aspx
 * https://docs.microsoft.com/en-us/windows/win32/api/ntsecapi/nf-ntsecapi-rtlgenrandom
 */
#ifdef _WIN32
BOOLEAN WINAPI SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);
#  ifndef RtlGenRandom
#    define RtlGenRandom(a, b) SystemFunction036(a, b)
#  endif
#endif


static ares_bool_t ares_init_rand_engine(ares_rand_state *state)
{
  state->cache_remaining = 0;

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  /* For fuzzing, random should be deterministic */
  state->bad_backends |= ARES_RAND_OS | ARES_RAND_FILE;
#endif

#if defined(HAVE_ARC4RANDOM_BUF) || defined(HAVE_GETRANDOM) || defined(_WIN32)
  if (!(state->bad_backends & ARES_RAND_OS)) {
    state->type = ARES_RAND_OS;
    return ARES_TRUE;
  }
#endif

#if defined(CARES_RANDOM_FILE)
  /* LCOV_EXCL_START: FallbackCode */
  if (!(state->bad_backends & ARES_RAND_FILE)) {
    state->type            = ARES_RAND_FILE;
    state->state.rand_file = fopen(CARES_RANDOM_FILE, "rb");
    if (state->state.rand_file) {
      setvbuf(state->state.rand_file, NULL, _IONBF, 0);
      return ARES_TRUE;
    }
  }
  /* LCOV_EXCL_STOP */

  /* Fall-Thru on failure to RC4 */
#endif

  /* LCOV_EXCL_START: FallbackCode */
  state->type = ARES_RAND_RC4;
  ares_rc4_init(&state->state.rc4);
  /* LCOV_EXCL_STOP */

  /* Currently cannot fail */
  return ARES_TRUE; /* LCOV_EXCL_LINE: UntestablePath */
}

ares_rand_state *ares_init_rand_state(void)
{
  ares_rand_state *state = NULL;

  state = ares_malloc_zero(sizeof(*state));
  if (!state) {
    return NULL;
  }

  if (!ares_init_rand_engine(state)) {
    ares_free(state); /* LCOV_EXCL_LINE: UntestablePath */
    return NULL;      /* LCOV_EXCL_LINE: UntestablePath */
  }

  return state;
}

static void ares_clear_rand_state(ares_rand_state *state)
{
  if (!state) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  switch (state->type) {
    case ARES_RAND_OS:
      break;
    /* LCOV_EXCL_START: FallbackCode */
    case ARES_RAND_FILE:
      fclose(state->state.rand_file);
      break;
    case ARES_RAND_RC4:
      break;
      /* LCOV_EXCL_STOP */
  }
}

static void ares_reinit_rand(ares_rand_state *state)
{
  /* LCOV_EXCL_START: UntestablePath */
  ares_clear_rand_state(state);
  ares_init_rand_engine(state);
  /* LCOV_EXCL_STOP */
}

void ares_destroy_rand_state(ares_rand_state *state)
{
  if (!state) {
    return;
  }

  ares_clear_rand_state(state);
  ares_free(state);
}

static void ares_rand_bytes_fetch(ares_rand_state *state, unsigned char *buf,
                                  size_t len)
{
  while (1) {
    size_t bytes_read = 0;

    switch (state->type) {
      case ARES_RAND_OS:
#ifdef _WIN32
        RtlGenRandom(buf, (ULONG)len);
        return;
#elif defined(HAVE_ARC4RANDOM_BUF)
        arc4random_buf(buf, len);
        return;
#elif defined(HAVE_GETRANDOM)
        while (1) {
          size_t  n = len - bytes_read;
          /* getrandom() on Linux always succeeds and is never
           * interrupted by a signal when requesting <= 256 bytes.
           */
          ssize_t rv = getrandom(buf + bytes_read, n > 256 ? 256 : n, 0);
          if (rv <= 0) {
            /* We need to fall back to another backend */
            if (errno == ENOSYS) {
              state->bad_backends |= ARES_RAND_OS;
              break;
            }
            continue; /* Just retry. */
          }

          bytes_read += (size_t)rv;
          if (bytes_read == len) {
            return;
          }
        }
        break;
#else
        /* Shouldn't be possible to be here */
        break;
#endif

        /* LCOV_EXCL_START: FallbackCode */

      case ARES_RAND_FILE:
        while (1) {
          size_t rv = fread(buf + bytes_read, 1, len - bytes_read,
                            state->state.rand_file);
          if (rv == 0) {
            break; /* critical error, will reinit rand state */
          }

          bytes_read += rv;
          if (bytes_read == len) {
            return;
          }
        }
        break;

      case ARES_RAND_RC4:
        ares_rc4_prng(&state->state.rc4, buf, len);
        return;

        /* LCOV_EXCL_STOP */
    }

    /* If we didn't return before we got here, that means we had a critical rand
     * failure and need to reinitialized */
    ares_reinit_rand(state); /* LCOV_EXCL_LINE: UntestablePath */
  }
}

void ares_rand_bytes(ares_rand_state *state, unsigned char *buf, size_t len)
{
  /* See if we need to refill the cache to serve the request, but if len is
   * excessive, we're not going to update our cache or serve from cache */
  if (len > state->cache_remaining && len < sizeof(state->cache)) {
    size_t fetch_size = sizeof(state->cache) - state->cache_remaining;
    ares_rand_bytes_fetch(state, state->cache, fetch_size);
    state->cache_remaining = sizeof(state->cache);
  }

  /* Serve from cache */
  if (len <= state->cache_remaining) {
    size_t offset = sizeof(state->cache) - state->cache_remaining;
    memcpy(buf, state->cache + offset, len);
    state->cache_remaining -= len;
    return;
  }

  /* Serve direct due to excess size of request */
  ares_rand_bytes_fetch(state, buf, len);
}

unsigned short ares_generate_new_id(ares_rand_state *state)
{
  unsigned short r = 0;

  ares_rand_bytes(state, (unsigned char *)&r, sizeof(r));
  return r;
}
