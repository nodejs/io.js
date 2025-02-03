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
#include "uv-common.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h> /* NULL */
#include <stdio.h>
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */

#if defined(_WIN32)
# include <malloc.h> /* malloc */
#else
# include <net/if.h> /* if_nametoindex */
# include <sys/un.h> /* AF_UNIX, sockaddr_un */
#endif


typedef struct {
  uv_malloc_func local_malloc;
  uv_realloc_func local_realloc;
  uv_calloc_func local_calloc;
  uv_free_func local_free;
} uv__allocator_t;

static uv__allocator_t uv__allocator = {
  malloc,
  realloc,
  calloc,
  free,
};

char* uv__strdup(const char* s) {
  size_t len = strlen(s) + 1;
  char* m = uv__malloc(len);
  if (m == NULL)
    return NULL;
  return memcpy(m, s, len);
}

char* uv__strndup(const char* s, size_t n) {
  char* m;
  size_t len = strlen(s);
  if (n < len)
    len = n;
  m = uv__malloc(len + 1);
  if (m == NULL)
    return NULL;
  m[len] = '\0';
  return memcpy(m, s, len);
}

void* uv__malloc(size_t size) {
  if (size > 0)
    return uv__allocator.local_malloc(size);
  return NULL;
}

void uv__free(void* ptr) {
  int saved_errno;

  /* Libuv expects that free() does not clobber errno.  The system allocator
   * honors that assumption but custom allocators may not be so careful.
   */
  saved_errno = errno;
  uv__allocator.local_free(ptr);
  errno = saved_errno;
}

void* uv__calloc(size_t count, size_t size) {
  return uv__allocator.local_calloc(count, size);
}

void* uv__realloc(void* ptr, size_t size) {
  if (size > 0)
    return uv__allocator.local_realloc(ptr, size);
  uv__free(ptr);
  return NULL;
}

void* uv__reallocf(void* ptr, size_t size) {
  void* newptr;

  newptr = uv__realloc(ptr, size);
  if (newptr == NULL)
    if (size > 0)
      uv__free(ptr);

  return newptr;
}

int uv_replace_allocator(uv_malloc_func malloc_func,
                         uv_realloc_func realloc_func,
                         uv_calloc_func calloc_func,
                         uv_free_func free_func) {
  if (malloc_func == NULL || realloc_func == NULL ||
      calloc_func == NULL || free_func == NULL) {
    return UV_EINVAL;
  }

  uv__allocator.local_malloc = malloc_func;
  uv__allocator.local_realloc = realloc_func;
  uv__allocator.local_calloc = calloc_func;
  uv__allocator.local_free = free_func;

  return 0;
}


void uv_os_free_passwd(uv_passwd_t* pwd) {
  if (pwd == NULL)
    return;

  /* On unix, the memory for name, shell, and homedir are allocated in a single
   * uv__malloc() call. The base of the pointer is stored in pwd->username, so
   * that is the field that needs to be freed.
   */
  uv__free(pwd->username);
#ifdef _WIN32
  uv__free(pwd->homedir);
#endif
  pwd->username = NULL;
  pwd->shell = NULL;
  pwd->homedir = NULL;
}


void uv_os_free_group(uv_group_t *grp) {
  if (grp == NULL)
    return;

  /* The memory for is allocated in a single uv__malloc() call. The base of the
   * pointer is stored in grp->members, so that is the only field that needs to
   * be freed.
   */
  uv__free(grp->members);
  grp->members = NULL;
  grp->groupname = NULL;
}


#define XX(uc, lc) case UV_##uc: return sizeof(uv_##lc##_t);

size_t uv_handle_size(uv_handle_type type) {
  switch (type) {
    UV_HANDLE_TYPE_MAP(XX)
    default:
      return -1;
  }
}

size_t uv_req_size(uv_req_type type) {
  switch(type) {
    UV_REQ_TYPE_MAP(XX)
    default:
      return -1;
  }
}

#undef XX


size_t uv_loop_size(void) {
  return sizeof(uv_loop_t);
}


uv_buf_t uv_buf_init(char* base, unsigned int len) {
  uv_buf_t buf;
  buf.base = base;
  buf.len = len;
  return buf;
}


static const char* uv__unknown_err_code(int err) {
  char buf[32];
  char* copy;

  snprintf(buf, sizeof(buf), "Unknown system error %d", err);
  copy = uv__strdup(buf);

  return copy != NULL ? copy : "Unknown system error";
}

