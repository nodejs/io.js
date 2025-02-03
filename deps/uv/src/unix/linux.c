/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

/* We lean on the fact that POLL{IN,OUT,ERR,HUP} correspond with their
 * EPOLL* counterparts.  We use the POLL* variants in this file because that
 * is what libuv uses elsewhere.
 */

#include "uv.h"
#include "internal.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>  /* offsetof */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <fcntl.h>
#include <ifaddrs.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifndef __NR_io_uring_setup
# define __NR_io_uring_setup 425
#endif

#ifndef __NR_io_uring_enter
# define __NR_io_uring_enter 426
#endif

#ifndef __NR_io_uring_register
# define __NR_io_uring_register 427
#endif

#ifndef __NR_copy_file_range
# if defined(__x86_64__)
#  define __NR_copy_file_range 326
# elif defined(__i386__)
#  define __NR_copy_file_range 377
# elif defined(__s390__)
#  define __NR_copy_file_range 375
# elif defined(__arm__)
#  define __NR_copy_file_range 391
# elif defined(__aarch64__)
#  define __NR_copy_file_range 285
# elif defined(__powerpc__)
#  define __NR_copy_file_range 379
# elif defined(__arc__)
#  define __NR_copy_file_range 285
# elif defined(__riscv)
#  define __NR_copy_file_range 285
# endif
#endif /* __NR_copy_file_range */

#ifndef __NR_statx
# if defined(__x86_64__)
#  define __NR_statx 332
# elif defined(__i386__)
#  define __NR_statx 383
# elif defined(__aarch64__)
#  define __NR_statx 397
# elif defined(__arm__)
#  define __NR_statx 397
# elif defined(__ppc__)
#  define __NR_statx 383
# elif defined(__s390__)
#  define __NR_statx 379
# elif defined(__riscv)
#  define __NR_statx 291
# endif
#endif /* __NR_statx */

#ifndef __NR_getrandom
# if defined(__x86_64__)
#  define __NR_getrandom 318
# elif defined(__i386__)
#  define __NR_getrandom 355
# elif defined(__aarch64__)
#  define __NR_getrandom 384
# elif defined(__arm__)
#  define __NR_getrandom 384
# elif defined(__ppc__)
#  define __NR_getrandom 359
# elif defined(__s390__)
#  define __NR_getrandom 349
# elif defined(__riscv)
#  define __NR_getrandom 278
# endif
#endif /* __NR_getrandom */

enum {
  UV__IORING_SETUP_SQPOLL = 2u,
  UV__IORING_SETUP_NO_SQARRAY = 0x10000u,
};

enum {
  UV__IORING_FEAT_SINGLE_MMAP = 1u,
  UV__IORING_FEAT_NODROP = 2u,
  UV__IORING_FEAT_RSRC_TAGS = 1024u,  /* linux v5.13 */
};

enum {
  UV__IORING_OP_READV = 1,
  UV__IORING_OP_WRITEV = 2,
  UV__IORING_OP_FSYNC = 3,
  UV__IORING_OP_OPENAT = 18,
  UV__IORING_OP_CLOSE = 19,
  UV__IORING_OP_STATX = 21,
  UV__IORING_OP_EPOLL_CTL = 29,
  UV__IORING_OP_RENAMEAT = 35,
  UV__IORING_OP_UNLINKAT = 36,
  UV__IORING_OP_MKDIRAT = 37,
  UV__IORING_OP_SYMLINKAT = 38,
  UV__IORING_OP_LINKAT = 39,
  UV__IORING_OP_FTRUNCATE = 55,
};

enum {
  UV__IORING_ENTER_GETEVENTS = 1u,
  UV__IORING_ENTER_SQ_WAKEUP = 2u,
};

enum {
  UV__IORING_SQ_NEED_WAKEUP = 1u,
  UV__IORING_SQ_CQ_OVERFLOW = 2u,
};

struct uv__io_cqring_offsets {
  uint32_t head;
  uint32_t tail;
  uint32_t ring_mask;
  uint32_t ring_entries;
  uint32_t overflow;
  uint32_t cqes;
  uint64_t reserved0;
  uint64_t reserved1;
};

STATIC_ASSERT(40 == sizeof(struct uv__io_cqring_offsets));

struct uv__io_sqring_offsets {
  uint32_t head;
  uint32_t tail;
  uint32_t ring_mask;
  uint32_t ring_entries;
  uint32_t flags;
  uint32_t dropped;
  uint32_t array;
  uint32_t reserved0;
  uint64_t reserved1;
};

STATIC_ASSERT(40 == sizeof(struct uv__io_sqring_offsets));

struct uv__io_uring_cqe {
  uint64_t user_data;
  int32_t res;
  uint32_t flags;
};

STATIC_ASSERT(16 == sizeof(struct uv__io_uring_cqe));

struct uv__io_uring_sqe {
  uint8_t opcode;
  uint8_t flags;
  uint16_t ioprio;
  int32_t fd;
  union {
    uint64_t off;
    uint64_t addr2;
  };
  union {
    uint64_t addr;
  };
  uint32_t len;
  union {
    uint32_t rw_flags;
    uint32_t fsync_flags;
    uint32_t open_flags;
    uint32_t statx_flags;
  };
  uint64_t user_data;
  union {
    uint16_t buf_index;
    uint64_t pad[3];
  };
};

STATIC_ASSERT(64 == sizeof(struct uv__io_uring_sqe));
STATIC_ASSERT(0 == offsetof(struct uv__io_uring_sqe, opcode));
STATIC_ASSERT(1 == offsetof(struct uv__io_uring_sqe, flags));
STATIC_ASSERT(2 == offsetof(struct uv__io_uring_sqe, ioprio));
STATIC_ASSERT(4 == offsetof(struct uv__io_uring_sqe, fd));
STATIC_ASSERT(8 == offsetof(struct uv__io_uring_sqe, off));
STATIC_ASSERT(16 == offsetof(struct uv__io_uring_sqe, addr));
STATIC_ASSERT(24 == offsetof(struct uv__io_uring_sqe, len));
STATIC_ASSERT(28 == offsetof(struct uv__io_uring_sqe, rw_flags));
STATIC_ASSERT(32 == offsetof(struct uv__io_uring_sqe, user_data));
STATIC_ASSERT(40 == offsetof(struct uv__io_uring_sqe, buf_index));

struct uv__io_uring_params {
  uint32_t sq_entries;
  uint32_t cq_entries;
  uint32_t flags;
  uint32_t sq_thread_cpu;
  uint32_t sq_thread_idle;
  uint32_t features;
  uint32_t reserved[4];
  struct uv__io_sqring_offsets sq_off;  /* 40 bytes */
  struct uv__io_cqring_offsets cq_off;  /* 40 bytes */
};

STATIC_ASSERT(40 + 40 + 40 == sizeof(struct uv__io_uring_params));
STATIC_ASSERT(40 == offsetof(struct uv__io_uring_params, sq_off));
STATIC_ASSERT(80 == offsetof(struct uv__io_uring_params, cq_off));

STATIC_ASSERT(EPOLL_CTL_ADD < 4);
STATIC_ASSERT(EPOLL_CTL_DEL < 4);
STATIC_ASSERT(EPOLL_CTL_MOD < 4);

struct watcher_list {
  RB_ENTRY(watcher_list) entry;
  struct uv__queue watchers;
  int iterating;
  char* path;
  int wd;
};

struct watcher_root {
  struct watcher_list* rbh_root;
};

static int uv__inotify_fork(uv_loop_t* loop, struct watcher_list* root);
static void uv__inotify_read(uv_loop_t* loop,
                             uv__io_t* w,
                             unsigned int revents);
static int compare_watchers(const struct watcher_list* a,
                            const struct watcher_list* b);
static void maybe_free_watcher_list(struct watcher_list* w,
                                    uv_loop_t* loop);

static void uv__epoll_ctl_flush(int epollfd,
                                struct uv__iou* ctl,
                                struct epoll_event (*events)[256]);

static void uv__epoll_ctl_prep(int epollfd,
                               struct uv__iou* ctl,
                               struct epoll_event (*events)[256],
                               int op,
                               int fd,
                               struct epoll_event* e);

RB_GENERATE_STATIC(watcher_root, watcher_list, entry, compare_watchers)


static struct watcher_root* uv__inotify_watchers(uv_loop_t* loop) {
  /* This cast works because watcher_root is a struct with a pointer as its
   * sole member. Such type punning is unsafe in the presence of strict
   * pointer aliasing (and is just plain nasty) but that is why libuv
   * is compiled with -fno-strict-aliasing.
   */
  return (struct watcher_root*) &loop->inotify_watchers;
}


unsigned uv__kernel_version(void) {
  static _Atomic unsigned cached_version;
  struct utsname u;
  unsigned version;
  unsigned major;
  unsigned minor;
  unsigned patch;
  char v_sig[256];
  char* needle;

  version = atomic_load_explicit(&cached_version, memory_order_relaxed);
  if (version != 0)
    return version;

  /* Check /proc/version_signature first as it's the way to get the mainline
   * kernel version in Ubuntu. The format is:
   *   Ubuntu ubuntu_kernel_version mainline_kernel_version
   * For example:
   *   Ubuntu 5.15.0-79.86-generic 5.15.111
   */
  if (0 == uv__slurp("/proc/version_signature", v_sig, sizeof(v_sig)))
    if (3 == sscanf(v_sig, "Ubuntu %*s %u.%u.%u", &major, &minor, &patch))
      goto calculate_version;

  if (-1 == uname(&u))
    return 0;

  /* In Debian we need to check `version` instead of `release` to extract the
   * mainline kernel version. This is an example of how it looks like:
   *  #1 SMP Debian 5.10.46-4 (2021-08-03)
   */
  needle = strstr(u.version, "Debian ");
  if (needle != NULL)
    if (3 == sscanf(needle, "Debian %u.%u.%u", &major, &minor, &patch))
      goto calculate_version;

  if (3 != sscanf(u.release, "%u.%u.%u", &major, &minor, &patch))
    return 0;

  /* Handle it when the process runs under the UNAME26 personality:
   *
   * - kernels >= 3.x identify as 2.6.40+x
   * - kernels >= 4.x identify as 2.6.60+x
   *
   * UNAME26 is a poorly conceived hack that doesn't let us distinguish
   * between 4.x kernels and 5.x/6.x kernels so we conservatively assume
   * that 2.6.60+x means 4.x.
   *
   * Fun fact of the day: it's technically possible to observe the actual
   * kernel version for a brief moment because uname() first copies out the
   * real release string before overwriting it with the backcompat string.
   */
  if (major == 2 && minor == 6) {
    if (patch >= 60) {
      major = 4;
      minor = patch - 60;
      patch = 0;
    } else if (patch >= 40) {
      major = 3;
      minor = patch - 40;
      patch = 0;
    }
  }

calculate_version:
  version = major * 65536 + minor * 256 + patch;
  atomic_store_explicit(&cached_version, version, memory_order_relaxed);

  return version;
}


