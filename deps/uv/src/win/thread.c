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
#include <limits.h>
#include <stdlib.h>

#if defined(__MINGW64_VERSION_MAJOR)
/* MemoryBarrier expands to __mm_mfence in some cases (x86+sse2), which may
 * require this header in some versions of mingw64. */
#include <intrin.h>
#endif

#include "uv.h"
#include "internal.h"

typedef void (*uv__once_cb)(void);

typedef struct {
  uv__once_cb callback;
} uv__once_data_t;

static BOOL WINAPI uv__once_inner(INIT_ONCE *once, void* param, void** context) {
  uv__once_data_t* data = param;

  data->callback();

  return TRUE;
}

void uv_once(uv_once_t* guard, uv__once_cb callback) {
  uv__once_data_t data = { .callback = callback };
  InitOnceExecuteOnce(&guard->init_once, uv__once_inner, (void*) &data, NULL);
}


/* Verify that uv_thread_t can be stored in a TLS slot. */
STATIC_ASSERT(sizeof(uv_thread_t) <= sizeof(void*));

static uv_key_t uv__current_thread_key;
static uv_once_t uv__current_thread_init_guard = UV_ONCE_INIT;


static void uv__init_current_thread_key(void) {
  if (uv_key_create(&uv__current_thread_key))
    abort();
}


struct thread_ctx {
  void (*entry)(void* arg);
  void* arg;
  uv_thread_t self;
};


static UINT __stdcall uv__thread_start(void* arg) {
  struct thread_ctx *ctx_p;
  struct thread_ctx ctx;

  ctx_p = arg;
  ctx = *ctx_p;
  uv__free(ctx_p);

  uv_once(&uv__current_thread_init_guard, uv__init_current_thread_key);
  uv_key_set(&uv__current_thread_key, ctx.self);

  ctx.entry(ctx.arg);

  return 0;
}


int uv_thread_create(uv_thread_t *tid, void (*entry)(void *arg), void *arg) {
  uv_thread_options_t params;
  params.flags = UV_THREAD_NO_FLAGS;
  return uv_thread_create_ex(tid, &params, entry, arg);
}


int uv_thread_detach(uv_thread_t *tid) {
  if (CloseHandle(*tid) == 0)
    return uv_translate_sys_error(GetLastError());

  return 0;
}


int uv_thread_create_ex(uv_thread_t* tid,
                        const uv_thread_options_t* params,
                        void (*entry)(void *arg),
                        void *arg) {
  struct thread_ctx* ctx;
  int err;
  HANDLE thread;
  SYSTEM_INFO sysinfo;
  size_t stack_size;
  size_t pagesize;

  stack_size =
      params->flags & UV_THREAD_HAS_STACK_SIZE ? params->stack_size : 0;

  if (stack_size != 0) {
    GetNativeSystemInfo(&sysinfo);
    pagesize = (size_t)sysinfo.dwPageSize;
    /* Round up to the nearest page boundary. */
    stack_size = (stack_size + pagesize - 1) &~ (pagesize - 1);

    if ((unsigned)stack_size != stack_size)
      return UV_EINVAL;
  }

  ctx = uv__malloc(sizeof(*ctx));
  if (ctx == NULL)
    return UV_ENOMEM;

  ctx->entry = entry;
  ctx->arg = arg;

  /* Create the thread in suspended state so we have a chance to pass
   * its own creation handle to it */
  thread = (HANDLE) _beginthreadex(NULL,
                                   (unsigned)stack_size,
                                   uv__thread_start,
                                   ctx,
                                   CREATE_SUSPENDED,
                                   NULL);
  if (thread == NULL) {
    err = errno;
    uv__free(ctx);
  } else {
    err = 0;
    *tid = thread;
    ctx->self = thread;
    ResumeThread(thread);
  }

  switch (err) {
    case 0:
      return 0;
    case EACCES:
      return UV_EACCES;
    case EAGAIN:
      return UV_EAGAIN;
    case EINVAL:
      return UV_EINVAL;
  }

  return UV_EIO;
}

int uv_thread_setaffinity(uv_thread_t* tid,
                          char* cpumask,
                          char* oldmask,
                          size_t mask_size) {
  int i;
  HANDLE hproc;
  DWORD_PTR procmask;
  DWORD_PTR sysmask;
  DWORD_PTR threadmask;
  DWORD_PTR oldthreadmask;
  int cpumasksize;

  cpumasksize = uv_cpumask_size();
  assert(cpumasksize > 0);
  if (mask_size < (size_t)cpumasksize)
    return UV_EINVAL;

  hproc = GetCurrentProcess();
  if (!GetProcessAffinityMask(hproc, &procmask, &sysmask))
    return uv_translate_sys_error(GetLastError());

  threadmask = 0;
  for (i = 0; i < cpumasksize; i++) {
    if (cpumask[i]) {
      if (procmask & (1 << i))
        threadmask |= 1 << i;
      else
        return UV_EINVAL;
    }
  }

  oldthreadmask = SetThreadAffinityMask(*tid, threadmask);
  if (oldthreadmask == 0)
    return uv_translate_sys_error(GetLastError());

  if (oldmask != NULL) {
    for (i = 0; i < cpumasksize; i++)
      oldmask[i] = (oldthreadmask >> i) & 1;
  }

  return 0;
}