#define UV_ERR_NAME_GEN_R(name, _) \
case UV_## name: \
  uv__strscpy(buf, #name, buflen); break;
char* uv_err_name_r(int err, char* buf, size_t buflen) {
  switch (err) {
    UV_ERRNO_MAP(UV_ERR_NAME_GEN_R)
    default: snprintf(buf, buflen, "Unknown system error %d", err);
  }
  return buf;
}
#undef UV_ERR_NAME_GEN_R


#define UV_ERR_NAME_GEN(name, _) case UV_ ## name: return #name;
const char* uv_err_name(int err) {
  switch (err) {
    UV_ERRNO_MAP(UV_ERR_NAME_GEN)
  }
  return uv__unknown_err_code(err);
}
#undef UV_ERR_NAME_GEN


#define UV_STRERROR_GEN_R(name, msg) \
case UV_ ## name: \
  snprintf(buf, buflen, "%s", msg); break;
char* uv_strerror_r(int err, char* buf, size_t buflen) {
  switch (err) {
    UV_ERRNO_MAP(UV_STRERROR_GEN_R)
    default: snprintf(buf, buflen, "Unknown system error %d", err);
  }
  return buf;
}
#undef UV_STRERROR_GEN_R


#define UV_STRERROR_GEN(name, msg) case UV_ ## name: return msg;
const char* uv_strerror(int err) {
  switch (err) {
    UV_ERRNO_MAP(UV_STRERROR_GEN)
  }
  return uv__unknown_err_code(err);
}
#undef UV_STRERROR_GEN


int uv_ip4_addr(const char* ip, int port, struct sockaddr_in* addr) {
  memset(addr, 0, sizeof(*addr));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
#ifdef SIN6_LEN
  addr->sin_len = sizeof(*addr);
#endif
  return uv_inet_pton(AF_INET, ip, &(addr->sin_addr.s_addr));
}


int uv_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr) {
  char address_part[40];
  size_t address_part_size;
  const char* zone_index;

  memset(addr, 0, sizeof(*addr));
  addr->sin6_family = AF_INET6;
  addr->sin6_port = htons(port);
#ifdef SIN6_LEN
  addr->sin6_len = sizeof(*addr);
#endif

  zone_index = strchr(ip, '%');
  if (zone_index != NULL) {
    address_part_size = zone_index - ip;
    if (address_part_size >= sizeof(address_part))
      address_part_size = sizeof(address_part) - 1;

    memcpy(address_part, ip, address_part_size);
    address_part[address_part_size] = '\0';
    ip = address_part;

    zone_index++; /* skip '%' */
    /* NOTE: unknown interface (id=0) is silently ignored */
#ifdef _WIN32
    addr->sin6_scope_id = atoi(zone_index);
#else
    addr->sin6_scope_id = if_nametoindex(zone_index);
#endif
  }

  return uv_inet_pton(AF_INET6, ip, &addr->sin6_addr);
}


int uv_ip4_name(const struct sockaddr_in* src, char* dst, size_t size) {
  return uv_inet_ntop(AF_INET, &src->sin_addr, dst, size);
}


int uv_ip6_name(const struct sockaddr_in6* src, char* dst, size_t size) {
  return uv_inet_ntop(AF_INET6, &src->sin6_addr, dst, size);
}


int uv_ip_name(const struct sockaddr *src, char *dst, size_t size) {
  switch (src->sa_family) {
  case AF_INET:
    return uv_inet_ntop(AF_INET, &((struct sockaddr_in *)src)->sin_addr,
                        dst, size);
  case AF_INET6:
    return uv_inet_ntop(AF_INET6, &((struct sockaddr_in6 *)src)->sin6_addr,
                        dst, size);
  default:
    return UV_EAFNOSUPPORT;
  }
}


int uv_tcp_bind(uv_tcp_t* handle,
                const struct sockaddr* addr,
                unsigned int flags) {
  unsigned int addrlen;

  if (handle->type != UV_TCP)
    return UV_EINVAL;
  if (uv__is_closing(handle)) {
    return UV_EINVAL;
  }
  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__tcp_bind(handle, addr, addrlen, flags);
}


int uv_udp_init_ex(uv_loop_t* loop, uv_udp_t* handle, unsigned flags) {
  unsigned extra_flags;
  int domain;
  int rc;

  /* Use the lower 8 bits for the domain. */
  domain = flags & 0xFF;
  if (domain != AF_INET && domain != AF_INET6 && domain != AF_UNSPEC)
    return UV_EINVAL;

  /* Use the higher bits for extra flags. */
  extra_flags = flags & ~0xFF;
  if (extra_flags & ~UV_UDP_RECVMMSG)
    return UV_EINVAL;

  rc = uv__udp_init_ex(loop, handle, flags, domain);

  if (rc == 0)
    if (extra_flags & UV_UDP_RECVMMSG)
      handle->flags |= UV_HANDLE_UDP_RECVMMSG;

  return rc;
}


