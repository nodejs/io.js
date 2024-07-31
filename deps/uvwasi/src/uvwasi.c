#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
# include <sched.h>
# include <sys/types.h>
# include <unistd.h>
# include <dirent.h>
# include <time.h>
#else
# define _CRT_INTERNAL_NONSTDC_NAMES 1
# include <sys/stat.h>
# include <io.h>
#endif /* _WIN32 */

#define UVWASI__READDIR_NUM_ENTRIES 1

#if !defined(_WIN32) && !defined(__ANDROID__)
# define UVWASI_FD_READDIR_SUPPORTED 1
#endif

#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
  #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#include "uvwasi.h"
#include "uvwasi_alloc.h"
#include "uv.h"
#include "uv_mapping.h"
#include "fd_table.h"
#include "clocks.h"
#include "path_resolver.h"
#include "poll_oneoff.h"
#include "sync_helpers.h"
#include "wasi_rights.h"
#include "wasi_serdes.h"
#include "debug.h"

/* IBMi PASE does not support posix_fadvise() */
#ifdef __PASE__
# undef POSIX_FADV_NORMAL
#endif

#define VALIDATE_FSTFLAGS_OR_RETURN(flags)                                    \
  do {                                                                        \
    uvwasi_fstflags_t f = flags;                                              \
    if (((f) & ~(UVWASI_FILESTAT_SET_ATIM | UVWASI_FILESTAT_SET_ATIM_NOW |    \
                 UVWASI_FILESTAT_SET_MTIM | UVWASI_FILESTAT_SET_MTIM_NOW)) || \
        ((f) & (UVWASI_FILESTAT_SET_ATIM | UVWASI_FILESTAT_SET_ATIM_NOW))     \
            == (UVWASI_FILESTAT_SET_ATIM | UVWASI_FILESTAT_SET_ATIM_NOW) ||   \
        ((f) & (UVWASI_FILESTAT_SET_MTIM | UVWASI_FILESTAT_SET_MTIM_NOW))     \
            == (UVWASI_FILESTAT_SET_MTIM | UVWASI_FILESTAT_SET_MTIM_NOW)) {   \
      return UVWASI_EINVAL;                                                   \
    }                                                                         \
  } while (0)

static uvwasi_errno_t uvwasi__get_filestat_set_times(
                                                    uvwasi_timestamp_t* st_atim,
                                                    uvwasi_timestamp_t* st_mtim,
                                                    uvwasi_fstflags_t fst_flags,
                                                    uv_file* fd,
                                                    char* path
                                                  ) {
  uvwasi_filestat_t stat;
  uvwasi_timestamp_t now;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  /* Check if either value requires the current time. */
  if ((fst_flags &
      (UVWASI_FILESTAT_SET_ATIM_NOW | UVWASI_FILESTAT_SET_MTIM_NOW)) != 0) {
    err = uvwasi__clock_gettime_realtime(&now);
    if (err != UVWASI_ESUCCESS)
      return err;
  }

  /* Check if either value is omitted. libuv doesn't have an 'omitted' option,
     so get the current stats for the file. This approach isn't perfect, but it
     will do until libuv can get better support here. */
  if ((fst_flags &
       (UVWASI_FILESTAT_SET_ATIM | UVWASI_FILESTAT_SET_ATIM_NOW)) == 0 ||
      (fst_flags &
       (UVWASI_FILESTAT_SET_MTIM | UVWASI_FILESTAT_SET_MTIM_NOW)) == 0) {

    if (fd != NULL)
      r = uv_fs_fstat(NULL, &req, *fd, NULL);
    else
      r = uv_fs_lstat(NULL, &req, path, NULL);

    if (r != 0) {
      uv_fs_req_cleanup(&req);
      return uvwasi__translate_uv_error(r);
    }

    uvwasi__stat_to_filestat(&req.statbuf, &stat);
    uv_fs_req_cleanup(&req);
  }

  /* Choose the provided time or 'now' and convert WASI timestamps from
     nanoseconds to seconds due to libuv. */
  if ((fst_flags & UVWASI_FILESTAT_SET_ATIM_NOW) != 0)
    *st_atim = now / NANOS_PER_SEC;
  else if ((fst_flags & UVWASI_FILESTAT_SET_ATIM) != 0)
    *st_atim = *st_atim / NANOS_PER_SEC;
  else
    *st_atim = stat.st_atim / NANOS_PER_SEC;

  if ((fst_flags & UVWASI_FILESTAT_SET_MTIM_NOW) != 0)
    *st_mtim = now / NANOS_PER_SEC;
  else if ((fst_flags & UVWASI_FILESTAT_SET_MTIM) != 0)
    *st_mtim = *st_mtim / NANOS_PER_SEC;
  else
    *st_mtim = stat.st_mtim / NANOS_PER_SEC;

  return UVWASI_ESUCCESS;
}

static void* default_malloc(size_t size, void* mem_user_data) {
  return malloc(size);
}

static void default_free(void* ptr, void* mem_user_data) {
  free(ptr);
}

static void* default_calloc(size_t nmemb, size_t size, void* mem_user_data) {
  return calloc(nmemb, size);
}

static void* default_realloc(void* ptr, size_t size, void* mem_user_data) {
  return realloc(ptr, size);
}

void* uvwasi__malloc(const uvwasi_t* uvwasi, size_t size) {
  return uvwasi->allocator->malloc(size, uvwasi->allocator->mem_user_data);
}

void uvwasi__free(const uvwasi_t* uvwasi, void* ptr) {
  if (ptr == NULL)
    return;

  uvwasi->allocator->free(ptr, uvwasi->allocator->mem_user_data);
}

void* uvwasi__calloc(const uvwasi_t* uvwasi, size_t nmemb, size_t size) {
  return uvwasi->allocator->calloc(nmemb,
                                   size,
                                   uvwasi->allocator->mem_user_data);
}

void* uvwasi__realloc(const uvwasi_t* uvwasi, void* ptr, size_t size) {
  return uvwasi->allocator->realloc(ptr,
                                    size,
                                    uvwasi->allocator->mem_user_data);
}

static const uvwasi_mem_t default_allocator = {
  NULL,
  default_malloc,
  default_free,
  default_calloc,
  default_realloc,
};


static uvwasi_errno_t uvwasi__lseek(uv_file fd,
                                    uvwasi_filedelta_t offset,
                                    uvwasi_whence_t whence,
                                    uvwasi_filesize_t* newoffset) {
  int real_whence;

  if (whence == UVWASI_WHENCE_CUR)
    real_whence = SEEK_CUR;
  else if (whence == UVWASI_WHENCE_END)
    real_whence = SEEK_END;
  else if (whence == UVWASI_WHENCE_SET)
    real_whence = SEEK_SET;
  else
    return UVWASI_EINVAL;

#ifdef _WIN32
  int64_t r;

  r = _lseeki64(fd, offset, real_whence);
  if (-1L == r)
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));
#else
  off_t r;

  r = lseek(fd, offset, real_whence);
  if ((off_t) -1 == r)
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));
#endif /* _WIN32 */

  *newoffset = r;
  return UVWASI_ESUCCESS;
}


static uvwasi_errno_t uvwasi__setup_iovs(const uvwasi_t* uvwasi,
                                         uv_buf_t** buffers,
                                         const uvwasi_iovec_t* iovs,
                                         uvwasi_size_t iovs_len) {
  uv_buf_t* bufs;
  uvwasi_size_t i;

  if ((iovs_len * sizeof(*bufs)) / (sizeof(*bufs)) != iovs_len)
    return UVWASI_ENOMEM;

  bufs = uvwasi__malloc(uvwasi, iovs_len * sizeof(*bufs));
  if (bufs == NULL)
    return UVWASI_ENOMEM;

  for (i = 0; i < iovs_len; ++i)
    bufs[i] = uv_buf_init(iovs[i].buf, iovs[i].buf_len);

  *buffers = bufs;
  return UVWASI_ESUCCESS;
}


static uvwasi_errno_t uvwasi__setup_ciovs(const uvwasi_t* uvwasi,
                                          uv_buf_t** buffers,
                                          const uvwasi_ciovec_t* iovs,
                                          uvwasi_size_t iovs_len) {
  uv_buf_t* bufs;
  uvwasi_size_t i;

  if ((iovs_len * sizeof(*bufs)) / (sizeof(*bufs)) != iovs_len)
    return UVWASI_ENOMEM;

  bufs = uvwasi__malloc(uvwasi, iovs_len * sizeof(*bufs));
  if (bufs == NULL)
    return UVWASI_ENOMEM;

  for (i = 0; i < iovs_len; ++i)
    bufs[i] = uv_buf_init((char*)iovs[i].buf, iovs[i].buf_len);

  *buffers = bufs;
  return UVWASI_ESUCCESS;
}

typedef struct new_connection_data_s {
  int done;
} new_connection_data_t;

void on_new_connection(uv_stream_t *server, int status) {
  // just do nothing
}