int uv_thread_getaffinity(uv_thread_t* tid,
                          char* cpumask,
                          size_t mask_size) {
  int i;
  HANDLE hproc;
  DWORD_PTR procmask;
  DWORD_PTR sysmask;
  DWORD_PTR threadmask;
  int cpumasksize;

  cpumasksize = uv_cpumask_size();
  assert(cpumasksize > 0);
  if (mask_size < (size_t)cpumasksize)
    return UV_EINVAL;

  hproc = GetCurrentProcess();
  if (!GetProcessAffinityMask(hproc, &procmask, &sysmask))
    return uv_translate_sys_error(GetLastError());

  threadmask = SetThreadAffinityMask(*tid, procmask);
  if (threadmask == 0 || SetThreadAffinityMask(*tid, threadmask) == 0)
    return uv_translate_sys_error(GetLastError());

  for (i = 0; i < cpumasksize; i++)
    cpumask[i] = (threadmask >> i) & 1;

  return 0;
}

int uv_thread_getcpu(void) {
  return GetCurrentProcessorNumber();
}

uv_thread_t uv_thread_self(void) {
  uv_thread_t key;
  uv_once(&uv__current_thread_init_guard, uv__init_current_thread_key);
  key = uv_key_get(&uv__current_thread_key);
  if (key == NULL) {
      /* If the thread wasn't started by uv_thread_create (such as the main
       * thread), we assign an id to it now. */
      if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                           GetCurrentProcess(), &key, 0,
                           FALSE, DUPLICATE_SAME_ACCESS)) {
          uv_fatal_error(GetLastError(), "DuplicateHandle");
      }
      uv_key_set(&uv__current_thread_key, key);
  }
  return key;
}


int uv_thread_join(uv_thread_t *tid) {
  if (WaitForSingleObject(*tid, INFINITE))
    return uv_translate_sys_error(GetLastError());
  else {
    CloseHandle(*tid);
    *tid = 0;
    MemoryBarrier();  /* For feature parity with pthread_join(). */
    return 0;
  }
}


int uv_thread_equal(const uv_thread_t* t1, const uv_thread_t* t2) {
  return *t1 == *t2;
}


int uv_thread_setname(const char* name) {
  HRESULT hr;
  WCHAR* namew;
  int err;
  char namebuf[UV_PTHREAD_MAX_NAMELEN_NP];

  if (name == NULL)
    return UV_EINVAL;

  strncpy(namebuf, name, sizeof(namebuf) - 1);
  namebuf[sizeof(namebuf) - 1] = '\0';

  namew = NULL;
  err = uv__convert_utf8_to_utf16(namebuf, &namew);
  if (err)
    return err;

  hr = SetThreadDescription(GetCurrentThread(), namew);
  uv__free(namew);
  if (FAILED(hr))
    return uv_translate_sys_error(HRESULT_CODE(hr));

  return 0;
}


int uv_thread_getname(uv_thread_t* tid, char* name, size_t size) {
  HRESULT hr;
  WCHAR* namew;
  char* thread_name;
  size_t buf_size;
  int r;
  DWORD exit_code;

  if (name == NULL || size == 0)
    return UV_EINVAL;

  if (tid == NULL || *tid == NULL)
    return UV_EINVAL;

  /* Check if the thread handle is valid */
  if (!GetExitCodeThread(*tid, &exit_code) || exit_code != STILL_ACTIVE)
    return UV_ENOENT;

  namew = NULL;
  thread_name = NULL;
  hr = GetThreadDescription(*tid, &namew);
  if (FAILED(hr))
    return uv_translate_sys_error(HRESULT_CODE(hr));

  buf_size = size;
  r = uv__copy_utf16_to_utf8(namew, -1, name, &buf_size);
  if (r == UV_ENOBUFS) {
    r = uv__convert_utf16_to_utf8(namew, wcslen(namew), &thread_name);
    if (r == 0) {
      uv__strscpy(name, thread_name, size);
      uv__free(thread_name);
    }
  }

  LocalFree(namew);
  return r;
}