int uv_udp_init(uv_loop_t* loop, uv_udp_t* handle) {
  return uv_udp_init_ex(loop, handle, AF_UNSPEC);
}


int uv_udp_bind(uv_udp_t* handle,
                const struct sockaddr* addr,
                unsigned int flags) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__udp_bind(handle, addr, addrlen, flags);
}


int uv_tcp_connect(uv_connect_t* req,
                   uv_tcp_t* handle,
                   const struct sockaddr* addr,
                   uv_connect_cb cb) {
  unsigned int addrlen;

  if (handle->type != UV_TCP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__tcp_connect(req, handle, addr, addrlen, cb);
}


int uv_udp_connect(uv_udp_t* handle, const struct sockaddr* addr) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  /* Disconnect the handle */
  if (addr == NULL) {
    if (!(handle->flags & UV_HANDLE_UDP_CONNECTED))
      return UV_ENOTCONN;

    return uv__udp_disconnect(handle);
  }

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  if (handle->flags & UV_HANDLE_UDP_CONNECTED)
    return UV_EISCONN;

  return uv__udp_connect(handle, addr, addrlen);
}


int uv__udp_is_connected(uv_udp_t* handle) {
  struct sockaddr_storage addr;
  int addrlen;
  if (handle->type != UV_UDP)
    return 0;

  addrlen = sizeof(addr);
  if (uv_udp_getpeername(handle, (struct sockaddr*) &addr, &addrlen) != 0)
    return 0;

  return addrlen > 0;
}


int uv__udp_check_before_send(uv_udp_t* handle, const struct sockaddr* addr) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  if (addr != NULL && (handle->flags & UV_HANDLE_UDP_CONNECTED))
    return UV_EISCONN;

  if (addr == NULL && !(handle->flags & UV_HANDLE_UDP_CONNECTED))
    return UV_EDESTADDRREQ;

  if (addr != NULL) {
    if (addr->sa_family == AF_INET)
      addrlen = sizeof(struct sockaddr_in);
    else if (addr->sa_family == AF_INET6)
      addrlen = sizeof(struct sockaddr_in6);
#if defined(AF_UNIX) && !defined(_WIN32)
    else if (addr->sa_family == AF_UNIX)
      addrlen = sizeof(struct sockaddr_un);
#endif
    else
      return UV_EINVAL;
  } else {
    addrlen = 0;
  }

  return addrlen;
}


int uv_udp_send(uv_udp_send_t* req,
                uv_udp_t* handle,
                const uv_buf_t bufs[],
                unsigned int nbufs,
                const struct sockaddr* addr,
                uv_udp_send_cb send_cb) {
  int addrlen;

  addrlen = uv__udp_check_before_send(handle, addr);
  if (addrlen < 0)
    return addrlen;

  return uv__udp_send(req, handle, bufs, nbufs, addr, addrlen, send_cb);
}


int uv_udp_try_send(uv_udp_t* handle,
                    const uv_buf_t bufs[],
                    unsigned int nbufs,
                    const struct sockaddr* addr) {
  int addrlen;

  addrlen = uv__udp_check_before_send(handle, addr);
  if (addrlen < 0)
    return addrlen;

  return uv__udp_try_send(handle, bufs, nbufs, addr, addrlen);
}


int uv_udp_try_send2(uv_udp_t* handle,
                     unsigned int count,
                     uv_buf_t* bufs[/*count*/],
                     unsigned int nbufs[/*count*/],
                     struct sockaddr* addrs[/*count*/],
                     unsigned int flags) {
  if (count < 1)
    return UV_EINVAL;

  if (flags != 0)
    return UV_EINVAL;

  if (handle->send_queue_count > 0)
    return UV_EAGAIN;

  return uv__udp_try_send2(handle, count, bufs, nbufs, addrs);
}


int uv_udp_recv_start(uv_udp_t* handle,
                      uv_alloc_cb alloc_cb,
                      uv_udp_recv_cb recv_cb) {
  if (handle->type != UV_UDP || alloc_cb == NULL || recv_cb == NULL)
    return UV_EINVAL;
  else
    return uv__udp_recv_start(handle, alloc_cb, recv_cb);
}