ssize_t
uv__fs_copy_file_range(int fd_in,
                       off_t* off_in,
                       int fd_out,
                       off_t* off_out,
                       size_t len,
                       unsigned int flags)
{
#ifdef __NR_copy_file_range
  return syscall(__NR_copy_file_range,
                 fd_in,
                 off_in,
                 fd_out,
                 off_out,
                 len,
                 flags);
#else
  return errno = ENOSYS, -1;
#endif
}


int uv__statx(int dirfd,
              const char* path,
              int flags,
              unsigned int mask,
              struct uv__statx* statxbuf) {
#if !defined(__NR_statx) || defined(__ANDROID_API__) && __ANDROID_API__ < 30
  return errno = ENOSYS, -1;
#else
  int rc;

  rc = syscall(__NR_statx, dirfd, path, flags, mask, statxbuf);
  if (rc >= 0)
    uv__msan_unpoison(statxbuf, sizeof(*statxbuf));

  return rc;
#endif
}


ssize_t uv__getrandom(void* buf, size_t buflen, unsigned flags) {
#if !defined(__NR_getrandom) || defined(__ANDROID_API__) && __ANDROID_API__ < 28
  return errno = ENOSYS, -1;
#else
  ssize_t rc;

  rc = syscall(__NR_getrandom, buf, buflen, flags);
  if (rc >= 0)
    uv__msan_unpoison(buf, buflen);

  return rc;
#endif
}


int uv__io_uring_setup(int entries, struct uv__io_uring_params* params) {
  return syscall(__NR_io_uring_setup, entries, params);
}


int uv__io_uring_enter(int fd,
                       unsigned to_submit,
                       unsigned min_complete,
                       unsigned flags) {
  /* io_uring_enter used to take a sigset_t but it's unused
   * in newer kernels unless IORING_ENTER_EXT_ARG is set,
   * in which case it takes a struct io_uring_getevents_arg.
   */
  return syscall(__NR_io_uring_enter,
                 fd,
                 to_submit,
                 min_complete,
                 flags,
                 NULL,
                 0L);
}


int uv__io_uring_register(int fd, unsigned opcode, void* arg, unsigned nargs) {
  return syscall(__NR_io_uring_register, fd, opcode, arg, nargs);
}


static int uv__use_io_uring(uint32_t flags) {
#if defined(__ANDROID_API__)
  return 0;  /* Possibly available but blocked by seccomp. */
#elif defined(__arm__) && __SIZEOF_POINTER__ == 4
  /* See https://github.com/libuv/libuv/issues/4158. */
  return 0;  /* All 32 bits kernels appear buggy. */
#elif defined(__powerpc64__) || defined(__ppc64__)
  /* See https://github.com/libuv/libuv/issues/4283. */
  return 0; /* Random SIGSEGV in signal handler. */
#else
  /* Ternary: unknown=0, yes=1, no=-1 */
  static _Atomic int use_io_uring;
  char* val;
  int use;

#if defined(__hppa__)
  /* io_uring first supported on parisc in 6.1, functional in .51
   * https://lore.kernel.org/all/cb912694-b1fe-dbb0-4d8c-d608f3526905@gmx.de/
   */
  if (uv__kernel_version() < /*6.1.51*/0x060133)
    return 0;
#endif

  /* SQPOLL is all kinds of buggy but epoll batching should work fine. */
  if (0 == (flags & UV__IORING_SETUP_SQPOLL))
    return 1;

  /* Older kernels have a bug where the sqpoll thread uses 100% CPU. */
  if (uv__kernel_version() < /*5.10.186*/0x050ABA)
    return 0;

  use = atomic_load_explicit(&use_io_uring, memory_order_relaxed);

  if (use == 0) {
    val = getenv("UV_USE_IO_URING");
    use = val != NULL && atoi(val) > 0 ? 1 : -1;
    atomic_store_explicit(&use_io_uring, use, memory_order_relaxed);
  }

  return use > 0;
#endif
}


static void uv__iou_init(int epollfd,
                         struct uv__iou* iou,
                         uint32_t entries,
                         uint32_t flags) {
  struct uv__io_uring_params params;
  struct epoll_event e;
  size_t cqlen;
  size_t sqlen;
  size_t maxlen;
  size_t sqelen;
  unsigned kernel_version;
  uint32_t* sqarray;
  uint32_t i;
  char* sq;
  char* sqe;
  int ringfd;
  int no_sqarray;

  sq = MAP_FAILED;
  sqe = MAP_FAILED;

  if (!uv__use_io_uring(flags))
    return;

  kernel_version = uv__kernel_version();
  no_sqarray =
      UV__IORING_SETUP_NO_SQARRAY * (kernel_version >= /* 6.6 */0x060600);

  /* SQPOLL required CAP_SYS_NICE until linux v5.12 relaxed that requirement.
   * Mostly academic because we check for a v5.13 kernel afterwards anyway.
   */
  memset(&params, 0, sizeof(params));
  params.flags = flags | no_sqarray;

  if (flags & UV__IORING_SETUP_SQPOLL)
    params.sq_thread_idle = 10;  /* milliseconds */

  /* Kernel returns a file descriptor with O_CLOEXEC flag set. */
  ringfd = uv__io_uring_setup(entries, &params);
  if (ringfd == -1)
    return;

  /* IORING_FEAT_RSRC_TAGS is used to detect linux v5.13 but what we're
   * actually detecting is whether IORING_OP_STATX works with SQPOLL.
   */
  if (!(params.features & UV__IORING_FEAT_RSRC_TAGS))
    goto fail;

  /* Implied by IORING_FEAT_RSRC_TAGS but checked explicitly anyway. */
  if (!(params.features & UV__IORING_FEAT_SINGLE_MMAP))
    goto fail;

  /* Implied by IORING_FEAT_RSRC_TAGS but checked explicitly anyway. */
  if (!(params.features & UV__IORING_FEAT_NODROP))
    goto fail;

  sqlen = params.sq_off.array + params.sq_entries * sizeof(uint32_t);
  cqlen =
      params.cq_off.cqes + params.cq_entries * sizeof(struct uv__io_uring_cqe);
  maxlen = sqlen < cqlen ? cqlen : sqlen;
  sqelen = params.sq_entries * sizeof(struct uv__io_uring_sqe);

  sq = mmap(0,
            maxlen,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_POPULATE,
            ringfd,
            0);  /* IORING_OFF_SQ_RING */

  sqe = mmap(0,
             sqelen,
             PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_POPULATE,
             ringfd,
             0x10000000ull);  /* IORING_OFF_SQES */

  if (sq == MAP_FAILED || sqe == MAP_FAILED)
    goto fail;

  if (flags & UV__IORING_SETUP_SQPOLL) {
    /* Only interested in completion events. To get notified when
     * the kernel pulls items from the submission ring, add POLLOUT.
     */
    memset(&e, 0, sizeof(e));
    e.events = POLLIN;
    e.data.fd = ringfd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ringfd, &e))
      goto fail;
  }

  iou->sqhead = (uint32_t*) (sq + params.sq_off.head);
  iou->sqtail = (uint32_t*) (sq + params.sq_off.tail);
  iou->sqmask = *(uint32_t*) (sq + params.sq_off.ring_mask);
  iou->sqflags = (uint32_t*) (sq + params.sq_off.flags);
  iou->cqhead = (uint32_t*) (sq + params.cq_off.head);
  iou->cqtail = (uint32_t*) (sq + params.cq_off.tail);
  iou->cqmask = *(uint32_t*) (sq + params.cq_off.ring_mask);
  iou->sq = sq;
  iou->cqe = sq + params.cq_off.cqes;
  iou->sqe = sqe;
  iou->sqlen = sqlen;
  iou->cqlen = cqlen;
  iou->maxlen = maxlen;
  iou->sqelen = sqelen;
  iou->ringfd = ringfd;
  iou->in_flight = 0;

  if (no_sqarray)
    return;

  sqarray = (uint32_t*) (sq + params.sq_off.array);
  for (i = 0; i <= iou->sqmask; i++)
    sqarray[i] = i;  /* Slot -> sqe identity mapping. */

  return;

fail:
  if (sq != MAP_FAILED)
    munmap(sq, maxlen);

  if (sqe != MAP_FAILED)
    munmap(sqe, sqelen);

  uv__close(ringfd);
}


static void uv__iou_delete(struct uv__iou* iou) {
  if (iou->ringfd > -1) {
    munmap(iou->sq, iou->maxlen);
    munmap(iou->sqe, iou->sqelen);
    uv__close(iou->ringfd);
    iou->ringfd = -1;
  }
}


int uv__platform_loop_init(uv_loop_t* loop) {
  uv__loop_internal_fields_t* lfields;

  lfields = uv__get_internal_fields(loop);
  lfields->ctl.ringfd = -1;
  lfields->iou.ringfd = -2;  /* "uninitialized" */

  loop->inotify_watchers = NULL;
  loop->inotify_fd = -1;
  loop->backend_fd = epoll_create1(O_CLOEXEC);

  if (loop->backend_fd == -1)
    return UV__ERR(errno);

  uv__iou_init(loop->backend_fd, &lfields->ctl, 256, 0);

  return 0;
}


int uv__io_fork(uv_loop_t* loop) {
  int err;
  struct watcher_list* root;

  root = uv__inotify_watchers(loop)->rbh_root;

  uv__close(loop->backend_fd);
  loop->backend_fd = -1;

  /* TODO(bnoordhuis) Loses items from the submission and completion rings. */
  uv__platform_loop_delete(loop);

  err = uv__platform_loop_init(loop);
  if (err)
    return err;

  return uv__inotify_fork(loop, root);
}


