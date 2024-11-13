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

/*
 * This file is private to libuv. It provides common functionality to both
 * Windows and Unix backends.
 */

#ifndef UV_COMMON_H_
#define UV_COMMON_H_

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "uv.h"
#include "uv/tree.h"
#include "queue.h"
#include "strscpy.h"

#ifndef _MSC_VER
# include <stdatomic.h>
#endif

#if EDOM > 0
# define UV__ERR(x) (-(x))
#else
# define UV__ERR(x) (x)
#endif

#if !defined(snprintf) && defined(_MSC_VER) && _MSC_VER < 1900
extern int snprintf(char*, size_t, const char*, ...);
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARRAY_END(a)  ((a) + ARRAY_SIZE(a))

#define container_of(ptr, type, member) \
  ((type *) ((char *) (ptr) - offsetof(type, member)))

/* C11 defines static_assert to be a macro which calls _Static_assert. */
#if defined(static_assert)
#define STATIC_ASSERT(expr) static_assert(expr, #expr)
#else
#define STATIC_ASSERT(expr)                                                   \
  void uv__static_assert(int static_assert_failed[1 - 2 * !(expr)])
#endif

#ifdef _MSC_VER
#define uv__exchange_int_relaxed(p, v)                                        \
  InterlockedExchangeNoFence((LONG volatile*)(p), v)
#else
#define uv__exchange_int_relaxed(p, v)                                        \
  atomic_exchange_explicit((_Atomic int*)(p), v, memory_order_relaxed)
#endif

#define UV__UDP_DGRAM_MAXSIZE (64 * 1024)

/* Handle flags. Some flags are specific to Windows or UNIX. */
enum {
  /* Used by all handles. */
  UV_HANDLE_CLOSING                     = 0x00000001,
  UV_HANDLE_CLOSED                      = 0x00000002,
  UV_HANDLE_ACTIVE                      = 0x00000004,
  UV_HANDLE_REF                         = 0x00000008,
  UV_HANDLE_INTERNAL                    = 0x00000010,
  UV_HANDLE_ENDGAME_QUEUED              = 0x00000020,

  /* Used by streams. */
  UV_HANDLE_LISTENING                   = 0x00000040,
  UV_HANDLE_CONNECTION                  = 0x00000080,
  UV_HANDLE_SHUT                        = 0x00000200,
  UV_HANDLE_READ_PARTIAL                = 0x00000400,
  UV_HANDLE_READ_EOF                    = 0x00000800,

  /* Used by streams and UDP handles. */
  UV_HANDLE_READING                     = 0x00001000,
  UV_HANDLE_BOUND                       = 0x00002000,
  UV_HANDLE_READABLE                    = 0x00004000,
  UV_HANDLE_WRITABLE                    = 0x00008000,
  UV_HANDLE_READ_PENDING                = 0x00010000,
  UV_HANDLE_SYNC_BYPASS_IOCP            = 0x00020000,
  UV_HANDLE_ZERO_READ                   = 0x00040000,
  UV_HANDLE_EMULATE_IOCP                = 0x00080000,
  UV_HANDLE_BLOCKING_WRITES             = 0x00100000,
  UV_HANDLE_CANCELLATION_PENDING        = 0x00200000,

  /* Used by uv_tcp_t and uv_udp_t handles */
  UV_HANDLE_IPV6                        = 0x00400000,

  /* Only used by uv_tcp_t handles. */
  UV_HANDLE_TCP_NODELAY                 = 0x01000000,
  UV_HANDLE_TCP_KEEPALIVE               = 0x02000000,
  UV_HANDLE_TCP_SINGLE_ACCEPT           = 0x04000000,
  UV_HANDLE_TCP_ACCEPT_STATE_CHANGING   = 0x08000000,
  UV_HANDLE_SHARED_TCP_SOCKET           = 0x10000000,

  /* Only used by uv_udp_t handles. */
  UV_HANDLE_UDP_PROCESSING              = 0x01000000,
  UV_HANDLE_UDP_CONNECTED               = 0x02000000,
  UV_HANDLE_UDP_RECVMMSG                = 0x04000000,

  /* Only used by uv_pipe_t handles. */
  UV_HANDLE_NON_OVERLAPPED_PIPE         = 0x01000000,
  UV_HANDLE_PIPESERVER                  = 0x02000000,

  /* Only used by uv_tty_t handles. */
  UV_HANDLE_TTY_READABLE                = 0x01000000,
  UV_HANDLE_TTY_RAW                     = 0x02000000,
  UV_HANDLE_TTY_SAVED_POSITION          = 0x04000000,
  UV_HANDLE_TTY_SAVED_ATTRIBUTES        = 0x08000000,

  /* Only used by uv_signal_t handles. */
  UV_SIGNAL_ONE_SHOT_DISPATCHED         = 0x01000000,
  UV_SIGNAL_ONE_SHOT                    = 0x02000000,

  /* Only used by uv_poll_t handles. */
  UV_HANDLE_POLL_SLOW                   = 0x01000000,

  /* Only used by uv_process_t handles. */
  UV_HANDLE_REAP                        = 0x10000000
};