int uv_udp_recv_stop(uv_udp_t* handle) {
  if (handle->type != UV_UDP)
    return UV_EINVAL;
  else
    return uv__udp_recv_stop(handle);
}


void uv_walk(uv_loop_t* loop, uv_walk_cb walk_cb, void* arg) {
  struct uv__queue queue;
  struct uv__queue* q;
  uv_handle_t* h;

  uv__queue_move(&loop->handle_queue, &queue);
  while (!uv__queue_empty(&queue)) {
    q = uv__queue_head(&queue);
    h = uv__queue_data(q, uv_handle_t, handle_queue);

    uv__queue_remove(q);
    uv__queue_insert_tail(&loop->handle_queue, q);

    if (h->flags & UV_HANDLE_INTERNAL) continue;
    walk_cb(h, arg);
  }
}


static void uv__print_handles(uv_loop_t* loop, int only_active, FILE* stream) {
  const char* type;
  struct uv__queue* q;
  uv_handle_t* h;

  if (loop == NULL)
    loop = uv_default_loop();

  if (stream == NULL)
    stream = stderr;

  uv__queue_foreach(q, &loop->handle_queue) {
    h = uv__queue_data(q, uv_handle_t, handle_queue);

    if (only_active && !uv__is_active(h))
      continue;

    switch (h->type) {
#define X(uc, lc) case UV_##uc: type = #lc; break;
      UV_HANDLE_TYPE_MAP(X)
#undef X
      default: type = "<unknown>";
    }

    fprintf(stream,
            "[%c%c%c] %-8s %p\n",
            "R-"[!(h->flags & UV_HANDLE_REF)],
            "A-"[!(h->flags & UV_HANDLE_ACTIVE)],
            "I-"[!(h->flags & UV_HANDLE_INTERNAL)],
            type,
            (void*)h);
  }
}


void uv_print_all_handles(uv_loop_t* loop, FILE* stream) {
  uv__print_handles(loop, 0, stream);
}


void uv_print_active_handles(uv_loop_t* loop, FILE* stream) {
  uv__print_handles(loop, 1, stream);
}


void uv_ref(uv_handle_t* handle) {
  uv__handle_ref(handle);
}


void uv_unref(uv_handle_t* handle) {
  uv__handle_unref(handle);
}


int uv_has_ref(const uv_handle_t* handle) {
  return uv__has_ref(handle);
}


void uv_stop(uv_loop_t* loop) {
  loop->stop_flag = 1;
}


uint64_t uv_now(const uv_loop_t* loop) {
  return loop->time;
}



size_t uv__count_bufs(const uv_buf_t bufs[], unsigned int nbufs) {
  unsigned int i;
  size_t bytes;

  bytes = 0;
  for (i = 0; i < nbufs; i++)
    bytes += (size_t) bufs[i].len;

  return bytes;
}

int uv_recv_buffer_size(uv_handle_t* handle, int* value) {
  return uv__socket_sockopt(handle, SO_RCVBUF, value);
}

int uv_send_buffer_size(uv_handle_t* handle, int *value) {
  return uv__socket_sockopt(handle, SO_SNDBUF, value);
}

int uv_fs_event_getpath(uv_fs_event_t* handle, char* buffer, size_t* size) {
  size_t required_len;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  if (!uv__is_active(handle)) {
    *size = 0;
    return UV_EINVAL;
  }

  required_len = strlen(handle->path);
  if (required_len >= *size) {
    *size = required_len + 1;
    return UV_ENOBUFS;
  }

  memcpy(buffer, handle->path, required_len);
  *size = required_len;
  buffer[required_len] = '\0';

  return 0;
}

/* The windows implementation does not have the same structure layout as
 * the unix implementation (nbufs is not directly inside req but is
 * contained in a nested union/struct) so this function locates it.
*/
static unsigned int* uv__get_nbufs(uv_fs_t* req) {
#ifdef _WIN32
  return &req->fs.info.nbufs;
#else
  return &req->nbufs;
#endif
}

/* uv_fs_scandir() uses the system allocator to allocate memory on non-Windows
 * systems. So, the memory should be released using free(). On Windows,
 * uv__malloc() is used, so use uv__free() to free memory.
*/
#ifdef _WIN32
# define uv__fs_scandir_free uv__free
#else
# define uv__fs_scandir_free free
#endif