void uv__platform_loop_delete(uv_loop_t* loop) {
  uv__loop_internal_fields_t* lfields;

  lfields = uv__get_internal_fields(loop);
  uv__iou_delete(&lfields->ctl);
  uv__iou_delete(&lfields->iou);

  if (loop->inotify_fd != -1) {
    uv__io_stop(loop, &loop->inotify_read_watcher, POLLIN);
    uv__close(loop->inotify_fd);
    loop->inotify_fd = -1;
  }
}


struct uv__invalidate {
  struct epoll_event (*prep)[256];
  struct epoll_event* events;
  int nfds;
};


void uv__platform_invalidate_fd(uv_loop_t* loop, int fd) {
  uv__loop_internal_fields_t* lfields;
  struct uv__invalidate* inv;
  struct epoll_event dummy;
  int i;

  lfields = uv__get_internal_fields(loop);
  inv = lfields->inv;

  /* Invalidate events with same file descriptor */
  if (inv != NULL)
    for (i = 0; i < inv->nfds; i++)
      if (inv->events[i].data.fd == fd)
        inv->events[i].data.fd = -1;

  /* Remove the file descriptor from the epoll.
   * This avoids a problem where the same file description remains open
   * in another process, causing repeated junk epoll events.
   *
   * Perform EPOLL_CTL_DEL immediately instead of going through
   * io_uring's submit queue, otherwise the file descriptor may
   * be closed by the time the kernel starts the operation.
   *
   * We pass in a dummy epoll_event, to work around a bug in old kernels.
   *
   * Work around a bug in kernels 3.10 to 3.19 where passing a struct that
   * has the EPOLLWAKEUP flag set generates spurious audit syslog warnings.
   */
  memset(&dummy, 0, sizeof(dummy));
  epoll_ctl(loop->backend_fd, EPOLL_CTL_DEL, fd, &dummy);
}


int uv__io_check_fd(uv_loop_t* loop, int fd) {
  struct epoll_event e;
  int rc;

  memset(&e, 0, sizeof(e));
  e.events = POLLIN;
  e.data.fd = -1;

  rc = 0;
  if (epoll_ctl(loop->backend_fd, EPOLL_CTL_ADD, fd, &e))
    if (errno != EEXIST)
      rc = UV__ERR(errno);

  if (rc == 0)
    if (epoll_ctl(loop->backend_fd, EPOLL_CTL_DEL, fd, &e))
      abort();

  return rc;
}


/* Caller must initialize SQE and call uv__iou_submit(). */
static struct uv__io_uring_sqe* uv__iou_get_sqe(struct uv__iou* iou,
                                                uv_loop_t* loop,
                                                uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  uint32_t head;
  uint32_t tail;
  uint32_t mask;
  uint32_t slot;

  /* Lazily create the ring. State machine: -2 means uninitialized, -1 means
   * initialization failed. Anything else is a valid ring file descriptor.
   */
  if (iou->ringfd == -2) {
    /* By default, the SQPOLL is not created. Enable only if the loop is
     * configured with UV_LOOP_USE_IO_URING_SQPOLL and the UV_USE_IO_URING
     * environment variable is unset or a positive number.
     */
    if (loop->flags & UV_LOOP_ENABLE_IO_URING_SQPOLL)
      if (uv__use_io_uring(UV__IORING_SETUP_SQPOLL))
        uv__iou_init(loop->backend_fd, iou, 64, UV__IORING_SETUP_SQPOLL);

    if (iou->ringfd == -2)
      iou->ringfd = -1;  /* "failed" */
  }

  if (iou->ringfd == -1)
    return NULL;

  head = atomic_load_explicit((_Atomic uint32_t*) iou->sqhead,
                              memory_order_acquire);
  tail = *iou->sqtail;
  mask = iou->sqmask;

  if ((head & mask) == ((tail + 1) & mask))
    return NULL;  /* No room in ring buffer. TODO(bnoordhuis) maybe flush it? */

  slot = tail & mask;
  sqe = iou->sqe;
  sqe = &sqe[slot];
  memset(sqe, 0, sizeof(*sqe));
  sqe->user_data = (uintptr_t) req;

  /* Pacify uv_cancel(). */
  req->work_req.loop = loop;
  req->work_req.work = NULL;
  req->work_req.done = NULL;
  uv__queue_init(&req->work_req.wq);

  uv__req_register(loop);
  iou->in_flight++;

  return sqe;
}


static void uv__iou_submit(struct uv__iou* iou) {
  uint32_t flags;

  atomic_store_explicit((_Atomic uint32_t*) iou->sqtail,
                        *iou->sqtail + 1,
                        memory_order_release);

  flags = atomic_load_explicit((_Atomic uint32_t*) iou->sqflags,
                               memory_order_acquire);

  if (flags & UV__IORING_SQ_NEED_WAKEUP)
    if (uv__io_uring_enter(iou->ringfd, 0, 0, UV__IORING_ENTER_SQ_WAKEUP))
      if (errno != EOWNERDEAD)  /* Kernel bug. Harmless, ignore. */
        perror("libuv: io_uring_enter(wakeup)");  /* Can't happen. */
}