uvwasi_errno_t uvwasi_init(uvwasi_t* uvwasi, const uvwasi_options_t* options) {
  uv_fs_t realpath_req;
  uv_fs_t open_req;
  uvwasi_errno_t err;
  uvwasi_size_t args_size;
  uvwasi_size_t size;
  uvwasi_size_t offset;
  uvwasi_size_t env_count;
  uvwasi_size_t env_buf_size;
  uvwasi_size_t i;
  int r;
  struct sockaddr_in addr;

  if (uvwasi == NULL || options == NULL || options->fd_table_size == 0)
    return UVWASI_EINVAL;

  // loop is only needed if there were pre-open sockets
  uvwasi->loop = NULL;

  uvwasi->allocator = options->allocator;

  if (uvwasi->allocator == NULL)
    uvwasi->allocator = &default_allocator;

  uvwasi->argv_buf = NULL;
  uvwasi->argv = NULL;
  uvwasi->env_buf = NULL;
  uvwasi->env = NULL;
  uvwasi->fds = NULL;

  args_size = 0;
  for (i = 0; i < options->argc; ++i)
    args_size += strlen(options->argv[i]) + 1;

  uvwasi->argc = options->argc;
  uvwasi->argv_buf_size = args_size;

  if (args_size > 0) {
    uvwasi->argv_buf = uvwasi__malloc(uvwasi, args_size);
    if (uvwasi->argv_buf == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    uvwasi->argv = uvwasi__calloc(uvwasi, options->argc, sizeof(char*));
    if (uvwasi->argv == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    offset = 0;
    for (i = 0; i < options->argc; ++i) {
      size = strlen(options->argv[i]) + 1;
      memcpy(uvwasi->argv_buf + offset, options->argv[i], size);
      uvwasi->argv[i] = uvwasi->argv_buf + offset;
      offset += size;
    }
  }

  env_count = 0;
  env_buf_size = 0;
  if (options->envp != NULL) {
    while (options->envp[env_count] != NULL) {
      env_buf_size += strlen(options->envp[env_count]) + 1;
      env_count++;
    }
  }

  uvwasi->envc = env_count;
  uvwasi->env_buf_size = env_buf_size;

  if (env_buf_size > 0) {
    uvwasi->env_buf = uvwasi__malloc(uvwasi, env_buf_size);
    if (uvwasi->env_buf == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    uvwasi->env = uvwasi__calloc(uvwasi, env_count, sizeof(char*));
    if (uvwasi->env == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    offset = 0;
    for (i = 0; i < env_count; ++i) {
      size = strlen(options->envp[i]) + 1;
      memcpy(uvwasi->env_buf + offset, options->envp[i], size);
      uvwasi->env[i] = uvwasi->env_buf + offset;
      offset += size;
    }
  }

  for (i = 0; i < options->preopenc; ++i) {
    if (options->preopens[i].real_path == NULL ||
        options->preopens[i].mapped_path == NULL) {
      err = UVWASI_EINVAL;
      goto exit;
    }
  }

  for (i = 0; i < options->preopen_socketc; ++i) {
    if (options->preopen_sockets[i].address == NULL ||
        options->preopen_sockets[i].port > 65535) {
      err = UVWASI_EINVAL;
      goto exit;
    }
  }

  err = uvwasi_fd_table_init(uvwasi, options);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  for (i = 0; i < options->preopenc; ++i) {
    r = uv_fs_realpath(NULL,
                       &realpath_req,
                       options->preopens[i].real_path,
                       NULL);
    if (r != 0) {
      err = uvwasi__translate_uv_error(r);
      uv_fs_req_cleanup(&realpath_req);
      goto exit;
    }

    r = uv_fs_open(NULL, &open_req, realpath_req.ptr, 0, 0666, NULL);
    if (r < 0) {
      err = uvwasi__translate_uv_error(r);
      uv_fs_req_cleanup(&realpath_req);
      uv_fs_req_cleanup(&open_req);
      goto exit;
    }

    err = uvwasi_fd_table_insert_preopen(uvwasi,
                                         uvwasi->fds,
                                         open_req.result,
                                         options->preopens[i].mapped_path,
                                         realpath_req.ptr);
    uv_fs_req_cleanup(&realpath_req);
    uv_fs_req_cleanup(&open_req);

    if (err != UVWASI_ESUCCESS)
      goto exit;
  }

  if (options->preopen_socketc > 0) {
    uvwasi->loop = uvwasi__malloc(uvwasi, sizeof(uv_loop_t));
    r = uv_loop_init(uvwasi->loop);
    if (r != 0) {
      err = uvwasi__translate_uv_error(r);
      goto exit;
    }
  }

  for (i = 0; i < options->preopen_socketc; ++i) {
    uv_tcp_t* socket = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uvwasi->loop, socket);

    uv_ip4_addr(options->preopen_sockets[i].address, options->preopen_sockets[i].port, &addr);

    uv_tcp_bind(socket, (const struct sockaddr*)&addr, 0);
    r = uv_listen((uv_stream_t*) socket, 128, on_new_connection);
    if (r != 0) {
      err = uvwasi__translate_uv_error(r);
      goto exit;
    }

    err = uvwasi_fd_table_insert_preopen_socket(uvwasi,
                                         uvwasi->fds,
                                         socket);

    if (err != UVWASI_ESUCCESS)
      goto exit;
  }

  return UVWASI_ESUCCESS;

exit:
  uvwasi_destroy(uvwasi);
  return err;
}


void uvwasi_destroy(uvwasi_t* uvwasi) {
  if (uvwasi == NULL)
    return;

  uvwasi_fd_table_free(uvwasi, uvwasi->fds);
  uvwasi__free(uvwasi, uvwasi->argv_buf);
  uvwasi__free(uvwasi, uvwasi->argv);
  uvwasi__free(uvwasi, uvwasi->env_buf);
  uvwasi__free(uvwasi, uvwasi->env);
  if (uvwasi->loop != NULL) {
    uv_stop(uvwasi->loop);
    uv_loop_close(uvwasi->loop);
    uvwasi__free(uvwasi, uvwasi->loop);
    uvwasi->loop = NULL;
  }
  uvwasi->fds = NULL;
  uvwasi->argv_buf = NULL;
  uvwasi->argv = NULL;
  uvwasi->env_buf = NULL;
  uvwasi->env = NULL;
}


void uvwasi_options_init(uvwasi_options_t* options) {
  if (options == NULL)
    return;

  options->in = 0;
  options->out = 1;
  options->err = 2;
  options->fd_table_size = 3;
  options->argc = 0;
  options->argv = NULL;
  options->envp = NULL;
  options->preopenc = 0;
  options->preopens = NULL;
  options->preopen_socketc = 0;
  options->preopen_sockets = NULL;
  options->allocator = NULL;
}


uvwasi_errno_t uvwasi_embedder_remap_fd(uvwasi_t* uvwasi,
                                        const uvwasi_fd_t fd,
                                        uv_file new_host_fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  wrap->fd = new_host_fd;
  uv_mutex_unlock(&wrap->mutex);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_args_get(uvwasi_t* uvwasi, char** argv, char* argv_buf) {
  uvwasi_size_t i;

  UVWASI_DEBUG("uvwasi_args_get(uvwasi=%p, argv=%p, argv_buf=%p)\n",
               uvwasi,
               argv,
               argv_buf);

  if (uvwasi == NULL || argv == NULL || argv_buf == NULL)
    return UVWASI_EINVAL;

  for (i = 0; i < uvwasi->argc; ++i) {
    argv[i] = argv_buf + (uvwasi->argv[i] - uvwasi->argv_buf);
  }

  memcpy(argv_buf, uvwasi->argv_buf, uvwasi->argv_buf_size);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_args_sizes_get(uvwasi_t* uvwasi,
                                     uvwasi_size_t* argc,
                                     uvwasi_size_t* argv_buf_size) {
  UVWASI_DEBUG("uvwasi_args_sizes_get(uvwasi=%p, argc=%p, argv_buf_size=%p)\n",
               uvwasi,
               argc,
               argv_buf_size);

  if (uvwasi == NULL || argc == NULL || argv_buf_size == NULL)
    return UVWASI_EINVAL;

  *argc = uvwasi->argc;
  *argv_buf_size = uvwasi->argv_buf_size;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_clock_res_get(uvwasi_t* uvwasi,
                                    uvwasi_clockid_t clock_id,
                                    uvwasi_timestamp_t* resolution) {
  UVWASI_DEBUG("uvwasi_clock_res_get(uvwasi=%p, clock_id=%d, resolution=%p)\n",
               uvwasi,
               clock_id,
               resolution);

  if (uvwasi == NULL || resolution == NULL)
    return UVWASI_EINVAL;

  switch (clock_id) {
    case UVWASI_CLOCK_MONOTONIC:
    case UVWASI_CLOCK_REALTIME:
      *resolution = 1;  /* Nanosecond precision. */
      return UVWASI_ESUCCESS;
    case UVWASI_CLOCK_PROCESS_CPUTIME_ID:
      return uvwasi__clock_getres_process_cputime(resolution);
    case UVWASI_CLOCK_THREAD_CPUTIME_ID:
      return uvwasi__clock_getres_thread_cputime(resolution);
    default:
      return UVWASI_EINVAL;
  }
}


uvwasi_errno_t uvwasi_clock_time_get(uvwasi_t* uvwasi,
                                     uvwasi_clockid_t clock_id,
                                     uvwasi_timestamp_t precision,
                                     uvwasi_timestamp_t* time) {
  UVWASI_DEBUG("uvwasi_clock_time_get(uvwasi=%p, clock_id=%d, "
               "precision=%"PRIu64", time=%p)\n",
               uvwasi,
               clock_id,
               precision,
               time);

  if (uvwasi == NULL || time == NULL)
    return UVWASI_EINVAL;

  switch (clock_id) {
    case UVWASI_CLOCK_MONOTONIC:
      *time = uv_hrtime();
      return UVWASI_ESUCCESS;
    case UVWASI_CLOCK_REALTIME:
      return uvwasi__clock_gettime_realtime(time);
    case UVWASI_CLOCK_PROCESS_CPUTIME_ID:
      return uvwasi__clock_gettime_process_cputime(time);
    case UVWASI_CLOCK_THREAD_CPUTIME_ID:
      return uvwasi__clock_gettime_thread_cputime(time);
    default:
      return UVWASI_EINVAL;
  }
}


uvwasi_errno_t uvwasi_environ_get(uvwasi_t* uvwasi,
                                  char** environment,
                                  char* environ_buf) {
  uvwasi_size_t i;

  UVWASI_DEBUG("uvwasi_environ_get(uvwasi=%p, environment=%p, "
               "environ_buf=%p)\n",
               uvwasi,
               environment,
               environ_buf);

  if (uvwasi == NULL || environment == NULL || environ_buf == NULL)
    return UVWASI_EINVAL;

  for (i = 0; i < uvwasi->envc; ++i) {
    environment[i] = environ_buf + (uvwasi->env[i] - uvwasi->env_buf);
  }

  memcpy(environ_buf, uvwasi->env_buf, uvwasi->env_buf_size);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_environ_sizes_get(uvwasi_t* uvwasi,
                                        uvwasi_size_t* environ_count,
                                        uvwasi_size_t* environ_buf_size) {
  UVWASI_DEBUG("uvwasi_environ_sizes_get(uvwasi=%p, environ_count=%p, "
               "environ_buf_size=%p)\n",
               uvwasi,
               environ_count,
               environ_buf_size);

  if (uvwasi == NULL || environ_count == NULL || environ_buf_size == NULL)
    return UVWASI_EINVAL;

  *environ_count = uvwasi->envc;
  *environ_buf_size = uvwasi->env_buf_size;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_advise(uvwasi_t* uvwasi,
                                uvwasi_fd_t fd,
                                uvwasi_filesize_t offset,
                                uvwasi_filesize_t len,
                                uvwasi_advice_t advice) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;
#ifdef POSIX_FADV_NORMAL
  int mapped_advice;
#endif /* POSIX_FADV_NORMAL */

  UVWASI_DEBUG("uvwasi_fd_advise(uvwasi=%p, fd=%d, offset=%"PRIu64", "
               "len=%"PRIu64", advice=%d)\n",
               uvwasi,
               fd,
               offset,
               len,
               advice);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  switch (advice) {
    case UVWASI_ADVICE_DONTNEED:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_DONTNEED;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_NOREUSE:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_NOREUSE;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_NORMAL:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_NORMAL;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_RANDOM:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_RANDOM;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_SEQUENTIAL:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_SEQUENTIAL;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_WILLNEED:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_WILLNEED;
#endif /* POSIX_FADV_NORMAL */
      break;
    default:
      return UVWASI_EINVAL;
  }

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_ADVISE, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_fstat(NULL, &req, wrap->fd, NULL);
  if (r == -1) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  if (S_ISDIR(req.statbuf.st_mode)) {
    err = UVWASI_EBADF;
    goto exit;
  }

  err = UVWASI_ESUCCESS;

#ifdef POSIX_FADV_NORMAL
  r = posix_fadvise(wrap->fd, offset, len, mapped_advice);
  if (r != 0)
    err = uvwasi__translate_uv_error(uv_translate_sys_error(r));
#endif /* POSIX_FADV_NORMAL */
exit:
  uv_mutex_unlock(&wrap->mutex);
  uv_fs_req_cleanup(&req);
  return err;
}


uvwasi_errno_t uvwasi_fd_allocate(uvwasi_t* uvwasi,
                                  uvwasi_fd_t fd,
                                  uvwasi_filesize_t offset,
                                  uvwasi_filesize_t len) {
#if !defined(__POSIX__)
  uv_fs_t req;
  uint64_t st_size;
#endif /* !__POSIX__ */
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_fd_allocate(uvwasi=%p, fd=%d, offset=%"PRIu64", "
               "len=%"PRIu64")\n",
               uvwasi,
               fd,
               offset,
               len);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_ALLOCATE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* Try to use posix_fallocate(). If that's not an option, fall back to the
     race condition prone combination of fstat() + ftruncate(). */
#if defined(__POSIX__)
  r = posix_fallocate(wrap->fd, offset, len);
  if (r != 0) {
    err = uvwasi__translate_uv_error(uv_translate_sys_error(r));
    goto exit;
  }
#else
  r = uv_fs_fstat(NULL, &req, wrap->fd, NULL);
  st_size = req.statbuf.st_size;
  uv_fs_req_cleanup(&req);
  if (r != 0) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  if (st_size < offset + len) {
    r = uv_fs_ftruncate(NULL, &req, wrap->fd, offset + len, NULL);
    if (r != 0) {
      err = uvwasi__translate_uv_error(r);
      goto exit;
    }
  }
#endif /* __POSIX__ */

  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  return err;
}

uvwasi_errno_t uvwasi_fd_close(uvwasi_t* uvwasi, uvwasi_fd_t fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err = 0;
  uv_fs_t req;
  int r;

  UVWASI_DEBUG("uvwasi_fd_close(uvwasi=%p, fd=%d)\n", uvwasi, fd);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  uvwasi_fd_table_lock(uvwasi->fds);

  err = uvwasi_fd_table_get_nolock(uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  if (wrap->sock == NULL) {
    r = uv_fs_close(NULL, &req, wrap->fd, NULL);
    uv_mutex_unlock(&wrap->mutex);
    uv_fs_req_cleanup(&req);
  } else {
    r = 0;
    err = free_handle_sync(uvwasi, (uv_handle_t*) wrap->sock);
    uv_mutex_unlock(&wrap->mutex);
    if (err != UVWASI_ESUCCESS) {
      goto exit;
    }   
  }

  if (r != 0) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  err = uvwasi_fd_table_remove_nolock(uvwasi, uvwasi->fds, fd);

exit:
  uvwasi_fd_table_unlock(uvwasi->fds);
  return err;
}


uvwasi_errno_t uvwasi_fd_datasync(uvwasi_t* uvwasi, uvwasi_fd_t fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  UVWASI_DEBUG("uvwasi_fd_datasync(uvwasi=%p, fd=%d)\n", uvwasi, fd);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_DATASYNC,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_fdatasync(NULL, &req, wrap->fd, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_fdstat_get(uvwasi_t* uvwasi,
                                    uvwasi_fd_t fd,
                                    uvwasi_fdstat_t* buf) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
#ifndef _WIN32
  int r;
#endif

  UVWASI_DEBUG("uvwasi_fd_fdstat_get(uvwasi=%p, fd=%d, buf=%p)\n",
               uvwasi,
               fd,
               buf);

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  buf->fs_filetype = wrap->type;
  buf->fs_rights_base = wrap->rights_base;
  buf->fs_rights_inheriting = wrap->rights_inheriting;
#ifdef _WIN32
  buf->fs_flags = 0;  /* TODO(cjihrig): Missing Windows support. */
#else
  r = fcntl(wrap->fd, F_GETFL);
  if (r < 0) {
    err = uvwasi__translate_uv_error(uv_translate_sys_error(errno));
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }
  buf->fs_flags = r;
#endif /* _WIN32 */

  uv_mutex_unlock(&wrap->mutex);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_fdstat_set_flags(uvwasi_t* uvwasi,
                                          uvwasi_fd_t fd,
                                          uvwasi_fdflags_t flags) {
#ifdef _WIN32
  UVWASI_DEBUG("uvwasi_fd_fdstat_set_flags(uvwasi=%p, fd=%d, flags=%d)\n",
               uvwasi,
               fd,
               flags);

  /* TODO(cjihrig): Windows is not supported. */
  return UVWASI_ENOSYS;
#else
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  int mapped_flags;
  int r;

  UVWASI_DEBUG("uvwasi_fd_fdstat_set_flags(uvwasi=%p, fd=%d, flags=%d)\n",
               uvwasi,
               fd,
               flags);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FDSTAT_SET_FLAGS,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  mapped_flags = 0;

  if ((flags & UVWASI_FDFLAG_APPEND) == UVWASI_FDFLAG_APPEND)
    mapped_flags |= O_APPEND;

  if ((flags & UVWASI_FDFLAG_DSYNC) == UVWASI_FDFLAG_DSYNC)
#ifdef O_DSYNC
    mapped_flags |= O_DSYNC;
#else
    mapped_flags |= O_SYNC;
#endif /* O_DSYNC */

  if ((flags & UVWASI_FDFLAG_NONBLOCK) == UVWASI_FDFLAG_NONBLOCK)
    mapped_flags |= O_NONBLOCK;

  if ((flags & UVWASI_FDFLAG_RSYNC) == UVWASI_FDFLAG_RSYNC)
#ifdef O_RSYNC
    mapped_flags |= O_RSYNC;
#else
    mapped_flags |= O_SYNC;
#endif /* O_RSYNC */

  if ((flags & UVWASI_FDFLAG_SYNC) == UVWASI_FDFLAG_SYNC)
    mapped_flags |= O_SYNC;

  r = fcntl(wrap->fd, F_SETFL, mapped_flags);
  if (r < 0)
    err = uvwasi__translate_uv_error(uv_translate_sys_error(errno));
  else
    err = UVWASI_ESUCCESS;

  uv_mutex_unlock(&wrap->mutex);
  return err;
#endif /* _WIN32 */
}


uvwasi_errno_t uvwasi_fd_fdstat_set_rights(uvwasi_t* uvwasi,
                                           uvwasi_fd_t fd,
                                           uvwasi_rights_t fs_rights_base,
                                           uvwasi_rights_t fs_rights_inheriting
                                          ) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  UVWASI_DEBUG("uvwasi_fd_fdstat_set_rights(uvwasi=%p, fd=%d, "
               "fs_rights_base=%"PRIu64", fs_rights_inheriting=%"PRIu64")\n",
               uvwasi,
               fd,
               fs_rights_base,
               fs_rights_inheriting);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* Check for attempts to add new permissions. */
  if ((fs_rights_base | wrap->rights_base) > wrap->rights_base) {
    err = UVWASI_ENOTCAPABLE;
    goto exit;
  }

  if ((fs_rights_inheriting | wrap->rights_inheriting) >
      wrap->rights_inheriting) {
    err = UVWASI_ENOTCAPABLE;
    goto exit;
  }

  wrap->rights_base = fs_rights_base;
  wrap->rights_inheriting = fs_rights_inheriting;
  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_fd_filestat_get(uvwasi_t* uvwasi,
                                      uvwasi_fd_t fd,
                                      uvwasi_filestat_t* buf) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_fd_filestat_get(uvwasi=%p, fd=%d, buf=%p)\n",
               uvwasi,
               fd,
               buf);

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FILESTAT_GET,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_fstat(NULL, &req, wrap->fd, NULL);
  if (r != 0) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  uvwasi__stat_to_filestat(&req.statbuf, buf);
  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  uv_fs_req_cleanup(&req);
  return err;
}


uvwasi_errno_t uvwasi_fd_filestat_set_size(uvwasi_t* uvwasi,
                                           uvwasi_fd_t fd,
                                           uvwasi_filesize_t st_size) {
  /* TODO(cjihrig): uv_fs_ftruncate() takes an int64_t. st_size is uint64_t. */
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_fd_filestat_set_size(uvwasi=%p, fd=%d, "
               "st_size=%"PRIu64")\n",
               uvwasi,
               fd,
               st_size);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FILESTAT_SET_SIZE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_ftruncate(NULL, &req, wrap->fd, st_size, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_filestat_set_times(uvwasi_t* uvwasi,
                                            uvwasi_fd_t fd,
                                            uvwasi_timestamp_t st_atim,
                                            uvwasi_timestamp_t st_mtim,
                                            uvwasi_fstflags_t fst_flags) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_timestamp_t atim;
  uvwasi_timestamp_t mtim;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_fd_filestat_set_times(uvwasi=%p, fd=%d, "
               "st_atim=%"PRIu64", st_mtim=%"PRIu64", fst_flags=%d)\n",
               uvwasi,
               fd,
               st_atim,
               st_mtim,
               fst_flags);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  VALIDATE_FSTFLAGS_OR_RETURN(fst_flags);

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FILESTAT_SET_TIMES,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  atim = st_atim;
  mtim = st_mtim;
  err = uvwasi__get_filestat_set_times(&atim,
                                       &mtim,
                                       fst_flags,
                                       &wrap->fd,
                                       NULL);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  /* libuv does not currently support nanosecond precision. */
  r = uv_fs_futime(NULL, &req, wrap->fd, atim, mtim, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_pread(uvwasi_t* uvwasi,
                               uvwasi_fd_t fd,
                               const uvwasi_iovec_t* iovs,
                               uvwasi_size_t iovs_len,
                               uvwasi_filesize_t offset,
                               uvwasi_size_t* nread) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvread;
  int r;

  UVWASI_DEBUG("uvwasi_fd_pread(uvwasi=%p, fd=%d, iovs=%p, iovs_len=%d, "
               "offset=%"PRIu64", nread=%p)\n",
               uvwasi,
               fd,
               iovs,
               iovs_len,
               offset,
               nread);

  if (uvwasi == NULL || (iovs == NULL && iovs_len > 0) || nread == NULL || offset > INT64_MAX)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_READ | UVWASI_RIGHT_FD_SEEK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  // libuv returns EINVAL in this case.  To behave consistently with other
  // Wasm runtimes, return OK here with a no-op.
  if (iovs_len == 0) {
    uv_mutex_unlock(&wrap->mutex);
    *nread = 0;
    return UVWASI_ESUCCESS;
  }

  err = uvwasi__setup_iovs(uvwasi, &bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_fs_read(NULL, &req, wrap->fd, bufs, iovs_len, offset, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvread = req.result;
  uv_fs_req_cleanup(&req);
  uvwasi__free(uvwasi, bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nread = (uvwasi_size_t) uvread;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_prestat_get(uvwasi_t* uvwasi,
                                     uvwasi_fd_t fd,
                                     uvwasi_prestat_t* buf) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  UVWASI_DEBUG("uvwasi_fd_prestat_get(uvwasi=%p, fd=%d, buf=%p)\n",
               uvwasi,
               fd,
               buf);

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;
  if (wrap->preopen != 1) {
    err = UVWASI_EINVAL;
    goto exit;
  }

  buf->pr_type = UVWASI_PREOPENTYPE_DIR;
  buf->u.dir.pr_name_len = strlen(wrap->path);
  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_fd_prestat_dir_name(uvwasi_t* uvwasi,
                                          uvwasi_fd_t fd,
                                          char* path,
                                          uvwasi_size_t path_len) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  size_t size;

  UVWASI_DEBUG("uvwasi_fd_prestat_dir_name(uvwasi=%p, fd=%d, path=%p, "
               "path_len=%d)\n",
               uvwasi,
               fd,
               path,
               path_len);

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;
  if (wrap->preopen != 1) {
    err = UVWASI_EBADF;
    goto exit;
  }

  size = strlen(wrap->path);
  if (size > (size_t) path_len) {
    err = UVWASI_ENOBUFS;
    goto exit;
  }

  memcpy(path, wrap->path, size);
  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_fd_pwrite(uvwasi_t* uvwasi,
                                uvwasi_fd_t fd,
                                const uvwasi_ciovec_t* iovs,
                                uvwasi_size_t iovs_len,
                                uvwasi_filesize_t offset,
                                uvwasi_size_t* nwritten) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvwritten;
  int r;

  UVWASI_DEBUG("uvwasi_fd_pwrite(uvwasi=%p, fd=%d, iovs=%p, iovs_len=%d, "
               "offset=%"PRIu64", nwritten=%p)\n",
               uvwasi,
               fd,
               iovs,
               iovs_len,
               offset,
               nwritten);

  if (uvwasi == NULL || (iovs == NULL && iovs_len > 0) || nwritten == NULL || offset > INT64_MAX)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_WRITE | UVWASI_RIGHT_FD_SEEK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  // libuv returns EINVAL in this case.  To behave consistently with other
  // Wasm runtimes, return OK here with a no-op.
  if (iovs_len == 0) {
    uv_mutex_unlock(&wrap->mutex);
    *nwritten = 0;
    return UVWASI_ESUCCESS;
  }

  err = uvwasi__setup_ciovs(uvwasi, &bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_fs_write(NULL, &req, wrap->fd, bufs, iovs_len, offset, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvwritten = req.result;
  uv_fs_req_cleanup(&req);
  uvwasi__free(uvwasi, bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nwritten = (uvwasi_size_t) uvwritten;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_read(uvwasi_t* uvwasi,
                              uvwasi_fd_t fd,
                              const uvwasi_iovec_t* iovs,
                              uvwasi_size_t iovs_len,
                              uvwasi_size_t* nread) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvread;
  int r;

  UVWASI_DEBUG("uvwasi_fd_read(uvwasi=%p, fd=%d, iovs=%p, iovs_len=%d, "
               "nread=%p)\n",
               uvwasi,
               fd,
               iovs,
               iovs_len,
               nread);
  if (uvwasi == NULL || (iovs == NULL && iovs_len > 0) || nread == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_READ, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  // libuv returns EINVAL in this case.  To behave consistently with other
  // Wasm runtimes, return OK here with a no-op.
  if (iovs_len == 0) {
    uv_mutex_unlock(&wrap->mutex);
    *nread = 0;
    return UVWASI_ESUCCESS;
  }

  err = uvwasi__setup_iovs(uvwasi, &bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_fs_read(NULL, &req, wrap->fd, bufs, iovs_len, -1, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvread = req.result;
  uv_fs_req_cleanup(&req);
  uvwasi__free(uvwasi, bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nread = (uvwasi_size_t) uvread;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_readdir(uvwasi_t* uvwasi,
                                 uvwasi_fd_t fd,
                                 void* buf,
                                 uvwasi_size_t buf_len,
                                 uvwasi_dircookie_t cookie,
                                 uvwasi_size_t* bufused) {
#if defined(UVWASI_FD_READDIR_SUPPORTED)
  /* TODO(cjihrig): Avoid opening and closing the directory on each call. */
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_dirent_t dirent;
  uv_dirent_t dirents[UVWASI__READDIR_NUM_ENTRIES];
  uv_dir_t* dir;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t name_len;
  size_t available;
  size_t size_to_cp;
  long tell;
  int i;
  int r;
#endif /* defined(UVWASI_FD_READDIR_SUPPORTED) */

  UVWASI_DEBUG("uvwasi_fd_readdir(uvwasi=%p, fd=%d, buf=%p, buf_len=%d, "
               "cookie=%"PRIu64", bufused=%p)\n",
               uvwasi,
               fd,
               buf,
               buf_len,
               cookie,
               bufused);

  if (uvwasi == NULL || buf == NULL || bufused == NULL)
    return UVWASI_EINVAL;

#if defined(UVWASI_FD_READDIR_SUPPORTED)
  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_READDIR,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* Open the directory. */
  r = uv_fs_opendir(NULL, &req, wrap->real_path, NULL);
  if (r != 0) {
    uv_mutex_unlock(&wrap->mutex);
    return uvwasi__translate_uv_error(r);
  }

  /* Setup for reading the directory. */
  dir = req.ptr;
  dir->dirents = dirents;
  dir->nentries = UVWASI__READDIR_NUM_ENTRIES;
  uv_fs_req_cleanup(&req);

  /* Seek to the proper location in the directory. */
  if (cookie != UVWASI_DIRCOOKIE_START)
    seekdir(dir->dir, cookie);

  /* Read the directory entries into the provided buffer. */
  err = UVWASI_ESUCCESS;
  *bufused = 0;
  while (0 != (r = uv_fs_readdir(NULL, &req, dir, NULL))) {
    if (r < 0) {
      err = uvwasi__translate_uv_error(r);
      uv_fs_req_cleanup(&req);
      goto exit;
    }

    available = 0;

    for (i = 0; i < r; i++) {
      tell = telldir(dir->dir);
      if (tell < 0) {
        err = uvwasi__translate_uv_error(uv_translate_sys_error(errno));
        uv_fs_req_cleanup(&req);
        goto exit;
      }

      name_len = strlen(dirents[i].name);
      dirent.d_next = (uvwasi_dircookie_t) tell;
      /* TODO(cjihrig): libuv doesn't provide d_ino, and d_type is not
                        supported on all platforms. Use stat()? */
      dirent.d_ino = 0;
      dirent.d_namlen = name_len;

      switch (dirents[i].type) {
        case UV_DIRENT_FILE:
          dirent.d_type = UVWASI_FILETYPE_REGULAR_FILE;
          break;
        case UV_DIRENT_DIR:
          dirent.d_type = UVWASI_FILETYPE_DIRECTORY;
          break;
        case UV_DIRENT_SOCKET:
          dirent.d_type = UVWASI_FILETYPE_SOCKET_STREAM;
          break;
        case UV_DIRENT_LINK:
          dirent.d_type = UVWASI_FILETYPE_SYMBOLIC_LINK;
          break;
        case UV_DIRENT_CHAR:
          dirent.d_type = UVWASI_FILETYPE_CHARACTER_DEVICE;
          break;
        case UV_DIRENT_BLOCK:
          dirent.d_type = UVWASI_FILETYPE_BLOCK_DEVICE;
          break;
        case UV_DIRENT_FIFO:
        case UV_DIRENT_UNKNOWN:
        default:
          dirent.d_type = UVWASI_FILETYPE_UNKNOWN;
          break;
      }

      /* Write dirent to the buffer if it will fit. */
      if (UVWASI_SERDES_SIZE_dirent_t + *bufused > buf_len) {
        /* If there are more entries to be written to the buffer we set
         * bufused, which is the return value, to the length of the buffer
         * which indicates that there are more entries to be read.
         */
        *bufused = buf_len;
        break;
      }

      uvwasi_serdes_write_dirent_t(buf, *bufused, &dirent);
      *bufused += UVWASI_SERDES_SIZE_dirent_t;
      available = buf_len - *bufused;

      /* Write as much of the entry name to the buffer as possible. */
      size_to_cp = name_len > available ? available : name_len;
      memcpy((char*)buf + *bufused, dirents[i].name, size_to_cp);
      *bufused += size_to_cp;
      available = buf_len - *bufused;
    }

    uv_fs_req_cleanup(&req);

    if (available == 0)
      break;
  }

exit:
  /* Close the directory. */
  r = uv_fs_closedir(NULL, &req, dir, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return err;
#else
  /* TODO(cjihrig): Need a solution for Windows and Android. */
  return UVWASI_ENOSYS;
#endif /* defined(UVWASI_FD_READDIR_SUPPORTED) */
}


uvwasi_errno_t uvwasi_fd_renumber(uvwasi_t* uvwasi,
                                  uvwasi_fd_t from,
                                  uvwasi_fd_t to) {
  UVWASI_DEBUG("uvwasi_fd_renumber(uvwasi=%p, from=%d, to=%d)\n",
               uvwasi,
               from,
               to);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  return uvwasi_fd_table_renumber(uvwasi, uvwasi->fds, to, from);
}


uvwasi_errno_t uvwasi_fd_seek(uvwasi_t* uvwasi,
                              uvwasi_fd_t fd,
                              uvwasi_filedelta_t offset,
                              uvwasi_whence_t whence,
                              uvwasi_filesize_t* newoffset) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  UVWASI_DEBUG("uvwasi_fd_seek(uvwasi=%p, fd=%d, offset=%"PRId64", "
               "whence=%d, newoffset=%p)\n",
               uvwasi,
               fd,
               offset,
               whence,
               newoffset);

  if (uvwasi == NULL || newoffset == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_SEEK, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__lseek(wrap->fd, offset, whence, newoffset);
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_fd_sync(uvwasi_t* uvwasi, uvwasi_fd_t fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_fd_sync(uvwasi=%p, fd=%d)\n", uvwasi, fd);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_SYNC,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_fsync(NULL, &req, wrap->fd, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_tell(uvwasi_t* uvwasi,
                              uvwasi_fd_t fd,
                              uvwasi_filesize_t* offset) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  UVWASI_DEBUG("uvwasi_fd_tell(uvwasi=%p, fd=%d, offset=%p)\n",
               uvwasi,
               fd,
               offset);

  if (uvwasi == NULL || offset == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_TELL, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__lseek(wrap->fd, 0, UVWASI_WHENCE_CUR, offset);
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_fd_write(uvwasi_t* uvwasi,
                               uvwasi_fd_t fd,
                               const uvwasi_ciovec_t* iovs,
                               uvwasi_size_t iovs_len,
                               uvwasi_size_t* nwritten) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvwritten;
  int r;

  UVWASI_DEBUG("uvwasi_fd_write(uvwasi=%p, fd=%d, iovs=%p, iovs_len=%d, "
               "nwritten=%p)\n",
               uvwasi,
               fd,
               iovs,
               iovs_len,
               nwritten);

  if (uvwasi == NULL || (iovs == NULL && iovs_len > 0) || nwritten == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_WRITE, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  // libuv returns EINVAL in this case.  To behave consistently with other
  // Wasm runtimes, return OK here with a no-op.
  if (iovs_len == 0) {
    uv_mutex_unlock(&wrap->mutex);
    *nwritten = 0;
    return UVWASI_ESUCCESS;
  }

  err = uvwasi__setup_ciovs(uvwasi, &bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_fs_write(NULL, &req, wrap->fd, bufs, iovs_len, -1, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvwritten = req.result;
  uv_fs_req_cleanup(&req);
  uvwasi__free(uvwasi, bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nwritten = (uvwasi_size_t) uvwritten;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_create_directory(uvwasi_t* uvwasi,
                                            uvwasi_fd_t fd,
                                            const char* path,
                                            uvwasi_size_t path_len) {
  char* resolved_path;
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_path_create_directory(uvwasi=%p, fd=%d, path='%s', "
               "path_len=%d)\n",
               uvwasi,
               fd,
               path,
               path_len);

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_CREATE_DIRECTORY,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(uvwasi, wrap, path, path_len, &resolved_path, 0);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  r = uv_fs_mkdir(NULL, &req, resolved_path, 0777, NULL);
  uv_fs_req_cleanup(&req);
  uvwasi__free(uvwasi, resolved_path);

  if (r != 0) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_path_filestat_get(uvwasi_t* uvwasi,
                                        uvwasi_fd_t fd,
                                        uvwasi_lookupflags_t flags,
                                        const char* path,
                                        uvwasi_size_t path_len,
                                        uvwasi_filestat_t* buf) {
  char* resolved_path;
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_path_filestat_get(uvwasi=%p, fd=%d, flags=%d, "
               "path='%s', path_len=%d, buf=%p)\n",
               uvwasi,
               fd,
               flags,
               path,
               path_len,
               buf);

  if (uvwasi == NULL || path == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_FILESTAT_GET,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(uvwasi,
                             wrap,
                             path,
                             path_len,
                             &resolved_path,
                             flags);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  r = uv_fs_lstat(NULL, &req, resolved_path, NULL);
  uvwasi__free(uvwasi, resolved_path);
  if (r != 0) {
    uv_fs_req_cleanup(&req);
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  uvwasi__stat_to_filestat(&req.statbuf, buf);
  uv_fs_req_cleanup(&req);
  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_path_filestat_set_times(uvwasi_t* uvwasi,
                                              uvwasi_fd_t fd,
                                              uvwasi_lookupflags_t flags,
                                              const char* path,
                                              uvwasi_size_t path_len,
                                              uvwasi_timestamp_t st_atim,
                                              uvwasi_timestamp_t st_mtim,
                                              uvwasi_fstflags_t fst_flags) {
  char* resolved_path;
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_timestamp_t atim;
  uvwasi_timestamp_t mtim;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_path_filestat_set_times(uvwasi=%p, fd=%d, "
               "flags=%d, path='%s', path_len=%d, "
               "st_atim=%"PRIu64", st_mtim=%"PRIu64", fst_flags=%d)\n",
               uvwasi,
               fd,
               flags,
               path,
               path_len,
               st_atim,
               st_mtim,
               fst_flags);

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_FILESTAT_SET_TIMES,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  VALIDATE_FSTFLAGS_OR_RETURN(fst_flags);

  err = uvwasi__resolve_path(uvwasi,
                             wrap,
                             path,
                             path_len,
                             &resolved_path,
                             flags);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  atim = st_atim;
  mtim = st_mtim;
  err = uvwasi__get_filestat_set_times(&atim,
                                       &mtim,
                                       fst_flags,
                                       NULL,
                                       resolved_path);
  if (err != UVWASI_ESUCCESS) {
    uvwasi__free(uvwasi, resolved_path);
    goto exit;
  }

  /* libuv does not currently support nanosecond precision. */
  r = uv_fs_lutime(NULL, &req, resolved_path, atim, mtim, NULL);
  uvwasi__free(uvwasi, resolved_path);
  uv_fs_req_cleanup(&req);

  if (r != 0) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


uvwasi_errno_t uvwasi_path_link(uvwasi_t* uvwasi,
                                uvwasi_fd_t old_fd,
                                uvwasi_lookupflags_t old_flags,
                                const char* old_path,
                                uvwasi_size_t old_path_len,
                                uvwasi_fd_t new_fd,
                                const char* new_path,
                                uvwasi_size_t new_path_len) {
  char* resolved_old_path;
  char* resolved_new_path;
  struct uvwasi_fd_wrap_t* old_wrap;
  struct uvwasi_fd_wrap_t* new_wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  UVWASI_DEBUG("uvwasi_path_link(uvwasi=%p, old_fd=%d, old_flags=%d, "
               "old_path='%s', old_path_len=%d, new_fd=%d, new_path='%s', "
               "new_path_len=%d)\n",
               uvwasi,
               old_fd,
               old_flags,
               old_path,
               old_path_len,
               new_fd,
               new_path,
               new_path_len);

  if (uvwasi == NULL || old_path == NULL || new_path == NULL)
    return UVWASI_EINVAL;

  uvwasi_fd_table_lock(uvwasi->fds);

  if (old_fd == new_fd) {
    err = uvwasi_fd_table_get_nolock(uvwasi->fds,
                                     old_fd,
                                     &old_wrap,
                                     UVWASI_RIGHT_PATH_LINK_SOURCE |
                                     UVWASI_RIGHT_PATH_LINK_TARGET,
                                     0);
    new_wrap = old_wrap;
  } else {
    err = uvwasi_fd_table_get_nolock(uvwasi->fds,
                                     old_fd,
                                     &old_wrap,
                                     UVWASI_RIGHT_PATH_LINK_SOURCE,
                                     0);
    if (err != UVWASI_ESUCCESS) {
      uvwasi_fd_table_unlock(uvwasi->fds);
      return err;
    }

    err = uvwasi_fd_table_get_nolock(uvwasi->fds,
                                     new_fd,
                                     &new_wrap,
                                     UVWASI_RIGHT_PATH_LINK_TARGET,
                                     0);
    if (err != UVWASI_ESUCCESS)
      uv_mutex_unlock(&old_wrap->mutex);
  }

  uvwasi_fd_table_unlock(uvwasi->fds);

  if (err != UVWASI_ESUCCESS)
    return err;

  resolved_old_path = NULL;
  resolved_new_path = NULL;

  err = uvwasi__resolve_path(uvwasi,
                             old_wrap,
                             old_path,
                             old_path_len,
                             &resolved_old_path,
                             old_flags);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  err = uvwasi__resolve_path(uvwasi,
                             new_wrap,
                             new_path,
                             new_path_len,
                             &resolved_new_path,
                             0);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  r = uv_fs_link(NULL, &req, resolved_old_path, resolved_new_path, NULL);
  uv_fs_req_cleanup(&req);
  if (r != 0) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&new_wrap->mutex);
  if (old_fd != new_fd)
    uv_mutex_unlock(&old_wrap->mutex);

  uvwasi__free(uvwasi, resolved_old_path);
  uvwasi__free(uvwasi, resolved_new_path);
  return err;
}


uvwasi_errno_t uvwasi_path_open(uvwasi_t* uvwasi,
                                uvwasi_fd_t dirfd,
                                uvwasi_lookupflags_t dirflags,
                                const char* path,
                                uvwasi_size_t path_len,
                                uvwasi_oflags_t o_flags,
                                uvwasi_rights_t fs_rights_base,
                                uvwasi_rights_t fs_rights_inheriting,
                                uvwasi_fdflags_t fs_flags,
                                uvwasi_fd_t* fd) {
  char* resolved_path;
  uvwasi_rights_t needed_inheriting;
  uvwasi_rights_t needed_base;
  uvwasi_rights_t max_base;
  uvwasi_rights_t max_inheriting;
  struct uvwasi_fd_wrap_t* dirfd_wrap;
  struct uvwasi_fd_wrap_t *wrap;
  uvwasi_filetype_t filetype;
  uvwasi_errno_t err;
  uv_fs_t req;
  int flags;
  int read;
  int write;
  int r;

  UVWASI_DEBUG("uvwasi_path_open(uvwasi=%p, dirfd=%d, dirflags=%d, path='%s', "
               "path_len=%d, o_flags=%d, fs_rights_base=%"PRIu64", "
               "fs_rights_inheriting=%"PRIu64", fs_flags=%d, fd=%p)\n",
               uvwasi,
               dirfd,
               dirflags,
               path,
               path_len,
               o_flags,
               fs_rights_base,
               fs_rights_inheriting,
               fs_flags,
               fd);

  if (uvwasi == NULL || path == NULL || fd == NULL)
    return UVWASI_EINVAL;

  read = 0 != (fs_rights_base & (UVWASI_RIGHT_FD_READ |
                                 UVWASI_RIGHT_FD_READDIR));
  write = 0 != (fs_rights_base & (UVWASI_RIGHT_FD_DATASYNC |
                                  UVWASI_RIGHT_FD_WRITE |
                                  UVWASI_RIGHT_FD_ALLOCATE |
                                  UVWASI_RIGHT_FD_FILESTAT_SET_SIZE));
  flags = write ? read ? UV_FS_O_RDWR : UV_FS_O_WRONLY : UV_FS_O_RDONLY;
  needed_base = UVWASI_RIGHT_PATH_OPEN;
  needed_inheriting = fs_rights_base | fs_rights_inheriting;

  if ((o_flags & UVWASI_O_CREAT) != 0) {
    flags |= UV_FS_O_CREAT;
    needed_base |= UVWASI_RIGHT_PATH_CREATE_FILE;
  }
  if ((o_flags & UVWASI_O_DIRECTORY) != 0)
    flags |= UV_FS_O_DIRECTORY;
  if ((o_flags & UVWASI_O_EXCL) != 0)
    flags |= UV_FS_O_EXCL;
  if ((o_flags & UVWASI_O_TRUNC) != 0) {
    flags |= UV_FS_O_TRUNC;
    needed_base |= UVWASI_RIGHT_PATH_FILESTAT_SET_SIZE;
  }

  if ((fs_flags & UVWASI_FDFLAG_APPEND) != 0)
    flags |= UV_FS_O_APPEND;
  if ((fs_flags & UVWASI_FDFLAG_DSYNC) != 0) {
    flags |= UV_FS_O_DSYNC;
    needed_inheriting |= UVWASI_RIGHT_FD_DATASYNC;
  }
  if ((fs_flags & UVWASI_FDFLAG_NONBLOCK) != 0)
    flags |= UV_FS_O_NONBLOCK;
  if ((fs_flags & UVWASI_FDFLAG_RSYNC) != 0) {
#ifdef O_RSYNC
    flags |= O_RSYNC; /* libuv has no UV_FS_O_RSYNC. */
#else
    flags |= UV_FS_O_SYNC;
#endif
    needed_inheriting |= UVWASI_RIGHT_FD_SYNC;
  }
  if ((fs_flags & UVWASI_FDFLAG_SYNC) != 0) {
    flags |= UV_FS_O_SYNC;
    needed_inheriting |= UVWASI_RIGHT_FD_SYNC;
  }
  if (write && (flags & (UV_FS_O_APPEND | UV_FS_O_TRUNC)) == 0)
    needed_inheriting |= UVWASI_RIGHT_FD_SEEK;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            dirfd,
                            &dirfd_wrap,
                            needed_base,
                            needed_inheriting);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(uvwasi,
                             dirfd_wrap,
                             path,
                             path_len,
                             &resolved_path,
                             dirflags);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&dirfd_wrap->mutex);
    return err;
  }

  r = uv_fs_open(NULL, &req, resolved_path, flags, 0666, NULL);
  uv_mutex_unlock(&dirfd_wrap->mutex);
  uv_fs_req_cleanup(&req);

  if (r < 0) {
    uvwasi__free(uvwasi, resolved_path);
    return uvwasi__translate_uv_error(r);
  }

  /* Not all platforms support UV_FS_O_DIRECTORY, so get the file type and check
     it here. */
  err = uvwasi__get_filetype_by_fd(r, &filetype);
  if (err != UVWASI_ESUCCESS)
    goto close_file_and_error_exit;

  if ((o_flags & UVWASI_O_DIRECTORY) != 0 &&
      filetype != UVWASI_FILETYPE_DIRECTORY) {
    err = UVWASI_ENOTDIR;
    goto close_file_and_error_exit;
  }

  err = uvwasi__get_rights(r, flags, filetype, &max_base, &max_inheriting);
  if (err != UVWASI_ESUCCESS)
    goto close_file_and_error_exit;

  err = uvwasi_fd_table_insert(uvwasi,
                               uvwasi->fds,
                               r,
                               NULL,
                               resolved_path,
                               resolved_path,
                               filetype,
                               fs_rights_base & max_base,
                               fs_rights_inheriting & max_inheriting,
                               0,
                               &wrap);
  if (err != UVWASI_ESUCCESS)
    goto close_file_and_error_exit;

  *fd = wrap->id;
  uv_mutex_unlock(&wrap->mutex);
  uvwasi__free(uvwasi, resolved_path);
  return UVWASI_ESUCCESS;

close_file_and_error_exit:
  uv_fs_close(NULL, &req, r, NULL);
  uv_fs_req_cleanup(&req);
  uvwasi__free(uvwasi, resolved_path);
  return err;
}


uvwasi_errno_t uvwasi_path_readlink(uvwasi_t* uvwasi,
                                    uvwasi_fd_t fd,
                                    const char* path,
                                    uvwasi_size_t path_len,
                                    char* buf,
                                    uvwasi_size_t buf_len,
                                    uvwasi_size_t* bufused) {
  char* resolved_path;
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  size_t len;
  int r;

  UVWASI_DEBUG("uvwasi_path_readlink(uvwasi=%p, fd=%d, path='%s', path_len=%d, "
               "buf=%p, buf_len=%d, bufused=%p)\n",
               uvwasi,
               fd,
               path,
               path_len,
               buf,
               buf_len,
               bufused);

  if (uvwasi == NULL || path == NULL || buf == NULL || bufused == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_READLINK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(uvwasi, wrap, path, path_len, &resolved_path, 0);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_fs_readlink(NULL, &req, resolved_path, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvwasi__free(uvwasi, resolved_path);
  if (r != 0) {
    uv_fs_req_cleanup(&req);
    return uvwasi__translate_uv_error(r);
  }

  len = strnlen(req.ptr, buf_len);
  if (len >= buf_len) {
    uv_fs_req_cleanup(&req);
    return UVWASI_ENOBUFS;
  }

  memcpy(buf, req.ptr, len);
  buf[len] = '\0';
  *bufused = len;
  uv_fs_req_cleanup(&req);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_remove_directory(uvwasi_t* uvwasi,
                                            uvwasi_fd_t fd,
                                            const char* path,
                                            uvwasi_size_t path_len) {
  char* resolved_path;
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_path_remove_directory(uvwasi=%p, fd=%d, path='%s', "
               "path_len=%d)\n",
               uvwasi,
               fd,
               path,
               path_len);

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_REMOVE_DIRECTORY,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(uvwasi, wrap, path, path_len, &resolved_path, 0);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_fs_rmdir(NULL, &req, resolved_path, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvwasi__free(uvwasi, resolved_path);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_rename(uvwasi_t* uvwasi,
                                  uvwasi_fd_t old_fd,
                                  const char* old_path,
                                  uvwasi_size_t old_path_len,
                                  uvwasi_fd_t new_fd,
                                  const char* new_path,
                                  uvwasi_size_t new_path_len) {
  char* resolved_old_path;
  char* resolved_new_path;
  struct uvwasi_fd_wrap_t* old_wrap;
  struct uvwasi_fd_wrap_t* new_wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  UVWASI_DEBUG("uvwasi_path_rename(uvwasi=%p, old_fd=%d, old_path='%s', "
               "old_path_len=%d, new_fd=%d, new_path='%s', new_path_len=%d)\n",
               uvwasi,
               old_fd,
               old_path,
               old_path_len,
               new_fd,
               new_path,
               new_path_len);

  if (uvwasi == NULL || old_path == NULL || new_path == NULL)
    return UVWASI_EINVAL;

  uvwasi_fd_table_lock(uvwasi->fds);

  if (old_fd == new_fd) {
    err = uvwasi_fd_table_get_nolock(uvwasi->fds,
                                     old_fd,
                                     &old_wrap,
                                     UVWASI_RIGHT_PATH_RENAME_SOURCE |
                                     UVWASI_RIGHT_PATH_RENAME_TARGET,
                                     0);
    new_wrap = old_wrap;
  } else {
    err = uvwasi_fd_table_get_nolock(uvwasi->fds,
                                     old_fd,
                                     &old_wrap,
                                     UVWASI_RIGHT_PATH_RENAME_SOURCE,
                                     0);
    if (err != UVWASI_ESUCCESS) {
      uvwasi_fd_table_unlock(uvwasi->fds);
      return err;
    }

    err = uvwasi_fd_table_get_nolock(uvwasi->fds,
                                     new_fd,
                                     &new_wrap,
                                     UVWASI_RIGHT_PATH_RENAME_TARGET,
                                     0);
    if (err != UVWASI_ESUCCESS)
      uv_mutex_unlock(&old_wrap->mutex);
  }

  uvwasi_fd_table_unlock(uvwasi->fds);

  if (err != UVWASI_ESUCCESS)
    return err;

  resolved_old_path = NULL;
  resolved_new_path = NULL;

  err = uvwasi__resolve_path(uvwasi,
                             old_wrap,
                             old_path,
                             old_path_len,
                             &resolved_old_path,
                             0);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  err = uvwasi__resolve_path(uvwasi,
                             new_wrap,
                             new_path,
                             new_path_len,
                             &resolved_new_path,
                             0);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  r = uv_fs_rename(NULL, &req, resolved_old_path, resolved_new_path, NULL);
  uv_fs_req_cleanup(&req);
  if (r != 0) {
    err = uvwasi__translate_uv_error(r);
    goto exit;
  }

  err = UVWASI_ESUCCESS;
exit:
  uv_mutex_unlock(&new_wrap->mutex);
  if (old_fd != new_fd)
    uv_mutex_unlock(&old_wrap->mutex);

  uvwasi__free(uvwasi, resolved_old_path);
  uvwasi__free(uvwasi, resolved_new_path);
  return err;
}


uvwasi_errno_t uvwasi_path_symlink(uvwasi_t* uvwasi,
                                   const char* old_path,
                                   uvwasi_size_t old_path_len,
                                   uvwasi_fd_t fd,
                                   const char* new_path,
                                   uvwasi_size_t new_path_len) {
  char* truncated_old_path;
  char* resolved_new_path;
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  UVWASI_DEBUG("uvwasi_path_symlink(uvwasi=%p, old_path='%s', old_path_len=%d, "
               "fd=%d, new_path='%s', new_path_len=%d)\n",
               uvwasi,
               old_path,
               old_path_len,
               fd,
               new_path,
               new_path_len);

  if (uvwasi == NULL || old_path == NULL || new_path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_SYMLINK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  truncated_old_path = uvwasi__malloc(uvwasi, old_path_len + 1);
  if (truncated_old_path == NULL) {
    uv_mutex_unlock(&wrap->mutex);
    return UVWASI_ENOMEM;
  }

  memcpy(truncated_old_path, old_path, old_path_len);
  truncated_old_path[old_path_len] = '\0';

  err = uvwasi__resolve_path(uvwasi,
                             wrap,
                             new_path,
                             new_path_len,
                             &resolved_new_path,
                             0);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    uvwasi__free(uvwasi, truncated_old_path);
    return err;
  }

  /* Windows support may require setting the flags option. */
  r = uv_fs_symlink(NULL, &req, truncated_old_path, resolved_new_path, 0, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvwasi__free(uvwasi, truncated_old_path);
  uvwasi__free(uvwasi, resolved_new_path);
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_unlink_file(uvwasi_t* uvwasi,
                                       uvwasi_fd_t fd,
                                       const char* path,
                                       uvwasi_size_t path_len) {
  char* resolved_path;
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  UVWASI_DEBUG("uvwasi_path_unlink_file(uvwasi=%p, fd=%d, path='%s', "
               "path_len=%d)\n",
               uvwasi,
               fd,
               path,
               path_len);

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_UNLINK_FILE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(uvwasi, wrap, path, path_len, &resolved_path, 0);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_fs_unlink(NULL, &req, resolved_path, NULL);
  uv_mutex_unlock(&wrap->mutex);
  uvwasi__free(uvwasi, resolved_path);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_poll_oneoff(uvwasi_t* uvwasi,
                                  const uvwasi_subscription_t* in,
                                  uvwasi_event_t* out,
                                  uvwasi_size_t nsubscriptions,
                                  uvwasi_size_t* nevents) {
  struct uvwasi_poll_oneoff_state_t state;
  struct uvwasi__poll_fdevent_t* fdevent;
  uvwasi_userdata_t timer_userdata;
  uvwasi_timestamp_t min_timeout;
  uvwasi_timestamp_t cur_timeout;
  uvwasi_timestamp_t now;
  uvwasi_subscription_t sub;
  uvwasi_event_t* event;
  uvwasi_errno_t err;
  int has_timeout;
  uvwasi_size_t i;

  UVWASI_DEBUG("uvwasi_poll_oneoff(uvwasi=%p, in=%p, out=%p, "
               "nsubscriptions=%d, nevents=%p)\n",
               uvwasi,
               in,
               out,
               nsubscriptions,
               nevents);

  if (uvwasi == NULL || in == NULL || out == NULL ||
      nsubscriptions == 0 || nevents == NULL) {
    return UVWASI_EINVAL;
  }

  *nevents = 0;
  err = uvwasi__poll_oneoff_state_init(uvwasi, &state, nsubscriptions);
  if (err != UVWASI_ESUCCESS)
    return err;

  timer_userdata = 0;
  has_timeout = 0;
  min_timeout = 0;

  for (i = 0; i < nsubscriptions; i++) {
    sub = in[i];

    switch (sub.type) {
      case UVWASI_EVENTTYPE_CLOCK:
        if (sub.u.clock.flags == UVWASI_SUBSCRIPTION_CLOCK_ABSTIME) {
          /* Convert absolute time to relative delay. */
          err = uvwasi__clock_gettime_realtime(&now);
          if (err != UVWASI_ESUCCESS)
            goto exit;

          cur_timeout = sub.u.clock.timeout - now;
        } else {
          cur_timeout = sub.u.clock.timeout;
        }

        if (has_timeout == 0 || cur_timeout < min_timeout) {
          min_timeout = cur_timeout;
          timer_userdata = sub.userdata;
          has_timeout = 1;
        }

        break;
      case UVWASI_EVENTTYPE_FD_READ:
      case UVWASI_EVENTTYPE_FD_WRITE:
        err = uvwasi__poll_oneoff_state_add_fdevent(&state, &sub);
        if (err != UVWASI_ESUCCESS)
          goto exit;

        break;
      default:
        err = UVWASI_EINVAL;
        goto exit;
    }
  }

  if (has_timeout == 1) {
    err = uvwasi__poll_oneoff_state_set_timer(&state, min_timeout);
    if (err != UVWASI_ESUCCESS)
      goto exit;
  }

  /* Handle poll() errors, then timeouts, then happy path. */
  err = uvwasi__poll_oneoff_run(&state);
  if (err != UVWASI_ESUCCESS) {
    goto exit;
  } else if (state.result == 0) {
    event = &out[0];
    event->userdata = timer_userdata;
    event->error = UVWASI_ESUCCESS;
    event->type = UVWASI_EVENTTYPE_CLOCK;
    *nevents = 1;
  } else {
    for (i = 0; i < state.fdevent_cnt; i++) {
      fdevent = &state.fdevents[i];
      event = &out[*nevents];

      event->userdata = fdevent->userdata;
      event->error = fdevent->error;
      event->type = fdevent->type;
      event->u.fd_readwrite.nbytes = 0;
      event->u.fd_readwrite.flags = 0;

      if (fdevent->error != UVWASI_ESUCCESS)
        ;
      else if ((fdevent->revents & UV_DISCONNECT) != 0)
        event->u.fd_readwrite.flags = UVWASI_EVENT_FD_READWRITE_HANGUP;
      else if ((fdevent->revents & (UV_READABLE | UV_WRITABLE)) != 0)
        ; /* TODO(cjihrig): Set nbytes if type is UVWASI_EVENTTYPE_FD_READ. */
      else
        continue;

      *nevents = *nevents + 1;
    }
  }

  err = UVWASI_ESUCCESS;

exit:
  uvwasi__poll_oneoff_state_cleanup(&state);
  return err;
}


uvwasi_errno_t uvwasi_proc_exit(uvwasi_t* uvwasi, uvwasi_exitcode_t rval) {
  UVWASI_DEBUG("uvwasi_proc_exit(uvwasi=%p, rval=%d)\n", uvwasi, rval);
  exit(rval);
  return UVWASI_ESUCCESS; /* This doesn't happen. */
}


uvwasi_errno_t uvwasi_proc_raise(uvwasi_t* uvwasi, uvwasi_signal_t sig) {
  int r;

  UVWASI_DEBUG("uvwasi_proc_raise(uvwasi=%p, sig=%d)\n", uvwasi, sig);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  r = uvwasi__translate_to_uv_signal(sig);
  if (r == -1)
    return UVWASI_ENOSYS;

  r = uv_kill(uv_os_getpid(), r);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_random_get(uvwasi_t* uvwasi,
                                 void* buf,
                                 uvwasi_size_t buf_len) {
  int r;

  UVWASI_DEBUG("uvwasi_random_get(uvwasi=%p, buf=%p, buf_len=%d)\n",
               uvwasi,
               buf,
               buf_len);

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  r = uv_random(NULL, NULL, buf, buf_len, 0, NULL);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_sched_yield(uvwasi_t* uvwasi) {
  UVWASI_DEBUG("uvwasi_sched_yield(uvwasi=%p)\n", uvwasi);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

#ifndef _WIN32
  if (0 != sched_yield())
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));
#else
  SwitchToThread();
#endif /* _WIN32 */

  return UVWASI_ESUCCESS;
}

uvwasi_errno_t uvwasi_sock_recv(uvwasi_t* uvwasi,
                                uvwasi_fd_t sock,
                                const uvwasi_iovec_t* ri_data,
                                uvwasi_size_t ri_data_len,
                                uvwasi_riflags_t ri_flags,
                                uvwasi_size_t* ro_datalen,
                                uvwasi_roflags_t* ro_flags) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err = 0;
  recv_data_t recv_data;

  UVWASI_DEBUG("uvwasi_sock_recv(uvwasi=%p, sock=%d, ri_data=%p, "
	       "ri_data_len=%d, ri_flags=%d, ro_datalen=%p, ro_flags=%p)\n",
               uvwasi,
               sock,
               ri_data,
               ri_data_len,
               ri_flags,
               ro_datalen,
               ro_flags);

  if (uvwasi == NULL || ri_data == NULL || ro_datalen == NULL || ro_flags == NULL)
    return UVWASI_EINVAL;

  if (ri_flags != 0)
    return UVWASI_ENOTSUP;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            sock,
                            &wrap,
                            UVWASI__RIGHTS_SOCKET_BASE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  recv_data.base = ri_data->buf;
  recv_data.len = ri_data->buf_len;
  err = read_stream_sync(uvwasi, (uv_stream_t*) wrap->sock, &recv_data);
  uv_mutex_unlock(&wrap->mutex);
  if (err != 0) {
    return err;
  }

  if (recv_data.nread == 0) {
    return UVWASI_EAGAIN;
  } else if (recv_data.nread < 0) {
    return uvwasi__translate_uv_error(recv_data.nread);
  }

  *ro_datalen = recv_data.nread;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_sock_send(uvwasi_t* uvwasi,
                                uvwasi_fd_t sock,
                                const uvwasi_ciovec_t* si_data,
                                uvwasi_size_t si_data_len,
                                uvwasi_siflags_t si_flags,
                                uvwasi_size_t* so_datalen) {

  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err = 0;
  uv_buf_t* bufs;
  int r = 0;

  UVWASI_DEBUG("uvwasi_sock_send(uvwasi=%p, sock=%d, si_data=%p, "
	       "si_data_len=%d, si_flags=%d, so_datalen=%p)\n",
               uvwasi,
               sock,
               si_data,
               si_data_len,
               si_flags,
               so_datalen);

  if (uvwasi == NULL || si_data == NULL || so_datalen == NULL ||
      si_flags != 0)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            sock,
                            &wrap,
                            UVWASI__RIGHTS_SOCKET_BASE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__setup_ciovs(uvwasi, &bufs, si_data, si_data_len);
  if (err != UVWASI_ESUCCESS) {
    uv_mutex_unlock(&wrap->mutex);
    return err;
  }

  r = uv_try_write((uv_stream_t*) wrap->sock, bufs, si_data_len);
  uvwasi__free(uvwasi, bufs);
  uv_mutex_unlock(&wrap->mutex);
  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *so_datalen = (uvwasi_size_t) r;
  return UVWASI_ESUCCESS;
}

uvwasi_errno_t uvwasi_sock_shutdown(uvwasi_t* uvwasi,
                                    uvwasi_fd_t sock,
                                    uvwasi_sdflags_t how) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err = 0;
  shutdown_data_t shutdown_data = {0};

  if (how & ~UVWASI_SHUT_WR)
    return UVWASI_ENOTSUP;

  UVWASI_DEBUG("uvwasi_sock_shutdown(uvwasi=%p, sock=%d, how=%d)\n",
               uvwasi,
               sock,
               how);

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            sock,
                            &wrap,
                            UVWASI__RIGHTS_SOCKET_BASE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  if (how & UVWASI_SHUT_WR) {
    err = shutdown_stream_sync(uvwasi, (uv_stream_t*) wrap->sock, &shutdown_data);
    if (err != UVWASI_ESUCCESS) {
      uv_mutex_unlock(&wrap->mutex);
      return err;
    }
  }

  uv_mutex_unlock(&wrap->mutex);

  if (shutdown_data.status != 0) 
    return uvwasi__translate_uv_error(shutdown_data.status);

  return UVWASI_ESUCCESS;
}

uvwasi_errno_t uvwasi_sock_accept(uvwasi_t* uvwasi,
                                  uvwasi_fd_t sock,
                                  uvwasi_fdflags_t flags,
                                  uvwasi_fd_t* connect_sock) {
  struct uvwasi_fd_wrap_t* wrap;
  struct uvwasi_fd_wrap_t* connected_wrap;
  uvwasi_errno_t err = 0;
  uv_loop_t* sock_loop = NULL;
  int r = 0;

  UVWASI_DEBUG("uvwasi_sock_accept(uvwasi=%p, sock=%d, flags=%d, "
               "connect_sock=%p)\n",
               uvwasi,
               sock,
               flags,
               connect_sock);

  if (uvwasi == NULL || connect_sock == NULL)
    return UVWASI_EINVAL;

  if (flags & ~UVWASI_FDFLAG_NONBLOCK)
    return UVWASI_ENOTSUP;

  err = uvwasi_fd_table_get(uvwasi->fds,
                            sock,
                            &wrap,
                            UVWASI__RIGHTS_SOCKET_BASE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  sock_loop = uv_handle_get_loop((uv_handle_t*) wrap->sock);
  uv_tcp_t* uv_connect_sock = (uv_tcp_t*) uvwasi__malloc(uvwasi, sizeof(uv_tcp_t));
  uv_tcp_init(sock_loop, uv_connect_sock);

  r = uv_accept((uv_stream_t*) wrap->sock, (uv_stream_t*) uv_connect_sock);
  if (r != 0) {
    if (r == UV_EAGAIN) {
      // if not blocking then just return as we have to wait for a connection
      if (flags & UVWASI_FDFLAG_NONBLOCK) {
        err = free_handle_sync(uvwasi, (uv_handle_t*) uv_connect_sock);
        uv_mutex_unlock(&wrap->mutex);
        if (err != UVWASI_ESUCCESS) {
          return err;
	}
        return UVWASI_EAGAIN;
      }
    } else {
      err = uvwasi__translate_uv_error(r);
      goto close_sock_and_error_exit;
    }

    // request was blocking and we have no connection yet. run
    // the loop until a connection comes in
    while (1) {
      err = 0;
      if (uv_run(sock_loop, UV_RUN_ONCE) == 0) {
        err = UVWASI_ECONNABORTED;
        goto close_sock_and_error_exit;
      }

      r = uv_accept((uv_stream_t*) wrap->sock, (uv_stream_t*) uv_connect_sock);
      if (r == UV_EAGAIN) {
	// still no connection or error so run the loop again
        continue;
      }

      if (r != 0) {
	// An error occurred accepting the connection. Break out of the loop and
	// report an error.
        err = uvwasi__translate_uv_error(r);
        goto close_sock_and_error_exit;
      }

      // if we get here a new connection was successfully accepted
      break;
    }
  }

  err = uvwasi_fd_table_insert(uvwasi,
                               uvwasi->fds,
                               -1,
                               uv_connect_sock,
                               NULL,
                               NULL,
                               UVWASI_FILETYPE_SOCKET_STREAM,
                               UVWASI__RIGHTS_SOCKET_BASE,
                               UVWASI__RIGHTS_SOCKET_INHERITING,
                               1,
                               &connected_wrap);

  if (err != UVWASI_ESUCCESS)
    goto close_sock_and_error_exit;

  *connect_sock = connected_wrap->id;
  uv_mutex_unlock(&wrap->mutex);
  uv_mutex_unlock(&connected_wrap->mutex);
  return UVWASI_ESUCCESS;

close_sock_and_error_exit:
  uvwasi__free(uvwasi, uv_connect_sock);
  uv_mutex_unlock(&wrap->mutex);
  return err;
}


const char* uvwasi_embedder_err_code_to_string(uvwasi_errno_t code) {
  switch (code) {
#define V(errcode) case errcode: return #errcode;
    V(UVWASI_E2BIG)
    V(UVWASI_EACCES)
    V(UVWASI_EADDRINUSE)
    V(UVWASI_EADDRNOTAVAIL)
    V(UVWASI_EAFNOSUPPORT)
    V(UVWASI_EAGAIN)
    V(UVWASI_EALREADY)
    V(UVWASI_EBADF)
    V(UVWASI_EBADMSG)
    V(UVWASI_EBUSY)
    V(UVWASI_ECANCELED)
    V(UVWASI_ECHILD)
    V(UVWASI_ECONNABORTED)
    V(UVWASI_ECONNREFUSED)
    V(UVWASI_ECONNRESET)
    V(UVWASI_EDEADLK)
    V(UVWASI_EDESTADDRREQ)
    V(UVWASI_EDOM)
    V(UVWASI_EDQUOT)
    V(UVWASI_EEXIST)
    V(UVWASI_EFAULT)
    V(UVWASI_EFBIG)
    V(UVWASI_EHOSTUNREACH)
    V(UVWASI_EIDRM)
    V(UVWASI_EILSEQ)
    V(UVWASI_EINPROGRESS)
    V(UVWASI_EINTR)
    V(UVWASI_EINVAL)
    V(UVWASI_EIO)
    V(UVWASI_EISCONN)
    V(UVWASI_EISDIR)
    V(UVWASI_ELOOP)
    V(UVWASI_EMFILE)
    V(UVWASI_EMLINK)
    V(UVWASI_EMSGSIZE)
    V(UVWASI_EMULTIHOP)
    V(UVWASI_ENAMETOOLONG)
    V(UVWASI_ENETDOWN)
    V(UVWASI_ENETRESET)
    V(UVWASI_ENETUNREACH)
    V(UVWASI_ENFILE)
    V(UVWASI_ENOBUFS)
    V(UVWASI_ENODEV)
    V(UVWASI_ENOENT)
    V(UVWASI_ENOEXEC)
    V(UVWASI_ENOLCK)
    V(UVWASI_ENOLINK)
    V(UVWASI_ENOMEM)
    V(UVWASI_ENOMSG)
    V(UVWASI_ENOPROTOOPT)
    V(UVWASI_ENOSPC)
    V(UVWASI_ENOSYS)
    V(UVWASI_ENOTCONN)
    V(UVWASI_ENOTDIR)
    V(UVWASI_ENOTEMPTY)
    V(UVWASI_ENOTRECOVERABLE)
    V(UVWASI_ENOTSOCK)
    V(UVWASI_ENOTSUP)
    V(UVWASI_ENOTTY)
    V(UVWASI_ENXIO)
    V(UVWASI_EOVERFLOW)
    V(UVWASI_EOWNERDEAD)
    V(UVWASI_EPERM)
    V(UVWASI_EPIPE)
    V(UVWASI_EPROTO)
    V(UVWASI_EPROTONOSUPPORT)
    V(UVWASI_EPROTOTYPE)
    V(UVWASI_ERANGE)
    V(UVWASI_EROFS)
    V(UVWASI_ESPIPE)
    V(UVWASI_ESRCH)
    V(UVWASI_ESTALE)
    V(UVWASI_ETIMEDOUT)
    V(UVWASI_ETXTBSY)
    V(UVWASI_EXDEV)
    V(UVWASI_ENOTCAPABLE)
    V(UVWASI_ESUCCESS)
#undef V
    default:
      return "UVWASI_UNKNOWN_ERROR";
  }
}