int uv__loop_configure(uv_loop_t* loop, uv_loop_option option, va_list ap);

void uv__loop_close(uv_loop_t* loop);

int uv__read_start(uv_stream_t* stream,
                   uv_alloc_cb alloc_cb,
                   uv_read_cb read_cb);

int uv__tcp_bind(uv_tcp_t* tcp,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags);

int uv__tcp_connect(uv_connect_t* req,
                   uv_tcp_t* handle,
                   const struct sockaddr* addr,
                   unsigned int addrlen,
                   uv_connect_cb cb);

int uv__udp_init_ex(uv_loop_t* loop,
                    uv_udp_t* handle,
                    unsigned flags,
                    int domain);

int uv__udp_bind(uv_udp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int  addrlen,
                 unsigned int flags);

int uv__udp_connect(uv_udp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen);

int uv__udp_disconnect(uv_udp_t* handle);

int uv__udp_is_connected(uv_udp_t* handle);

int uv__udp_send(uv_udp_send_t* req,
                 uv_udp_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 uv_udp_send_cb send_cb);

int uv__udp_try_send(uv_udp_t* handle,
                     const uv_buf_t bufs[],
                     unsigned int nbufs,
                     const struct sockaddr* addr,
                     unsigned int addrlen);

int uv__udp_recv_start(uv_udp_t* handle, uv_alloc_cb alloccb,
                       uv_udp_recv_cb recv_cb);

int uv__udp_recv_stop(uv_udp_t* handle);

void uv__fs_poll_close(uv_fs_poll_t* handle);

int uv__getaddrinfo_translate_error(int sys_err);    /* EAI_* error. */

enum uv__work_kind {
  UV__WORK_CPU,
  UV__WORK_FAST_IO,
  UV__WORK_SLOW_IO
};

void uv__work_submit(uv_loop_t* loop,
                     struct uv__work *w,
                     enum uv__work_kind kind,
                     void (*work)(struct uv__work *w),
                     void (*done)(struct uv__work *w, int status));

void uv__work_done(uv_async_t* handle);

size_t uv__count_bufs(const uv_buf_t bufs[], unsigned int nbufs);

int uv__socket_sockopt(uv_handle_t* handle, int optname, int* value);

void uv__fs_scandir_cleanup(uv_fs_t* req);
void uv__fs_readdir_cleanup(uv_fs_t* req);
uv_dirent_type_t uv__fs_get_dirent_type(uv__dirent_t* dent);

int uv__next_timeout(const uv_loop_t* loop);
void uv__run_timers(uv_loop_t* loop);
void uv__timer_close(uv_timer_t* handle);

void uv__process_title_cleanup(void);
void uv__signal_cleanup(void);
void uv__threadpool_cleanup(void);

#define uv__has_active_reqs(loop)                                             \
  ((loop)->active_reqs.count > 0)

#define uv__req_register(loop)                                                \
  do {                                                                        \
    (loop)->active_reqs.count++;                                              \
  }                                                                           \
  while (0)

#define uv__req_unregister(loop)                                              \
  do {                                                                        \
    assert(uv__has_active_reqs(loop));                                        \
    (loop)->active_reqs.count--;                                              \
  }                                                                           \
  while (0)

#define uv__has_active_handles(loop)                                          \
  ((loop)->active_handles > 0)

#define uv__active_handle_add(h)                                              \
  do {                                                                        \
    (h)->loop->active_handles++;                                              \
  }                                                                           \
  while (0)

#define uv__active_handle_rm(h)                                               \
  do {                                                                        \
    (h)->loop->active_handles--;                                              \
  }                                                                           \
  while (0)

#define uv__is_active(h)                                                      \
  (((h)->flags & UV_HANDLE_ACTIVE) != 0)

#define uv__is_closing(h)                                                     \
  (((h)->flags & (UV_HANDLE_CLOSING | UV_HANDLE_CLOSED)) != 0)

#if defined(_WIN32)
# define uv__is_stream_shutting(h)                                            \
  (h->stream.conn.shutdown_req != NULL)
#else
# define uv__is_stream_shutting(h)                                            \
  (h->shutdown_req != NULL)
#endif

#define uv__handle_start(h)                                                   \
  do {                                                                        \
    if (((h)->flags & UV_HANDLE_ACTIVE) != 0) break;                          \
    (h)->flags |= UV_HANDLE_ACTIVE;                                           \
    if (((h)->flags & UV_HANDLE_REF) != 0) uv__active_handle_add(h);          \
  }                                                                           \
  while (0)

#define uv__handle_stop(h)                                                    \
  do {                                                                        \
    if (((h)->flags & UV_HANDLE_ACTIVE) == 0) break;                          \
    (h)->flags &= ~UV_HANDLE_ACTIVE;                                          \
    if (((h)->flags & UV_HANDLE_REF) != 0) uv__active_handle_rm(h);           \
  }                                                                           \
  while (0)