int uv__iou_fs_close(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;
  int kv;

  kv = uv__kernel_version();
  /* Work around a poorly understood bug in older kernels where closing a file
   * descriptor pointing to /foo/bar results in ETXTBSY errors when trying to
   * execve("/foo/bar") later on. The bug seems to have been fixed somewhere
   * between 5.15.85 and 5.15.90. I couldn't pinpoint the responsible commit
   * but good candidates are the several data race fixes. Interestingly, it
   * seems to manifest only when running under Docker so the possibility of
   * a Docker bug can't be completely ruled out either. Yay, computers.
   * Also, disable on non-longterm versions between 5.16.0 (non-longterm) and
   * 6.1.0 (longterm). Starting with longterm 6.1.x, the issue seems to be
   * solved.
   */
  if (kv < /* 5.15.90 */ 0x050F5A)
    return 0;

  if (kv >= /* 5.16.0 */ 0x050A00 && kv < /* 6.1.0 */ 0x060100)
    return 0;


  iou = &uv__get_internal_fields(loop)->iou;

  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->fd = req->file;
  sqe->opcode = UV__IORING_OP_CLOSE;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_ftruncate(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  if (uv__kernel_version() < /* 6.9 */0x060900)
    return 0;

  iou = &uv__get_internal_fields(loop)->iou;
  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->fd = req->file;
  sqe->len = req->off;
  sqe->opcode = UV__IORING_OP_FTRUNCATE;
  uv__iou_submit(iou);

  return 1;
}

int uv__iou_fs_fsync_or_fdatasync(uv_loop_t* loop,
                                  uv_fs_t* req,
                                  uint32_t fsync_flags) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  iou = &uv__get_internal_fields(loop)->iou;

  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  /* Little known fact: setting seq->off and seq->len turns
   * it into an asynchronous sync_file_range() operation.
   */
  sqe->fd = req->file;
  sqe->fsync_flags = fsync_flags;
  sqe->opcode = UV__IORING_OP_FSYNC;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_link(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  if (uv__kernel_version() < /* 5.15.0 */0x050F00)
    return 0;

  iou = &uv__get_internal_fields(loop)->iou;
  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->addr = (uintptr_t) req->path;
  sqe->fd = AT_FDCWD;
  sqe->addr2 = (uintptr_t) req->new_path;
  sqe->len = AT_FDCWD;
  sqe->opcode = UV__IORING_OP_LINKAT;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_mkdir(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  if (uv__kernel_version() < /* 5.15.0 */0x050F00)
    return 0;

  iou = &uv__get_internal_fields(loop)->iou;
  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->addr = (uintptr_t) req->path;
  sqe->fd = AT_FDCWD;
  sqe->len = req->mode;
  sqe->opcode = UV__IORING_OP_MKDIRAT;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_open(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  iou = &uv__get_internal_fields(loop)->iou;

  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->addr = (uintptr_t) req->path;
  sqe->fd = AT_FDCWD;
  sqe->len = req->mode;
  sqe->opcode = UV__IORING_OP_OPENAT;
  sqe->open_flags = req->flags | O_CLOEXEC;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_rename(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  iou = &uv__get_internal_fields(loop)->iou;

  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->addr = (uintptr_t) req->path;
  sqe->fd = AT_FDCWD;
  sqe->addr2 = (uintptr_t) req->new_path;
  sqe->len = AT_FDCWD;
  sqe->opcode = UV__IORING_OP_RENAMEAT;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_symlink(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  if (uv__kernel_version() < /* 5.15.0 */0x050F00)
    return 0;

  iou = &uv__get_internal_fields(loop)->iou;
  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->addr = (uintptr_t) req->path;
  sqe->fd = AT_FDCWD;
  sqe->addr2 = (uintptr_t) req->new_path;
  sqe->opcode = UV__IORING_OP_SYMLINKAT;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_unlink(uv_loop_t* loop, uv_fs_t* req) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  iou = &uv__get_internal_fields(loop)->iou;

  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->addr = (uintptr_t) req->path;
  sqe->fd = AT_FDCWD;
  sqe->opcode = UV__IORING_OP_UNLINKAT;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_read_or_write(uv_loop_t* loop,
                             uv_fs_t* req,
                             int is_read) {
  struct uv__io_uring_sqe* sqe;
  struct uv__iou* iou;

  /* If iovcnt is greater than IOV_MAX, cap it to IOV_MAX on reads and fallback
   * to the threadpool on writes */
  if (req->nbufs > IOV_MAX) {
    if (is_read)
      req->nbufs = IOV_MAX;
    else
      return 0;
  }

  iou = &uv__get_internal_fields(loop)->iou;

  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL)
    return 0;

  sqe->addr = (uintptr_t) req->bufs;
  sqe->fd = req->file;
  sqe->len = req->nbufs;
  sqe->off = req->off < 0 ? -1 : req->off;
  sqe->opcode = is_read ? UV__IORING_OP_READV : UV__IORING_OP_WRITEV;

  uv__iou_submit(iou);

  return 1;
}


int uv__iou_fs_statx(uv_loop_t* loop,
                     uv_fs_t* req,
                     int is_fstat,
                     int is_lstat) {
  struct uv__io_uring_sqe* sqe;
  struct uv__statx* statxbuf;
  struct uv__iou* iou;

  statxbuf = uv__malloc(sizeof(*statxbuf));
  if (statxbuf == NULL)
    return 0;

  iou = &uv__get_internal_fields(loop)->iou;

  sqe = uv__iou_get_sqe(iou, loop, req);
  if (sqe == NULL) {
    uv__free(statxbuf);
    return 0;
  }

  req->ptr = statxbuf;

  sqe->addr = (uintptr_t) req->path;
  sqe->addr2 = (uintptr_t) statxbuf;
  sqe->fd = AT_FDCWD;
  sqe->len = 0xFFF; /* STATX_BASIC_STATS + STATX_BTIME */
  sqe->opcode = UV__IORING_OP_STATX;

  if (is_fstat) {
    sqe->addr = (uintptr_t) "";
    sqe->fd = req->file;
    sqe->statx_flags |= 0x1000; /* AT_EMPTY_PATH */
  }

  if (is_lstat)
    sqe->statx_flags |= AT_SYMLINK_NOFOLLOW;

  uv__iou_submit(iou);

  return 1;
}


void uv__statx_to_stat(const struct uv__statx* statxbuf, uv_stat_t* buf) {
  buf->st_dev = makedev(statxbuf->stx_dev_major, statxbuf->stx_dev_minor);
  buf->st_mode = statxbuf->stx_mode;
  buf->st_nlink = statxbuf->stx_nlink;
  buf->st_uid = statxbuf->stx_uid;
  buf->st_gid = statxbuf->stx_gid;
  buf->st_rdev = makedev(statxbuf->stx_rdev_major, statxbuf->stx_rdev_minor);
  buf->st_ino = statxbuf->stx_ino;
  buf->st_size = statxbuf->stx_size;
  buf->st_blksize = statxbuf->stx_blksize;
  buf->st_blocks = statxbuf->stx_blocks;
  buf->st_atim.tv_sec = statxbuf->stx_atime.tv_sec;
  buf->st_atim.tv_nsec = statxbuf->stx_atime.tv_nsec;
  buf->st_mtim.tv_sec = statxbuf->stx_mtime.tv_sec;
  buf->st_mtim.tv_nsec = statxbuf->stx_mtime.tv_nsec;
  buf->st_ctim.tv_sec = statxbuf->stx_ctime.tv_sec;
  buf->st_ctim.tv_nsec = statxbuf->stx_ctime.tv_nsec;
  buf->st_birthtim.tv_sec = statxbuf->stx_btime.tv_sec;
  buf->st_birthtim.tv_nsec = statxbuf->stx_btime.tv_nsec;
  buf->st_flags = 0;
  buf->st_gen = 0;
}


static void uv__iou_fs_statx_post(uv_fs_t* req) {
  struct uv__statx* statxbuf;
  uv_stat_t* buf;

  buf = &req->statbuf;
  statxbuf = req->ptr;
  req->ptr = NULL;

  if (req->result == 0) {
    uv__msan_unpoison(statxbuf, sizeof(*statxbuf));
    uv__statx_to_stat(statxbuf, buf);
    req->ptr = buf;
  }

  uv__free(statxbuf);
}


static void uv__poll_io_uring(uv_loop_t* loop, struct uv__iou* iou) {
  struct uv__io_uring_cqe* cqe;
  struct uv__io_uring_cqe* e;
  uv_fs_t* req;
  uint32_t head;
  uint32_t tail;
  uint32_t mask;
  uint32_t i;
  uint32_t flags;
  int nevents;
  int rc;

  head = *iou->cqhead;
  tail = atomic_load_explicit((_Atomic uint32_t*) iou->cqtail,
                              memory_order_acquire);
  mask = iou->cqmask;
  cqe = iou->cqe;
  nevents = 0;

  for (i = head; i != tail; i++) {
    e = &cqe[i & mask];

    req = (uv_fs_t*) (uintptr_t) e->user_data;
    assert(req->type == UV_FS);

    uv__req_unregister(loop);
    iou->in_flight--;

    /* If the op is not supported by the kernel retry using the thread pool */
    if (e->res == -EOPNOTSUPP) {
      uv__fs_post(loop, req);
      continue;
    }

    /* io_uring stores error codes as negative numbers, same as libuv. */
    req->result = e->res;

    switch (req->fs_type) {
      case UV_FS_FSTAT:
      case UV_FS_LSTAT:
      case UV_FS_STAT:
        uv__iou_fs_statx_post(req);
        break;
      default:  /* Squelch -Wswitch warnings. */
        break;
    }

    uv__metrics_update_idle_time(loop);
    req->cb(req);
    nevents++;
  }

  atomic_store_explicit((_Atomic uint32_t*) iou->cqhead,
                        tail,
                        memory_order_release);

  /* Check whether CQE's overflowed, if so enter the kernel to make them
   * available. Don't grab them immediately but in the next loop iteration to
   * avoid loop starvation. */
  flags = atomic_load_explicit((_Atomic uint32_t*) iou->sqflags,
                               memory_order_acquire);

  if (flags & UV__IORING_SQ_CQ_OVERFLOW) {
    do
      rc = uv__io_uring_enter(iou->ringfd, 0, 0, UV__IORING_ENTER_GETEVENTS);
    while (rc == -1 && errno == EINTR);

    if (rc < 0)
      perror("libuv: io_uring_enter(getevents)");  /* Can't happen. */
  }

  uv__metrics_inc_events(loop, nevents);
  if (uv__get_internal_fields(loop)->current_timeout == 0)
    uv__metrics_inc_events_waiting(loop, nevents);
}


/* Only for EPOLL_CTL_ADD and EPOLL_CTL_MOD. EPOLL_CTL_DEL should always be
 * executed immediately, otherwise the file descriptor may have been closed
 * by the time the kernel starts the operation.
 */
static void uv__epoll_ctl_prep(int epollfd,
                               struct uv__iou* ctl,
                               struct epoll_event (*events)[256],
                               int op,
                               int fd,
                               struct epoll_event* e) {
  struct uv__io_uring_sqe* sqe;
  struct epoll_event* pe;
  uint32_t mask;
  uint32_t slot;

  assert(op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD);
  assert(ctl->ringfd != -1);

  mask = ctl->sqmask;
  slot = (*ctl->sqtail)++ & mask;

  pe = &(*events)[slot];
  *pe = *e;

  sqe = ctl->sqe;
  sqe = &sqe[slot];

  memset(sqe, 0, sizeof(*sqe));
  sqe->addr = (uintptr_t) pe;
  sqe->fd = epollfd;
  sqe->len = op;
  sqe->off = fd;
  sqe->opcode = UV__IORING_OP_EPOLL_CTL;
  sqe->user_data = op | slot << 2 | (int64_t) fd << 32;

  if ((*ctl->sqhead & mask) == (*ctl->sqtail & mask))
    uv__epoll_ctl_flush(epollfd, ctl, events);
}


static void uv__epoll_ctl_flush(int epollfd,
                                struct uv__iou* ctl,
                                struct epoll_event (*events)[256]) {
  struct epoll_event oldevents[256];
  struct uv__io_uring_cqe* cqe;
  uint32_t oldslot;
  uint32_t slot;
  uint32_t n;
  int fd;
  int op;
  int rc;

  STATIC_ASSERT(sizeof(oldevents) == sizeof(*events));
  assert(ctl->ringfd != -1);
  assert(*ctl->sqhead != *ctl->sqtail);

  n = *ctl->sqtail - *ctl->sqhead;
  do
    rc = uv__io_uring_enter(ctl->ringfd, n, n, UV__IORING_ENTER_GETEVENTS);
  while (rc == -1 && errno == EINTR);

  if (rc < 0)
    perror("libuv: io_uring_enter(getevents)");  /* Can't happen. */

  if (rc != (int) n)
    abort();

  assert(*ctl->sqhead == *ctl->sqtail);

  memcpy(oldevents, *events, sizeof(*events));

  /* Failed submissions are either EPOLL_CTL_DEL commands for file descriptors
   * that have been closed, or EPOLL_CTL_ADD commands for file descriptors
   * that we are already watching. Ignore the former and retry the latter
   * with EPOLL_CTL_MOD.
   */
  while (*ctl->cqhead != *ctl->cqtail) {
    slot = (*ctl->cqhead)++ & ctl->cqmask;

    cqe = ctl->cqe;
    cqe = &cqe[slot];

    if (cqe->res == 0)
      continue;

    fd = cqe->user_data >> 32;
    op = 3 & cqe->user_data;
    oldslot = 255 & (cqe->user_data >> 2);

    if (op == EPOLL_CTL_DEL)
      continue;

    if (op != EPOLL_CTL_ADD)
      abort();

    if (cqe->res != -EEXIST)
      abort();

    uv__epoll_ctl_prep(epollfd,
                       ctl,
                       events,
                       EPOLL_CTL_MOD,
                       fd,
                       &oldevents[oldslot]);
  }
}


void uv__io_poll(uv_loop_t* loop, int timeout) {
  uv__loop_internal_fields_t* lfields;
  struct epoll_event events[1024];
  struct epoll_event prep[256];
  struct uv__invalidate inv;
  struct epoll_event* pe;
  struct epoll_event e;
  struct uv__iou* ctl;
  struct uv__iou* iou;
  int real_timeout;
  struct uv__queue* q;
  uv__io_t* w;
  sigset_t* sigmask;
  sigset_t sigset;
  uint64_t base;
  int have_iou_events;
  int have_signals;
  int nevents;
  int epollfd;
  int count;
  int nfds;
  int fd;
  int op;
  int i;
  int user_timeout;
  int reset_timeout;

  lfields = uv__get_internal_fields(loop);
  ctl = &lfields->ctl;
  iou = &lfields->iou;

  sigmask = NULL;
  if (loop->flags & UV_LOOP_BLOCK_SIGPROF) {
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPROF);
    sigmask = &sigset;
  }

  assert(timeout >= -1);
  base = loop->time;
  count = 48; /* Benchmarks suggest this gives the best throughput. */
  real_timeout = timeout;

  if (lfields->flags & UV_METRICS_IDLE_TIME) {
    reset_timeout = 1;
    user_timeout = timeout;
    timeout = 0;
  } else {
    reset_timeout = 0;
    user_timeout = 0;
  }

  epollfd = loop->backend_fd;

  memset(&e, 0, sizeof(e));

  while (!uv__queue_empty(&loop->watcher_queue)) {
    q = uv__queue_head(&loop->watcher_queue);
    w = uv__queue_data(q, uv__io_t, watcher_queue);
    uv__queue_remove(q);
    uv__queue_init(q);

    op = EPOLL_CTL_MOD;
    if (w->events == 0)
      op = EPOLL_CTL_ADD;

    w->events = w->pevents;
    e.events = w->pevents;
    e.data.fd = w->fd;
    fd = w->fd;

    if (ctl->ringfd != -1) {
      uv__epoll_ctl_prep(epollfd, ctl, &prep, op, fd, &e);
      continue;
    }

    if (!epoll_ctl(epollfd, op, fd, &e))
      continue;

    assert(op == EPOLL_CTL_ADD);
    assert(errno == EEXIST);

    /* File descriptor that's been watched before, update event mask. */
    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &e))
      abort();
  }

  inv.events = events;
  inv.prep = &prep;
  inv.nfds = -1;

  for (;;) {
    if (loop->nfds == 0)
      if (iou->in_flight == 0)
        break;

    /* All event mask mutations should be visible to the kernel before
     * we enter epoll_pwait().
     */
    if (ctl->ringfd != -1)
      while (*ctl->sqhead != *ctl->sqtail)
        uv__epoll_ctl_flush(epollfd, ctl, &prep);

    /* Only need to set the provider_entry_time if timeout != 0. The function
     * will return early if the loop isn't configured with UV_METRICS_IDLE_TIME.
     */
    if (timeout != 0)
      uv__metrics_set_provider_entry_time(loop);

    /* Store the current timeout in a location that's globally accessible so
     * other locations like uv__work_done() can determine whether the queue
     * of events in the callback were waiting when poll was called.
     */
    lfields->current_timeout = timeout;

    nfds = epoll_pwait(epollfd, events, ARRAY_SIZE(events), timeout, sigmask);

    /* Update loop->time unconditionally. It's tempting to skip the update when
     * timeout == 0 (i.e. non-blocking poll) but there is no guarantee that the
     * operating system didn't reschedule our process while in the syscall.
     */
    SAVE_ERRNO(uv__update_time(loop));

    if (nfds == -1)
      assert(errno == EINTR);
    else if (nfds == 0)
      /* Unlimited timeout should only return with events or signal. */
      assert(timeout != -1);

    if (nfds == 0 || nfds == -1) {
      if (reset_timeout != 0) {
        timeout = user_timeout;
        reset_timeout = 0;
      } else if (nfds == 0) {
        return;
      }

      /* Interrupted by a signal. Update timeout and poll again. */
      goto update_timeout;
    }

    have_iou_events = 0;
    have_signals = 0;
    nevents = 0;

    inv.nfds = nfds;
    lfields->inv = &inv;

    for (i = 0; i < nfds; i++) {
      pe = events + i;
      fd = pe->data.fd;

      /* Skip invalidated events, see uv__platform_invalidate_fd */
      if (fd == -1)
        continue;

      if (fd == iou->ringfd) {
        uv__poll_io_uring(loop, iou);
        have_iou_events = 1;
        continue;
      }

      assert(fd >= 0);
      assert((unsigned) fd < loop->nwatchers);

      w = loop->watchers[fd];

      if (w == NULL) {
        /* File descriptor that we've stopped watching, disarm it.
         *
         * Ignore all errors because we may be racing with another thread
         * when the file descriptor is closed.
         *
         * Perform EPOLL_CTL_DEL immediately instead of going through
         * io_uring's submit queue, otherwise the file descriptor may
         * be closed by the time the kernel starts the operation.
         */
        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, pe);
        continue;
      }

      /* Give users only events they're interested in. Prevents spurious
       * callbacks when previous callback invocation in this loop has stopped
       * the current watcher. Also, filters out events that users has not
       * requested us to watch.
       */
      pe->events &= w->pevents | POLLERR | POLLHUP;

      /* Work around an epoll quirk where it sometimes reports just the
       * EPOLLERR or EPOLLHUP event.  In order to force the event loop to
       * move forward, we merge in the read/write events that the watcher
       * is interested in; uv__read() and uv__write() will then deal with
       * the error or hangup in the usual fashion.
       *
       * Note to self: happens when epoll reports EPOLLIN|EPOLLHUP, the user
       * reads the available data, calls uv_read_stop(), then sometime later
       * calls uv_read_start() again.  By then, libuv has forgotten about the
       * hangup and the kernel won't report EPOLLIN again because there's
       * nothing left to read.  If anything, libuv is to blame here.  The
       * current hack is just a quick bandaid; to properly fix it, libuv
       * needs to remember the error/hangup event.  We should get that for
       * free when we switch over to edge-triggered I/O.
       */
      if (pe->events == POLLERR || pe->events == POLLHUP)
        pe->events |=
          w->pevents & (POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI);

      if (pe->events != 0) {
        /* Run signal watchers last.  This also affects child process watchers
         * because those are implemented in terms of signal watchers.
         */
        if (w == &loop->signal_io_watcher) {
          have_signals = 1;
        } else {
          uv__metrics_update_idle_time(loop);
          w->cb(loop, w, pe->events);
        }

        nevents++;
      }
    }

    uv__metrics_inc_events(loop, nevents);
    if (reset_timeout != 0) {
      timeout = user_timeout;
      reset_timeout = 0;
      uv__metrics_inc_events_waiting(loop, nevents);
    }

    if (have_signals != 0) {
      uv__metrics_update_idle_time(loop);
      loop->signal_io_watcher.cb(loop, &loop->signal_io_watcher, POLLIN);
    }

    lfields->inv = NULL;

    if (have_iou_events != 0)
      break;  /* Event loop should cycle now so don't poll again. */

    if (have_signals != 0)
      break;  /* Event loop should cycle now so don't poll again. */

    if (nevents != 0) {
      if (nfds == ARRAY_SIZE(events) && --count != 0) {
        /* Poll for more events but don't block this time. */
        timeout = 0;
        continue;
      }
      break;
    }

update_timeout:
    if (timeout == 0)
      break;

    if (timeout == -1)
      continue;

    assert(timeout > 0);

    real_timeout -= (loop->time - base);
    if (real_timeout <= 0)
      break;

    timeout = real_timeout;
  }

  if (ctl->ringfd != -1)
    while (*ctl->sqhead != *ctl->sqtail)
      uv__epoll_ctl_flush(epollfd, ctl, &prep);
}

uint64_t uv__hrtime(uv_clocktype_t type) {
  static _Atomic clock_t fast_clock_id = -1;
  struct timespec t;
  clock_t clock_id;

  /* Prefer CLOCK_MONOTONIC_COARSE if available but only when it has
   * millisecond granularity or better.  CLOCK_MONOTONIC_COARSE is
   * serviced entirely from the vDSO, whereas CLOCK_MONOTONIC may
   * decide to make a costly system call.
   */
  /* TODO(bnoordhuis) Use CLOCK_MONOTONIC_COARSE for UV_CLOCK_PRECISE
   * when it has microsecond granularity or better (unlikely).
   */
  clock_id = CLOCK_MONOTONIC;
  if (type != UV_CLOCK_FAST)
    goto done;

  clock_id = atomic_load_explicit(&fast_clock_id, memory_order_relaxed);
  if (clock_id != -1)
    goto done;

  clock_id = CLOCK_MONOTONIC;
  if (0 == clock_getres(CLOCK_MONOTONIC_COARSE, &t))
    if (t.tv_nsec <= 1 * 1000 * 1000)
      clock_id = CLOCK_MONOTONIC_COARSE;

  atomic_store_explicit(&fast_clock_id, clock_id, memory_order_relaxed);

done:

  if (clock_gettime(clock_id, &t))
    return 0;  /* Not really possible. */

  return t.tv_sec * (uint64_t) 1e9 + t.tv_nsec;
}


int uv_resident_set_memory(size_t* rss) {
  char buf[1024];
  const char* s;
  long val;
  int rc;
  int i;

  /* rss: 24th element */
  rc = uv__slurp("/proc/self/stat", buf, sizeof(buf));
  if (rc < 0)
    return rc;

  /* find the last ')' */
  s = strrchr(buf, ')');
  if (s == NULL)
    goto err;

  for (i = 1; i <= 22; i++) {
    s = strchr(s + 1, ' ');
    if (s == NULL)
      goto err;
  }

  errno = 0;
  val = strtol(s, NULL, 10);
  if (val < 0 || errno != 0)
    goto err;

  *rss = val * getpagesize();
  return 0;

err:
  return UV_EINVAL;
}

int uv_uptime(double* uptime) {
  struct timespec now;
  char buf[128];

  /* Consult /proc/uptime when present (common case), or fall back to
   * clock_gettime. Why not always clock_gettime? It doesn't always return the
   * right result under OpenVZ and possibly other containerized environments.
   */
  if (0 == uv__slurp("/proc/uptime", buf, sizeof(buf)))
    if (1 == sscanf(buf, "%lf", uptime))
      return 0;

  if (clock_gettime(CLOCK_BOOTTIME, &now))
    return UV__ERR(errno);

  *uptime = now.tv_sec;
  return 0;
}


int uv_cpu_info(uv_cpu_info_t** ci, int* count) {
#if defined(__PPC__)
  static const char model_marker[] = "cpu\t\t: ";
  static const char model_marker2[] = "";
#elif defined(__arm__)
  static const char model_marker[] = "model name\t: ";
  static const char model_marker2[] = "Processor\t: ";
#elif defined(__aarch64__)
  static const char model_marker[] = "CPU part\t: ";
  static const char model_marker2[] = "";
#elif defined(__mips__)
  static const char model_marker[] = "cpu model\t\t: ";
  static const char model_marker2[] = "";
#elif defined(__loongarch__)
  static const char model_marker[] = "cpu family\t\t: ";
  static const char model_marker2[] = "";
#else
  static const char model_marker[] = "model name\t: ";
  static const char model_marker2[] = "";
#endif
  static const char parts[] =
#ifdef __aarch64__
    "0x811\nARM810\n"       "0x920\nARM920\n"      "0x922\nARM922\n"
    "0x926\nARM926\n"       "0x940\nARM940\n"      "0x946\nARM946\n"
    "0x966\nARM966\n"       "0xa20\nARM1020\n"      "0xa22\nARM1022\n"
    "0xa26\nARM1026\n"      "0xb02\nARM11 MPCore\n" "0xb36\nARM1136\n"
    "0xb56\nARM1156\n"      "0xb76\nARM1176\n"      "0xc05\nCortex-A5\n"
    "0xc07\nCortex-A7\n"    "0xc08\nCortex-A8\n"    "0xc09\nCortex-A9\n"
    "0xc0d\nCortex-A17\n"   /* Originally A12 */
    "0xc0f\nCortex-A15\n"   "0xc0e\nCortex-A17\n"   "0xc14\nCortex-R4\n"
    "0xc15\nCortex-R5\n"    "0xc17\nCortex-R7\n"    "0xc18\nCortex-R8\n"
    "0xc20\nCortex-M0\n"    "0xc21\nCortex-M1\n"    "0xc23\nCortex-M3\n"
    "0xc24\nCortex-M4\n"    "0xc27\nCortex-M7\n"    "0xc60\nCortex-M0+\n"
    "0xd01\nCortex-A32\n"   "0xd03\nCortex-A53\n"   "0xd04\nCortex-A35\n"
    "0xd05\nCortex-A55\n"   "0xd06\nCortex-A65\n"   "0xd07\nCortex-A57\n"
    "0xd08\nCortex-A72\n"   "0xd09\nCortex-A73\n"   "0xd0a\nCortex-A75\n"
    "0xd0b\nCortex-A76\n"   "0xd0c\nNeoverse-N1\n"  "0xd0d\nCortex-A77\n"
    "0xd0e\nCortex-A76AE\n" "0xd13\nCortex-R52\n"   "0xd20\nCortex-M23\n"
    "0xd21\nCortex-M33\n"   "0xd41\nCortex-A78\n"   "0xd42\nCortex-A78AE\n"
    "0xd4a\nNeoverse-E1\n"  "0xd4b\nCortex-A78C\n"
#endif
    "";
  struct cpu {
    unsigned long long freq, user, nice, sys, idle, irq;
    unsigned model;
  };
  FILE* fp;
  char* p;
  int found;
  int n;
  unsigned i;
  unsigned cpu;
  unsigned maxcpu;
  unsigned size;
  unsigned long long skip;
  struct cpu (*cpus)[8192];  /* Kernel maximum. */
  struct cpu* c;
  struct cpu t;
  char (*model)[64];
  unsigned char bitmap[ARRAY_SIZE(*cpus) / 8];
  /* Assumption: even big.LITTLE systems will have only a handful
   * of different CPU models. Most systems will just have one.
   */
  char models[8][64];
  char buf[1024];

  memset(bitmap, 0, sizeof(bitmap));
  memset(models, 0, sizeof(models));
  snprintf(*models, sizeof(*models), "unknown");
  maxcpu = 0;

  cpus = uv__calloc(ARRAY_SIZE(*cpus), sizeof(**cpus));
  if (cpus == NULL)
    return UV_ENOMEM;

  fp = uv__open_file("/proc/stat");
  if (fp == NULL) {
    uv__free(cpus);
    return UV__ERR(errno);
  }

  if (NULL == fgets(buf, sizeof(buf), fp))
    abort();

  for (;;) {
    memset(&t, 0, sizeof(t));

    n = fscanf(fp, "cpu%u %llu %llu %llu %llu %llu %llu",
               &cpu, &t.user, &t.nice, &t.sys, &t.idle, &skip, &t.irq);

    if (n != 7)
      break;

    if (NULL == fgets(buf, sizeof(buf), fp))
      abort();

    if (cpu >= ARRAY_SIZE(*cpus))
      continue;

    (*cpus)[cpu] = t;

    bitmap[cpu >> 3] |= 1 << (cpu & 7);

    if (cpu >= maxcpu)
      maxcpu = cpu + 1;
  }

  fclose(fp);

  fp = uv__open_file("/proc/cpuinfo");
  if (fp == NULL)
    goto nocpuinfo;

  for (;;) {
    if (1 != fscanf(fp, "processor\t: %u\n", &cpu))
      break;  /* Parse error. */

    while (fgets(buf, sizeof(buf), fp)) {
      if (!strncmp(buf, model_marker, sizeof(model_marker) - 1)) {
        p = buf + sizeof(model_marker) - 1;
        goto parts;
      }
      if (!*model_marker2)
        continue;
      if (!strncmp(buf, model_marker2, sizeof(model_marker2) - 1)) {
        p = buf + sizeof(model_marker2) - 1;
        goto parts;
      }
    }

    goto next;  /* Not found. */

parts:
    n = (int) strcspn(p, "\n");

    /* arm64: translate CPU part code to model name. */
    if (*parts) {
      p = memmem(parts, sizeof(parts) - 1, p, n + 1);
      if (p == NULL)
        p = "unknown";
      else
        p += n + 1;
      n = (int) strcspn(p, "\n");
    }

    found = 0;
    for (model = models; !found && model < ARRAY_END(models); model++)
      found = !strncmp(p, *model, strlen(*model));

    if (!found)
      goto next;

    if (**model == '\0')
      snprintf(*model, sizeof(*model), "%.*s", n, p);

    if (cpu < maxcpu)
      (*cpus)[cpu].model = model - models;

next:
    while (fgets(buf, sizeof(buf), fp))
      if (*buf == '\n')
        break;
  }

  fclose(fp);
  fp = NULL;

nocpuinfo:

  n = 0;
  for (cpu = 0; cpu < maxcpu; cpu++) {
    if (!(bitmap[cpu >> 3] & (1 << (cpu & 7))))
      continue;

    n++;
    snprintf(buf, sizeof(buf),
             "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", cpu);

    fp = uv__open_file(buf);
    if (fp == NULL)
      continue;

    if (1 != fscanf(fp, "%llu", &(*cpus)[cpu].freq))
      abort();
    fclose(fp);
    fp = NULL;
  }

  size = n * sizeof(**ci) + sizeof(models);
  *ci = uv__malloc(size);
  *count = 0;

  if (*ci == NULL) {
    uv__free(cpus);
    return UV_ENOMEM;
  }

  *count = n;
  p = memcpy(*ci + n, models, sizeof(models));

  i = 0;
  for (cpu = 0; cpu < maxcpu; cpu++) {
    if (!(bitmap[cpu >> 3] & (1 << (cpu & 7))))
      continue;

    c = *cpus + cpu;

    (*ci)[i++] = (uv_cpu_info_t) {
      .model     = p + c->model * sizeof(*model),
      .speed     = c->freq / 1000,
      /* Note: sysconf(_SC_CLK_TCK) is fixed at 100 Hz,
       * therefore the multiplier is always 1000/100 = 10.
       */
      .cpu_times = (struct uv_cpu_times_s) {
        .user = 10 * c->user,
        .nice = 10 * c->nice,
        .sys  = 10 * c->sys,
        .idle = 10 * c->idle,
        .irq  = 10 * c->irq,
      },
    };
  }

  uv__free(cpus);

  return 0;
}


static int uv__ifaddr_exclude(struct ifaddrs *ent, int exclude_type) {
  if (!((ent->ifa_flags & IFF_UP) && (ent->ifa_flags & IFF_RUNNING)))
    return 1;
  if (ent->ifa_addr == NULL)
    return 1;
  /*
   * On Linux getifaddrs returns information related to the raw underlying
   * devices. We're not interested in this information yet.
   */
  if (ent->ifa_addr->sa_family == PF_PACKET)
    return exclude_type;
  return !exclude_type;
}

int uv_interface_addresses(uv_interface_address_t** addresses, int* count) {
  struct ifaddrs *addrs, *ent;
  uv_interface_address_t* address;
  int i;
  struct sockaddr_ll *sll;

  *count = 0;
  *addresses = NULL;

  if (getifaddrs(&addrs))
    return UV__ERR(errno);

  /* Count the number of interfaces */
  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFADDR))
      continue;

    (*count)++;
  }

  if (*count == 0) {
    freeifaddrs(addrs);
    return 0;
  }

  /* Make sure the memory is initiallized to zero using calloc() */
  *addresses = uv__calloc(*count, sizeof(**addresses));
  if (!(*addresses)) {
    freeifaddrs(addrs);
    return UV_ENOMEM;
  }

  address = *addresses;

  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFADDR))
      continue;

    address->name = uv__strdup(ent->ifa_name);

    if (ent->ifa_addr->sa_family == AF_INET6) {
      address->address.address6 = *((struct sockaddr_in6*) ent->ifa_addr);
    } else {
      address->address.address4 = *((struct sockaddr_in*) ent->ifa_addr);
    }

    if (ent->ifa_netmask->sa_family == AF_INET6) {
      address->netmask.netmask6 = *((struct sockaddr_in6*) ent->ifa_netmask);
    } else {
      address->netmask.netmask4 = *((struct sockaddr_in*) ent->ifa_netmask);
    }

    address->is_internal = !!(ent->ifa_flags & IFF_LOOPBACK);

    address++;
  }

  /* Fill in physical addresses for each interface */
  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFPHYS))
      continue;

    address = *addresses;

    for (i = 0; i < (*count); i++) {
      size_t namelen = strlen(ent->ifa_name);
      /* Alias interface share the same physical address */
      if (strncmp(address->name, ent->ifa_name, namelen) == 0 &&
          (address->name[namelen] == 0 || address->name[namelen] == ':')) {
        sll = (struct sockaddr_ll*)ent->ifa_addr;
        memcpy(address->phys_addr, sll->sll_addr, sizeof(address->phys_addr));
      }
      address++;
    }
  }

  freeifaddrs(addrs);

  return 0;
}


