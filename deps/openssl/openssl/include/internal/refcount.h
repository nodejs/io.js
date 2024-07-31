/*
 * Copyright 2016-2023 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */
#ifndef OSSL_INTERNAL_REFCOUNT_H
# define OSSL_INTERNAL_REFCOUNT_H
# pragma once

# include <openssl/e_os2.h>
# include <openssl/trace.h>

# if defined(OPENSSL_THREADS) && !defined(OPENSSL_DEV_NO_ATOMICS)
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L \
      && !defined(__STDC_NO_ATOMICS__)
#   include <stdatomic.h>
#   define HAVE_C11_ATOMICS
#  endif

#  if defined(HAVE_C11_ATOMICS) && defined(ATOMIC_INT_LOCK_FREE) \
      && ATOMIC_INT_LOCK_FREE > 0

#   define HAVE_ATOMICS 1

typedef _Atomic int CRYPTO_REF_COUNT;

static inline int CRYPTO_UP_REF(_Atomic int *val, int *ret,
                                ossl_unused void *lock)
{
    *ret = atomic_fetch_add_explicit(val, 1, memory_order_relaxed) + 1;
    return 1;
}

/*
 * Changes to shared structure other than reference counter have to be
 * serialized. And any kind of serialization implies a release fence. This
 * means that by the time reference counter is decremented all other
 * changes are visible on all processors. Hence decrement itself can be
 * relaxed. In case it hits zero, object will be destructed. Since it's
 * last use of the object, destructor programmer might reason that access
 * to mutable members doesn't have to be serialized anymore, which would
 * otherwise imply an acquire fence. Hence conditional acquire fence...
 */
static inline int CRYPTO_DOWN_REF(_Atomic int *val, int *ret,
                                  ossl_unused void *lock)
{
    *ret = atomic_fetch_sub_explicit(val, 1, memory_order_relaxed) - 1;
    if (*ret == 0)
        atomic_thread_fence(memory_order_acquire);
    return 1;
}

#  elif defined(__GNUC__) && defined(__ATOMIC_RELAXED) && __GCC_ATOMIC_INT_LOCK_FREE > 0

#   define HAVE_ATOMICS 1

typedef int CRYPTO_REF_COUNT;

static __inline__ int CRYPTO_UP_REF(int *val, int *ret, ossl_unused void *lock)
{
    *ret = __atomic_fetch_add(val, 1, __ATOMIC_RELAXED) + 1;
    return 1;
}

static __inline__ int CRYPTO_DOWN_REF(int *val, int *ret,
                                      ossl_unused void *lock)
{
    *ret = __atomic_fetch_sub(val, 1, __ATOMIC_RELAXED) - 1;
    if (*ret == 0)
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return 1;
}
#  elif defined(__ICL) && defined(_WIN32)
#   define HAVE_ATOMICS 1
typedef volatile int CRYPTO_REF_COUNT;

static __inline int CRYPTO_UP_REF(volatile int *val, int *ret,
                                  ossl_unused void *lock)
{
    *ret = _InterlockedExchangeAdd((void *)val, 1) + 1;
    return 1;
}

static __inline int CRYPTO_DOWN_REF(volatile int *val, int *ret,
                                    ossl_unused void *lock)
{
    *ret = _InterlockedExchangeAdd((void *)val, -1) - 1;
    return 1;
}

#  elif defined(_MSC_VER) && _MSC_VER>=1200

#   define HAVE_ATOMICS 1

typedef volatile int CRYPTO_REF_COUNT;

#   if (defined(_M_ARM) && _M_ARM>=7 && !defined(_WIN32_WCE)) || defined(_M_ARM64)
#    include <intrin.h>
#    if defined(_M_ARM64) && !defined(_ARM_BARRIER_ISH)
#     define _ARM_BARRIER_ISH _ARM64_BARRIER_ISH
#    endif

static __inline int CRYPTO_UP_REF(volatile int *val, int *ret,
                                  ossl_unused void *lock)
{
    *ret = _InterlockedExchangeAdd_nf(val, 1) + 1;
    return 1;
}

static __inline int CRYPTO_DOWN_REF(volatile int *val, int *ret,
                                    ossl_unused void *lock)
{
    *ret = _InterlockedExchangeAdd_nf(val, -1) - 1;
    if (*ret == 0)
        __dmb(_ARM_BARRIER_ISH);
    return 1;
}
#   else
#    if !defined(_WIN32_WCE)
#     pragma intrinsic(_InterlockedExchangeAdd)
#    else
#     if _WIN32_WCE >= 0x600
       extern long __cdecl _InterlockedExchangeAdd(long volatile*, long);
#     else
       /* under Windows CE we still have old-style Interlocked* functions */
       extern long __cdecl InterlockedExchangeAdd(long volatile*, long);
#      define _InterlockedExchangeAdd InterlockedExchangeAdd
#     endif
#    endif

static __inline int CRYPTO_UP_REF(volatile int *val, int *ret,
                                  ossl_unused void *lock)
{
    *ret = _InterlockedExchangeAdd((long volatile *)val, 1) + 1;
    return 1;
}

static __inline int CRYPTO_DOWN_REF(volatile int *val, int *ret,
                                    ossl_unused void *lock)
{
    *ret = _InterlockedExchangeAdd((long volatile *)val, -1) - 1;
    return 1;
}
#   endif

#  endif
# endif  /* !OPENSSL_DEV_NO_ATOMICS */

/*
 * All the refcounting implementations above define HAVE_ATOMICS, so if it's
 * still undefined here (such as when OPENSSL_DEV_NO_ATOMICS is defined), it
 * means we need to implement a fallback.  This fallback uses locks.
 */
# ifndef HAVE_ATOMICS

typedef int CRYPTO_REF_COUNT;

# define CRYPTO_UP_REF(val, ret, lock) CRYPTO_atomic_add(val, 1, ret, lock)
# define CRYPTO_DOWN_REF(val, ret, lock) CRYPTO_atomic_add(val, -1, ret, lock)

# endif

# if !defined(NDEBUG) && !defined(OPENSSL_NO_STDIO)
#  define REF_ASSERT_ISNT(test) \
    (void)((test) ? (OPENSSL_die("refcount error", __FILE__, __LINE__), 1) : 0)
# else
#  define REF_ASSERT_ISNT(i)
# endif

# define REF_PRINT_EX(text, count, object) \
    OSSL_TRACE3(REF_COUNT, "%p:%4d:%s\n", (object), (count), (text));
# define REF_PRINT_COUNT(text, object) \
    REF_PRINT_EX(text, object->references, (void *)object)

#endif