int uv_mutex_init(uv_mutex_t* mutex) {
  InitializeCriticalSection(mutex);
  return 0;
}


int uv_mutex_init_recursive(uv_mutex_t* mutex) {
  return uv_mutex_init(mutex);
}


void uv_mutex_destroy(uv_mutex_t* mutex) {
  DeleteCriticalSection(mutex);
}


void uv_mutex_lock(uv_mutex_t* mutex) {
  EnterCriticalSection(mutex);
}


int uv_mutex_trylock(uv_mutex_t* mutex) {
  if (TryEnterCriticalSection(mutex))
    return 0;
  else
    return UV_EBUSY;
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
  LeaveCriticalSection(mutex);
}

/* Ensure that the ABI for this type remains stable in v1.x */
#ifdef _WIN64
STATIC_ASSERT(sizeof(uv_rwlock_t) == 80);
#else
STATIC_ASSERT(sizeof(uv_rwlock_t) == 48);
#endif

int uv_rwlock_init(uv_rwlock_t* rwlock) {
  memset(rwlock, 0, sizeof(*rwlock));
  InitializeSRWLock(&rwlock->read_write_lock_);

  return 0;
}


void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
  /* SRWLock does not need explicit destruction so long as there are no waiting threads
     See: https://docs.microsoft.com/windows/win32/api/synchapi/nf-synchapi-initializesrwlock#remarks */
}


void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
  AcquireSRWLockShared(&rwlock->read_write_lock_);
}


int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  if (!TryAcquireSRWLockShared(&rwlock->read_write_lock_))
    return UV_EBUSY;

  return 0;
}


void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
  ReleaseSRWLockShared(&rwlock->read_write_lock_);
}


void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
  AcquireSRWLockExclusive(&rwlock->read_write_lock_);
}


int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  if (!TryAcquireSRWLockExclusive(&rwlock->read_write_lock_))
    return UV_EBUSY;

  return 0;
}


void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
  ReleaseSRWLockExclusive(&rwlock->read_write_lock_);
}


int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  *sem = CreateSemaphore(NULL, value, INT_MAX, NULL);
  if (*sem == NULL)
    return uv_translate_sys_error(GetLastError());
  else
    return 0;
}


void uv_sem_destroy(uv_sem_t* sem) {
  if (!CloseHandle(*sem))
    abort();
}


void uv_sem_post(uv_sem_t* sem) {
  if (!ReleaseSemaphore(*sem, 1, NULL))
    abort();
}


void uv_sem_wait(uv_sem_t* sem) {
  if (WaitForSingleObject(*sem, INFINITE) != WAIT_OBJECT_0)
    abort();
}


int uv_sem_trywait(uv_sem_t* sem) {
  DWORD r = WaitForSingleObject(*sem, 0);

  if (r == WAIT_OBJECT_0)
    return 0;

  if (r == WAIT_TIMEOUT)
    return UV_EAGAIN;

  abort();
  return -1; /* Satisfy the compiler. */
}


int uv_cond_init(uv_cond_t* cond) {
  InitializeConditionVariable(&cond->cond_var);
  return 0;
}


void uv_cond_destroy(uv_cond_t* cond) {
  /* nothing to do */
  (void) &cond;
}


void uv_cond_signal(uv_cond_t* cond) {
  WakeConditionVariable(&cond->cond_var);
}


void uv_cond_broadcast(uv_cond_t* cond) {
  WakeAllConditionVariable(&cond->cond_var);
}


void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
  if (!SleepConditionVariableCS(&cond->cond_var, mutex, INFINITE))
    abort();
}


int uv_cond_timedwait(uv_cond_t* cond, uv_mutex_t* mutex, uint64_t timeout) {
  if (SleepConditionVariableCS(&cond->cond_var, mutex, (DWORD)(timeout / 1e6)))
    return 0;
  if (GetLastError() != ERROR_TIMEOUT)
    abort();
  return UV_ETIMEDOUT;
}


int uv_key_create(uv_key_t* key) {
  key->tls_index = TlsAlloc();
  if (key->tls_index == TLS_OUT_OF_INDEXES)
    return UV_ENOMEM;
  return 0;
}


void uv_key_delete(uv_key_t* key) {
  if (TlsFree(key->tls_index) == FALSE)
    abort();
  key->tls_index = TLS_OUT_OF_INDEXES;
}


void* uv_key_get(uv_key_t* key) {
  void* value;

  value = TlsGetValue(key->tls_index);
  if (value == NULL)
    if (GetLastError() != ERROR_SUCCESS)
      abort();

  return value;
}


void uv_key_set(uv_key_t* key, void* value) {
  if (TlsSetValue(key->tls_index, value) == FALSE)
    abort();
}