void uv_free_interface_addresses(uv_interface_address_t* addresses,
  int count) {
  int i;

  for (i = 0; i < count; i++) {
    uv__free(addresses[i].name);
  }

  uv__free(addresses);
}


void uv__set_process_title(const char* title) {
#if defined(PR_SET_NAME)
  prctl(PR_SET_NAME, title);  /* Only copies first 16 characters. */
#endif
}


static uint64_t uv__read_proc_meminfo(const char* what) {
  uint64_t rc;
  char* p;
  char buf[4096];  /* Large enough to hold all of /proc/meminfo. */

  if (uv__slurp("/proc/meminfo", buf, sizeof(buf)))
    return 0;

  p = strstr(buf, what);

  if (p == NULL)
    return 0;

  p += strlen(what);

  rc = 0;
  sscanf(p, "%" PRIu64 " kB", &rc);

  return rc * 1024;
}


uint64_t uv_get_free_memory(void) {
  struct sysinfo info;
  uint64_t rc;

  rc = uv__read_proc_meminfo("MemAvailable:");

  if (rc != 0)
    return rc;

  if (0 == sysinfo(&info))
    return (uint64_t) info.freeram * info.mem_unit;

  return 0;
}


uint64_t uv_get_total_memory(void) {
  struct sysinfo info;
  uint64_t rc;

  rc = uv__read_proc_meminfo("MemTotal:");

  if (rc != 0)
    return rc;

  if (0 == sysinfo(&info))
    return (uint64_t) info.totalram * info.mem_unit;

  return 0;
}