void uv__fs_scandir_cleanup(uv_fs_t* req) {
  uv__dirent_t** dents;
  unsigned int* nbufs;
  unsigned int i;
  unsigned int n;

  if (req->result >= 0) {
    dents = req->ptr;
    nbufs = uv__get_nbufs(req);

    i = 0;
    if (*nbufs > 0)
      i = *nbufs - 1;

    n = (unsigned int) req->result;
    for (; i < n; i++)
      uv__fs_scandir_free(dents[i]);
  }

  uv__fs_scandir_free(req->ptr);
  req->ptr = NULL;
}


int uv_fs_scandir_next(uv_fs_t* req, uv_dirent_t* ent) {
  uv__dirent_t** dents;
  uv__dirent_t* dent;
  unsigned int* nbufs;

  /* Check to see if req passed */
  if (req->result < 0)
    return req->result;

  /* Ptr will be null if req was canceled or no files found */
  if (!req->ptr)
    return UV_EOF;

  nbufs = uv__get_nbufs(req);
  assert(nbufs);

  dents = req->ptr;

  /* Free previous entity */
  if (*nbufs > 0)
    uv__fs_scandir_free(dents[*nbufs - 1]);

  /* End was already reached */
  if (*nbufs == (unsigned int) req->result) {
    uv__fs_scandir_free(dents);
    req->ptr = NULL;
    return UV_EOF;
  }

  dent = dents[(*nbufs)++];

  ent->name = dent->d_name;
  ent->type = uv__fs_get_dirent_type(dent);

  return 0;
}

uv_dirent_type_t uv__fs_get_dirent_type(uv__dirent_t* dent) {
  uv_dirent_type_t type;

#ifdef HAVE_DIRENT_TYPES
  switch (dent->d_type) {
    case UV__DT_DIR:
      type = UV_DIRENT_DIR;
      break;
    case UV__DT_FILE:
      type = UV_DIRENT_FILE;
      break;
    case UV__DT_LINK:
      type = UV_DIRENT_LINK;
      break;
    case UV__DT_FIFO:
      type = UV_DIRENT_FIFO;
      break;
    case UV__DT_SOCKET:
      type = UV_DIRENT_SOCKET;
      break;
    case UV__DT_CHAR:
      type = UV_DIRENT_CHAR;
      break;
    case UV__DT_BLOCK:
      type = UV_DIRENT_BLOCK;
      break;
    default:
      type = UV_DIRENT_UNKNOWN;
  }
#else
  type = UV_DIRENT_UNKNOWN;
#endif

  return type;
}

void uv__fs_readdir_cleanup(uv_fs_t* req) {
  uv_dir_t* dir;
  uv_dirent_t* dirents;
  int i;

  if (req->ptr == NULL)
    return;

  dir = req->ptr;
  dirents = dir->dirents;
  req->ptr = NULL;

  if (dirents == NULL)
    return;

  for (i = 0; i < req->result; ++i) {
    uv__free((char*) dirents[i].name);
    dirents[i].name = NULL;
  }
}


int uv_loop_configure(uv_loop_t* loop, uv_loop_option option, ...) {
  va_list ap;
  int err;

  va_start(ap, option);
  /* Any platform-agnostic options should be handled here. */
  err = uv__loop_configure(loop, option, ap);
  va_end(ap);

  return err;
}


static uv_loop_t default_loop_struct;
static uv_loop_t* default_loop_ptr;


uv_loop_t* uv_default_loop(void) {
  if (default_loop_ptr != NULL)
    return default_loop_ptr;

  if (uv_loop_init(&default_loop_struct))
    return NULL;

  default_loop_ptr = &default_loop_struct;
  return default_loop_ptr;
}


uv_loop_t* uv_loop_new(void) {
  uv_loop_t* loop;

  loop = uv__malloc(sizeof(*loop));
  if (loop == NULL)
    return NULL;

  if (uv_loop_init(loop)) {
    uv__free(loop);
    return NULL;
  }

  return loop;
}


int uv_loop_close(uv_loop_t* loop) {
  struct uv__queue* q;
  uv_handle_t* h;
#ifndef NDEBUG
  void* saved_data;
#endif

  if (uv__has_active_reqs(loop))
    return UV_EBUSY;

  uv__queue_foreach(q, &loop->handle_queue) {
    h = uv__queue_data(q, uv_handle_t, handle_queue);
    if (!(h->flags & UV_HANDLE_INTERNAL))
      return UV_EBUSY;
  }

  uv__loop_close(loop);

#ifndef NDEBUG
  saved_data = loop->data;
  memset(loop, -1, sizeof(*loop));
  loop->data = saved_data;
#endif
  if (loop == default_loop_ptr)
    default_loop_ptr = NULL;

  return 0;
}