#define uv__handle_ref(h)                                                     \
  do {                                                                        \
    if (((h)->flags & UV_HANDLE_REF) != 0) break;                             \
    (h)->flags |= UV_HANDLE_REF;                                              \
    if (((h)->flags & UV_HANDLE_CLOSING) != 0) break;                         \
    if (((h)->flags & UV_HANDLE_ACTIVE) != 0) uv__active_handle_add(h);       \
  }                                                                           \
  while (0)

#define uv__handle_unref(h)                                                   \
  do {                                                                        \
    if (((h)->flags & UV_HANDLE_REF) == 0) break;                             \
    (h)->flags &= ~UV_HANDLE_REF;                                             \
    if (((h)->flags & UV_HANDLE_CLOSING) != 0) break;                         \
    if (((h)->flags & UV_HANDLE_ACTIVE) != 0) uv__active_handle_rm(h);        \
  }                                                                           \
  while (0)

#define uv__has_ref(h)                                                        \
  (((h)->flags & UV_HANDLE_REF) != 0)

#if defined(_WIN32)
# define uv__handle_platform_init(h) ((h)->u.fd = -1)
#else
# define uv__handle_platform_init(h) ((h)->next_closing = NULL)
#endif

#define uv__handle_init(loop_, h, type_)                                      \
  do {                                                                        \
    (h)->loop = (loop_);                                                      \
    (h)->type = (type_);                                                      \
    (h)->flags = UV_HANDLE_REF;  /* Ref the loop when active. */              \
    uv__queue_insert_tail(&(loop_)->handle_queue, &(h)->handle_queue);        \
    uv__handle_platform_init(h);                                              \
  }                                                                           \
  while (0)

/* Note: uses an open-coded version of SET_REQ_SUCCESS() because of
 * a circular dependency between src/uv-common.h and src/win/internal.h.
 */
#if defined(_WIN32)
# define UV_REQ_INIT(req, typ)                                                \
  do {                                                                        \
    (req)->type = (typ);                                                      \
    (req)->u.io.overlapped.Internal = 0;  /* SET_REQ_SUCCESS() */             \
  }                                                                           \
  while (0)
#else
# define UV_REQ_INIT(req, typ)                                                \
  do {                                                                        \
    (req)->type = (typ);                                                      \
  }                                                                           \
  while (0)
#endif

#define uv__req_init(loop, req, typ)                                          \
  do {                                                                        \
    UV_REQ_INIT(req, typ);                                                    \
    uv__req_register(loop);                                                   \
  }                                                                           \
  while (0)

#define uv__get_internal_fields(loop)                                         \
  ((uv__loop_internal_fields_t*) loop->internal_fields)

#define uv__get_loop_metrics(loop)                                            \
  (&uv__get_internal_fields(loop)->loop_metrics)

#define uv__metrics_inc_loop_count(loop)                                      \
  do {                                                                        \
    uv__get_loop_metrics(loop)->metrics.loop_count++;                         \
  } while (0)

#define uv__metrics_inc_events(loop, e)                                       \
  do {                                                                        \
    uv__get_loop_metrics(loop)->metrics.events += (e);                        \
  } while (0)

#define uv__metrics_inc_events_waiting(loop, e)                               \
  do {                                                                        \
    uv__get_loop_metrics(loop)->metrics.events_waiting += (e);                \
  } while (0)

/* Allocator prototypes */
void *uv__calloc(size_t count, size_t size);
char *uv__strdup(const char* s);
char *uv__strndup(const char* s, size_t n);
void* uv__malloc(size_t size);
void uv__free(void* ptr);
void* uv__realloc(void* ptr, size_t size);
void* uv__reallocf(void* ptr, size_t size);

typedef struct uv__loop_metrics_s uv__loop_metrics_t;
typedef struct uv__loop_internal_fields_s uv__loop_internal_fields_t;

struct uv__loop_metrics_s {
  uv_metrics_t metrics;
  uint64_t provider_entry_time;
  uint64_t provider_idle_time;
  uv_mutex_t lock;
};

void uv__metrics_update_idle_time(uv_loop_t* loop);
void uv__metrics_set_provider_entry_time(uv_loop_t* loop);

#ifdef __linux__
struct uv__iou {
  uint32_t* sqhead;
  uint32_t* sqtail;
  uint32_t sqmask;
  uint32_t* sqflags;
  uint32_t* cqhead;
  uint32_t* cqtail;
  uint32_t cqmask;
  void* sq;   /* pointer to munmap() on event loop teardown */
  void* cqe;  /* pointer to array of struct uv__io_uring_cqe */
  void* sqe;  /* pointer to array of struct uv__io_uring_sqe */
  size_t sqlen;
  size_t cqlen;
  size_t maxlen;
  size_t sqelen;
  int ringfd;
  uint32_t in_flight;
};
#endif  /* __linux__ */

struct uv__loop_internal_fields_s {
  unsigned int flags;
  uv__loop_metrics_t loop_metrics;
  int current_timeout;
#ifdef __linux__
  struct uv__iou ctl;
  struct uv__iou iou;
  void* inv;  /* used by uv__platform_invalidate_fd() */
#endif  /* __linux__ */
};

#endif /* UV_COMMON_H_ */