static uint64_t uv__read_uint64(const char* filename) {
  char buf[32];  /* Large enough to hold an encoded uint64_t. */
  uint64_t rc;

  rc = 0;
  if (0 == uv__slurp(filename, buf, sizeof(buf)))
    if (1 != sscanf(buf, "%" PRIu64, &rc))
      if (0 == strcmp(buf, "max\n"))
        rc = UINT64_MAX;

  return rc;
}


/* Given a buffer with the contents of a cgroup1 /proc/self/cgroups,
 * finds the location and length of the memory controller mount path.
 * This disregards the leading / for easy concatenation of paths.
 * Returns NULL if the memory controller wasn't found. */
static char* uv__cgroup1_find_memory_controller(char buf[static 1024],
                                                int* n) {
  char* p;

  /* Seek to the memory controller line. */
  p = strchr(buf, ':');
  while (p != NULL && strncmp(p, ":memory:", 8)) {
    p = strchr(p, '\n');
    if (p != NULL)
      p = strchr(p, ':');
  }

  if (p != NULL) {
    /* Determine the length of the mount path. */
    p = p + strlen(":memory:/");
    *n = (int) strcspn(p, "\n");
  }

  return p;
}

static void uv__get_cgroup1_memory_limits(char buf[static 1024], uint64_t* high,
                                          uint64_t* max) {
  char filename[4097];
  char* p;
  int n;
  uint64_t cgroup1_max;

  /* Find out where the controller is mounted. */
  p = uv__cgroup1_find_memory_controller(buf, &n);
  if (p != NULL) {
    snprintf(filename, sizeof(filename),
             "/sys/fs/cgroup/memory/%.*s/memory.soft_limit_in_bytes", n, p);
    *high = uv__read_uint64(filename);

    snprintf(filename, sizeof(filename),
             "/sys/fs/cgroup/memory/%.*s/memory.limit_in_bytes", n, p);
    *max = uv__read_uint64(filename);

    /* If the controller wasn't mounted, the reads above will have failed,
     * as indicated by uv__read_uint64 returning 0.
     */
     if (*high != 0 && *max != 0)
       goto update_limits;
  }

  /* Fall back to the limits of the global memory controller. */
  *high = uv__read_uint64("/sys/fs/cgroup/memory/memory.soft_limit_in_bytes");
  *max = uv__read_uint64("/sys/fs/cgroup/memory/memory.limit_in_bytes");

  /* uv__read_uint64 detects cgroup2's "max", so we need to separately detect
   * cgroup1's maximum value (which is derived from LONG_MAX and PAGE_SIZE).
   */
update_limits:
  cgroup1_max = LONG_MAX & ~(sysconf(_SC_PAGESIZE) - 1);
  if (*high == cgroup1_max)
    *high = UINT64_MAX;
  if (*max == cgroup1_max)
    *max = UINT64_MAX;
}