void uv_loop_delete(uv_loop_t* loop) {
  uv_loop_t* default_loop;
  int err;

  default_loop = default_loop_ptr;

  err = uv_loop_close(loop);
  (void) err;    /* Squelch compiler warnings. */
  assert(err == 0);
  if (loop != default_loop)
    uv__free(loop);
}


int uv_read_start(uv_stream_t* stream,
                  uv_alloc_cb alloc_cb,
                  uv_read_cb read_cb) {
  if (stream == NULL || alloc_cb == NULL || read_cb == NULL)
    return UV_EINVAL;

  if (stream->flags & UV_HANDLE_CLOSING)
    return UV_EINVAL;

  if (stream->flags & UV_HANDLE_READING)
    return UV_EALREADY;

  if (!(stream->flags & UV_HANDLE_READABLE))
    return UV_ENOTCONN;

  return uv__read_start(stream, alloc_cb, read_cb);
}


void uv_os_free_environ(uv_env_item_t* envitems, int count) {
  int i;

  for (i = 0; i < count; i++) {
    uv__free(envitems[i].name);
  }

  uv__free(envitems);
}


void uv_free_cpu_info(uv_cpu_info_t* cpu_infos, int count) {
#ifdef __linux__
  (void) &count;
  uv__free(cpu_infos);
#else
  int i;

  for (i = 0; i < count; i++)
    uv__free(cpu_infos[i].model);

  uv__free(cpu_infos);
#endif  /* __linux__ */
}


/* Also covers __clang__ and __INTEL_COMPILER. Disabled on Windows because
 * threads have already been forcibly terminated by the operating system
 * by the time destructors run, ergo, it's not safe to try to clean them up.
 */
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((destructor))
#endif
void uv_library_shutdown(void) {
  static int was_shutdown;

  if (uv__exchange_int_relaxed(&was_shutdown, 1))
    return;

  uv__process_title_cleanup();
  uv__signal_cleanup();
#ifdef __MVS__
  /* TODO(itodorov) - zos: revisit when Woz compiler is available. */
  uv__os390_cleanup();
#else
  uv__threadpool_cleanup();
#endif
}


void uv__metrics_update_idle_time(uv_loop_t* loop) {
  uv__loop_metrics_t* loop_metrics;
  uint64_t entry_time;
  uint64_t exit_time;

  if (!(uv__get_internal_fields(loop)->flags & UV_METRICS_IDLE_TIME))
    return;

  loop_metrics = uv__get_loop_metrics(loop);

  /* The thread running uv__metrics_update_idle_time() is always the same
   * thread that sets provider_entry_time. So it's unnecessary to lock before
   * retrieving this value.
   */
  if (loop_metrics->provider_entry_time == 0)
    return;

  exit_time = uv_hrtime();

  uv_mutex_lock(&loop_metrics->lock);
  entry_time = loop_metrics->provider_entry_time;
  loop_metrics->provider_entry_time = 0;
  loop_metrics->provider_idle_time += exit_time - entry_time;
  uv_mutex_unlock(&loop_metrics->lock);
}


void uv__metrics_set_provider_entry_time(uv_loop_t* loop) {
  uv__loop_metrics_t* loop_metrics;
  uint64_t now;

  if (!(uv__get_internal_fields(loop)->flags & UV_METRICS_IDLE_TIME))
    return;

  now = uv_hrtime();
  loop_metrics = uv__get_loop_metrics(loop);
  uv_mutex_lock(&loop_metrics->lock);
  loop_metrics->provider_entry_time = now;
  uv_mutex_unlock(&loop_metrics->lock);
}


int uv_metrics_info(uv_loop_t* loop, uv_metrics_t* metrics) {
  memcpy(metrics,
         &uv__get_loop_metrics(loop)->metrics,
         sizeof(*metrics));

  return 0;
}


uint64_t uv_metrics_idle_time(uv_loop_t* loop) {
  uv__loop_metrics_t* loop_metrics;
  uint64_t entry_time;
  uint64_t idle_time;

  loop_metrics = uv__get_loop_metrics(loop);
  uv_mutex_lock(&loop_metrics->lock);
  idle_time = loop_metrics->provider_idle_time;
  entry_time = loop_metrics->provider_entry_time;
  uv_mutex_unlock(&loop_metrics->lock);

  if (entry_time > 0)
    idle_time += uv_hrtime() - entry_time;
  return idle_time;
}