static void uv__get_cgroup2_memory_limits(char buf[static 1024], uint64_t* high,
                                          uint64_t* max) {
  char filename[4097];
  char* p;
  int n;

  /* Find out where the controller is mounted. */
  p = buf + strlen("0::/");
  n = (int) strcspn(p, "\n");

  /* Read the memory limits of the controller. */
  snprintf(filename, sizeof(filename), "/sys/fs/cgroup/%.*s/memory.max", n, p);
  *max = uv__read_uint64(filename);
  snprintf(filename, sizeof(filename), "/sys/fs/cgroup/%.*s/memory.high", n, p);
  *high = uv__read_uint64(filename);
}

static uint64_t uv__get_cgroup_constrained_memory(char buf[static 1024]) {
  uint64_t high;
  uint64_t max;

  /* In the case of cgroupv2, we'll only have a single entry. */
  if (strncmp(buf, "0::/", 4))
    uv__get_cgroup1_memory_limits(buf, &high, &max);
  else
    uv__get_cgroup2_memory_limits(buf, &high, &max);

  if (high == 0 || max == 0)
    return 0;

  return high < max ? high : max;
}

uint64_t uv_get_constrained_memory(void) {
  char buf[1024];

  if (uv__slurp("/proc/self/cgroup", buf, sizeof(buf)))
    return 0;

  return uv__get_cgroup_constrained_memory(buf);
}


static uint64_t uv__get_cgroup1_current_memory(char buf[static 1024]) {
  char filename[4097];
  uint64_t current;
  char* p;
  int n;

  /* Find out where the controller is mounted. */
  p = uv__cgroup1_find_memory_controller(buf, &n);
  if (p != NULL) {
    snprintf(filename, sizeof(filename),
            "/sys/fs/cgroup/memory/%.*s/memory.usage_in_bytes", n, p);
    current = uv__read_uint64(filename);

    /* If the controller wasn't mounted, the reads above will have failed,
     * as indicated by uv__read_uint64 returning 0.
     */
    if (current != 0)
      return current;
  }

  /* Fall back to the usage of the global memory controller. */
  return uv__read_uint64("/sys/fs/cgroup/memory/memory.usage_in_bytes");
}

static uint64_t uv__get_cgroup2_current_memory(char buf[static 1024]) {
  char filename[4097];
  char* p;
  int n;

  /* Find out where the controller is mounted. */
  p = buf + strlen("0::/");
  n = (int) strcspn(p, "\n");

  snprintf(filename, sizeof(filename),
           "/sys/fs/cgroup/%.*s/memory.current", n, p);
  return uv__read_uint64(filename);
}

uint64_t uv_get_available_memory(void) {
  char buf[1024];
  uint64_t constrained;
  uint64_t current;
  uint64_t total;

  if (uv__slurp("/proc/self/cgroup", buf, sizeof(buf)))
    return 0;

  constrained = uv__get_cgroup_constrained_memory(buf);
  if (constrained == 0)
    return uv_get_free_memory();

  total = uv_get_total_memory();
  if (constrained > total)
    return uv_get_free_memory();

  /* In the case of cgroupv2, we'll only have a single entry. */
  if (strncmp(buf, "0::/", 4))
    current = uv__get_cgroup1_current_memory(buf);
  else
    current = uv__get_cgroup2_current_memory(buf);

  /* memory usage can be higher than the limit (for short bursts of time) */
  if (constrained < current)
    return 0;

  return constrained - current;
}


static int uv__get_cgroupv2_constrained_cpu(const char* cgroup,
                                            uv__cpu_constraint* constraint) {
  char path[256];
  char buf[1024];
  unsigned int weight;
  int cgroup_size;
  const char* cgroup_trimmed;
  char quota_buf[16];

  if (strncmp(cgroup, "0::/", 4) != 0)
    return UV_EINVAL;

  /* Trim ending \n by replacing it with a 0 */
  cgroup_trimmed = cgroup + sizeof("0::/") - 1;      /* Skip the prefix "0::/" */
  cgroup_size = (int)strcspn(cgroup_trimmed, "\n");  /* Find the first slash */

  /* Construct the path to the cpu.max file */
  snprintf(path, sizeof(path), "/sys/fs/cgroup/%.*s/cpu.max", cgroup_size,
           cgroup_trimmed);

  /* Read cpu.max */
  if (uv__slurp(path, buf, sizeof(buf)) < 0)
    return UV_EIO;

  if (sscanf(buf, "%15s %llu", quota_buf, &constraint->period_length) != 2)
    return UV_EINVAL;

  if (strncmp(quota_buf, "max", 3) == 0)
    constraint->quota_per_period = LLONG_MAX;
  else if (sscanf(quota_buf, "%lld", &constraint->quota_per_period) != 1)
    return UV_EINVAL; // conversion failed

  /* Construct the path to the cpu.weight file */
  snprintf(path, sizeof(path), "/sys/fs/cgroup/%.*s/cpu.weight", cgroup_size,
           cgroup_trimmed);

  /* Read cpu.weight */
  if (uv__slurp(path, buf, sizeof(buf)) < 0)
    return UV_EIO;

  if (sscanf(buf, "%u", &weight) != 1)
    return UV_EINVAL;

  constraint->proportions = (double)weight / 100.0;

  return 0;
}

static char* uv__cgroup1_find_cpu_controller(const char* cgroup,
                                             int* cgroup_size) {
  /* Seek to the cpu controller line. */
  char* cgroup_cpu = strstr(cgroup, ":cpu,");

  if (cgroup_cpu != NULL) {
    /* Skip the controller prefix to the start of the cgroup path. */
    cgroup_cpu += sizeof(":cpu,") - 1;
    /* Determine the length of the cgroup path, excluding the newline. */
    *cgroup_size = (int)strcspn(cgroup_cpu, "\n");
  }

  return cgroup_cpu;
}

static int uv__get_cgroupv1_constrained_cpu(const char* cgroup,
                                            uv__cpu_constraint* constraint) {
  char path[256];
  char buf[1024];
  unsigned int shares;
  int cgroup_size;
  char* cgroup_cpu;

  cgroup_cpu = uv__cgroup1_find_cpu_controller(cgroup, &cgroup_size);

  if (cgroup_cpu == NULL)
    return UV_EIO;

  /* Construct the path to the cpu.cfs_quota_us file */
  snprintf(path, sizeof(path), "/sys/fs/cgroup/%.*s/cpu.cfs_quota_us",
           cgroup_size, cgroup_cpu);

  if (uv__slurp(path, buf, sizeof(buf)) < 0)
    return UV_EIO;

  if (sscanf(buf, "%lld", &constraint->quota_per_period) != 1)
    return UV_EINVAL;

  /* Construct the path to the cpu.cfs_period_us file */
  snprintf(path, sizeof(path), "/sys/fs/cgroup/%.*s/cpu.cfs_period_us",
           cgroup_size, cgroup_cpu);

  /* Read cpu.cfs_period_us */
  if (uv__slurp(path, buf, sizeof(buf)) < 0)
    return UV_EIO;

  if (sscanf(buf, "%lld", &constraint->period_length) != 1)
    return UV_EINVAL;

  /* Construct the path to the cpu.shares file */
  snprintf(path, sizeof(path), "/sys/fs/cgroup/%.*s/cpu.shares", cgroup_size,
           cgroup_cpu);

  /* Read cpu.shares */
  if (uv__slurp(path, buf, sizeof(buf)) < 0)
    return UV_EIO;

  if (sscanf(buf, "%u", &shares) != 1)
    return UV_EINVAL;

  constraint->proportions = (double)shares / 1024.0;

  return 0;
}

int uv__get_constrained_cpu(uv__cpu_constraint* constraint) {
  char cgroup[1024];

  /* Read the cgroup from /proc/self/cgroup */
  if (uv__slurp("/proc/self/cgroup", cgroup, sizeof(cgroup)) < 0)
    return UV_EIO;

  /* Check if the system is using cgroup v2 by examining /proc/self/cgroup
   * The entry for cgroup v2 is always in the format "0::$PATH"
   * see https://docs.kernel.org/admin-guide/cgroup-v2.html */
  if (strncmp(cgroup, "0::/", 4) == 0)
    return uv__get_cgroupv2_constrained_cpu(cgroup, constraint);
  else
    return uv__get_cgroupv1_constrained_cpu(cgroup, constraint);
}


void uv_loadavg(double avg[3]) {
  struct sysinfo info;
  char buf[128];  /* Large enough to hold all of /proc/loadavg. */

  if (0 == uv__slurp("/proc/loadavg", buf, sizeof(buf)))
    if (3 == sscanf(buf, "%lf %lf %lf", &avg[0], &avg[1], &avg[2]))
      return;

  if (sysinfo(&info) < 0)
    return;

  avg[0] = (double) info.loads[0] / 65536.0;
  avg[1] = (double) info.loads[1] / 65536.0;
  avg[2] = (double) info.loads[2] / 65536.0;
}


static int compare_watchers(const struct watcher_list* a,
                            const struct watcher_list* b) {
  if (a->wd < b->wd) return -1;
  if (a->wd > b->wd) return 1;
  return 0;
}


static int init_inotify(uv_loop_t* loop) {
  int fd;

  if (loop->inotify_fd != -1)
    return 0;

  fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0)
    return UV__ERR(errno);

  loop->inotify_fd = fd;
  uv__io_init(&loop->inotify_read_watcher, uv__inotify_read, loop->inotify_fd);
  uv__io_start(loop, &loop->inotify_read_watcher, POLLIN);

  return 0;
}


static int uv__inotify_fork(uv_loop_t* loop, struct watcher_list* root) {
  /* Open the inotify_fd, and re-arm all the inotify watchers. */
  int err;
  struct watcher_list* tmp_watcher_list_iter;
  struct watcher_list* watcher_list;
  struct watcher_list tmp_watcher_list;
  struct uv__queue queue;
  struct uv__queue* q;
  uv_fs_event_t* handle;
  char* tmp_path;

  if (root == NULL)
    return 0;

  /* We must restore the old watcher list to be able to close items
   * out of it.
   */
  loop->inotify_watchers = root;

  uv__queue_init(&tmp_watcher_list.watchers);
  /* Note that the queue we use is shared with the start and stop()
   * functions, making uv__queue_foreach unsafe to use. So we use the
   * uv__queue_move trick to safely iterate. Also don't free the watcher
   * list until we're done iterating. c.f. uv__inotify_read.
   */
  RB_FOREACH_SAFE(watcher_list, watcher_root,
                  uv__inotify_watchers(loop), tmp_watcher_list_iter) {
    watcher_list->iterating = 1;
    uv__queue_move(&watcher_list->watchers, &queue);
    while (!uv__queue_empty(&queue)) {
      q = uv__queue_head(&queue);
      handle = uv__queue_data(q, uv_fs_event_t, watchers);
      /* It's critical to keep a copy of path here, because it
       * will be set to NULL by stop() and then deallocated by
       * maybe_free_watcher_list
       */
      tmp_path = uv__strdup(handle->path);
      assert(tmp_path != NULL);
      uv__queue_remove(q);
      uv__queue_insert_tail(&watcher_list->watchers, q);
      uv_fs_event_stop(handle);

      uv__queue_insert_tail(&tmp_watcher_list.watchers, &handle->watchers);
      handle->path = tmp_path;
    }
    watcher_list->iterating = 0;
    maybe_free_watcher_list(watcher_list, loop);
  }

  uv__queue_move(&tmp_watcher_list.watchers, &queue);
  while (!uv__queue_empty(&queue)) {
      q = uv__queue_head(&queue);
      uv__queue_remove(q);
      handle = uv__queue_data(q, uv_fs_event_t, watchers);
      tmp_path = handle->path;
      handle->path = NULL;
      err = uv_fs_event_start(handle, handle->cb, tmp_path, 0);
      uv__free(tmp_path);
      if (err)
        return err;
  }

  return 0;
}


static struct watcher_list* find_watcher(uv_loop_t* loop, int wd) {
  struct watcher_list w;
  w.wd = wd;
  return RB_FIND(watcher_root, uv__inotify_watchers(loop), &w);
}


static void maybe_free_watcher_list(struct watcher_list* w, uv_loop_t* loop) {
  /* if the watcher_list->watchers is being iterated over, we can't free it. */
  if ((!w->iterating) && uv__queue_empty(&w->watchers)) {
    /* No watchers left for this path. Clean up. */
    RB_REMOVE(watcher_root, uv__inotify_watchers(loop), w);
    inotify_rm_watch(loop->inotify_fd, w->wd);
    uv__free(w);
  }
}


static void uv__inotify_read(uv_loop_t* loop,
                             uv__io_t* dummy,
                             unsigned int events) {
  const struct inotify_event* e;
  struct watcher_list* w;
  uv_fs_event_t* h;
  struct uv__queue queue;
  struct uv__queue* q;
  const char* path;
  ssize_t size;
  const char *p;
  /* needs to be large enough for sizeof(inotify_event) + strlen(path) */
  char buf[4096];

  for (;;) {
    do
      size = read(loop->inotify_fd, buf, sizeof(buf));
    while (size == -1 && errno == EINTR);

    if (size == -1) {
      assert(errno == EAGAIN || errno == EWOULDBLOCK);
      break;
    }

    assert(size > 0); /* pre-2.6.21 thing, size=0 == read buffer too small */

    /* Now we have one or more inotify_event structs. */
    for (p = buf; p < buf + size; p += sizeof(*e) + e->len) {
      e = (const struct inotify_event*) p;

      events = 0;
      if (e->mask & (IN_ATTRIB|IN_MODIFY))
        events |= UV_CHANGE;
      if (e->mask & ~(IN_ATTRIB|IN_MODIFY))
        events |= UV_RENAME;

      w = find_watcher(loop, e->wd);
      if (w == NULL)
        continue; /* Stale event, no watchers left. */

      /* inotify does not return the filename when monitoring a single file
       * for modifications. Repurpose the filename for API compatibility.
       * I'm not convinced this is a good thing, maybe it should go.
       */
      path = e->len ? (const char*) (e + 1) : uv__basename_r(w->path);

      /* We're about to iterate over the queue and call user's callbacks.
       * What can go wrong?
       * A callback could call uv_fs_event_stop()
       * and the queue can change under our feet.
       * So, we use uv__queue_move() trick to safely iterate over the queue.
       * And we don't free the watcher_list until we're done iterating.
       *
       * First,
       * tell uv_fs_event_stop() (that could be called from a user's callback)
       * not to free watcher_list.
       */
      w->iterating = 1;
      uv__queue_move(&w->watchers, &queue);
      while (!uv__queue_empty(&queue)) {
        q = uv__queue_head(&queue);
        h = uv__queue_data(q, uv_fs_event_t, watchers);

        uv__queue_remove(q);
        uv__queue_insert_tail(&w->watchers, q);

        h->cb(h, path, events, 0);
      }
      /* done iterating, time to (maybe) free empty watcher_list */
      w->iterating = 0;
      maybe_free_watcher_list(w, loop);
    }
  }
}


int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
  uv__handle_init(loop, (uv_handle_t*)handle, UV_FS_EVENT);
  return 0;
}


int uv_fs_event_start(uv_fs_event_t* handle,
                      uv_fs_event_cb cb,
                      const char* path,
                      unsigned int flags) {
  struct watcher_list* w;
  uv_loop_t* loop;
  size_t len;
  int events;
  int err;
  int wd;

  if (uv__is_active(handle))
    return UV_EINVAL;

  loop = handle->loop;

  err = init_inotify(loop);
  if (err)
    return err;

  events = IN_ATTRIB
         | IN_CREATE
         | IN_MODIFY
         | IN_DELETE
         | IN_DELETE_SELF
         | IN_MOVE_SELF
         | IN_MOVED_FROM
         | IN_MOVED_TO;

  wd = inotify_add_watch(loop->inotify_fd, path, events);
  if (wd == -1)
    return UV__ERR(errno);

  w = find_watcher(loop, wd);
  if (w)
    goto no_insert;

  len = strlen(path) + 1;
  w = uv__malloc(sizeof(*w) + len);
  if (w == NULL)
    return UV_ENOMEM;

  w->wd = wd;
  w->path = memcpy(w + 1, path, len);
  uv__queue_init(&w->watchers);
  w->iterating = 0;
  RB_INSERT(watcher_root, uv__inotify_watchers(loop), w);

no_insert:
  uv__handle_start(handle);
  uv__queue_insert_tail(&w->watchers, &handle->watchers);
  handle->path = w->path;
  handle->cb = cb;
  handle->wd = wd;

  return 0;
}


int uv_fs_event_stop(uv_fs_event_t* handle) {
  struct watcher_list* w;

  if (!uv__is_active(handle))
    return 0;

  w = find_watcher(handle->loop, handle->wd);
  assert(w != NULL);

  handle->wd = -1;
  handle->path = NULL;
  uv__handle_stop(handle);
  uv__queue_remove(&handle->watchers);

  maybe_free_watcher_list(w, handle->loop);

  return 0;
}


void uv__fs_event_close(uv_fs_event_t* handle) {
  uv_fs_event_stop(handle);
}
