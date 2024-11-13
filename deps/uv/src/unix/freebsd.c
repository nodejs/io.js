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

#include "uv.h"
#include "internal.h"

#include <assert.h>
#include <string.h>
#include <errno.h>

#include <paths.h>
#if defined(__DragonFly__)
# include <sys/event.h>
# include <sys/kinfo.h>
#else
# include <sys/user.h>
#endif
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <vm/vm_param.h> /* VM_LOADAVG */
#include <time.h>
#include <stdlib.h>
#include <unistd.h> /* sysconf */
#include <fcntl.h>

#ifndef CPUSTATES
# define CPUSTATES 5U
#endif
#ifndef CP_USER
# define CP_USER 0
# define CP_NICE 1
# define CP_SYS 2
# define CP_IDLE 3
# define CP_INTR 4
#endif


int uv__platform_loop_init(uv_loop_t* loop) {
  return uv__kqueue_init(loop);
}


void uv__platform_loop_delete(uv_loop_t* loop) {
}

int uv_exepath(char* buffer, size_t* size) {
  char abspath[PATH_MAX * 2 + 1];
  int mib[4];
  size_t abspath_size;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PATHNAME;
  mib[3] = -1;

  abspath_size = sizeof abspath;
  if (sysctl(mib, ARRAY_SIZE(mib), abspath, &abspath_size, NULL, 0))
    return UV__ERR(errno);

  assert(abspath_size > 0);
  abspath_size -= 1;
  *size -= 1;

  if (*size > abspath_size)
    *size = abspath_size;

  memcpy(buffer, abspath, *size);
  buffer[*size] = '\0';

  return 0;
}

uint64_t uv_get_free_memory(void) {
  int freecount;
  size_t size = sizeof(freecount);

  if (sysctlbyname("vm.stats.vm.v_free_count", &freecount, &size, NULL, 0))
    return 0;

  return (uint64_t) freecount * sysconf(_SC_PAGESIZE);

}


uint64_t uv_get_total_memory(void) {
  unsigned long info;
  int which[] = {CTL_HW, HW_PHYSMEM};

  size_t size = sizeof(info);

  if (sysctl(which, ARRAY_SIZE(which), &info, &size, NULL, 0))
    return 0;

  return (uint64_t) info;
}


uint64_t uv_get_constrained_memory(void) {
  return 0;  /* Memory constraints are unknown. */
}


uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}


void uv_loadavg(double avg[3]) {
  struct loadavg info;
  size_t size = sizeof(info);
  int which[] = {CTL_VM, VM_LOADAVG};

  if (sysctl(which, ARRAY_SIZE(which), &info, &size, NULL, 0) < 0) return;

  avg[0] = (double) info.ldavg[0] / info.fscale;
  avg[1] = (double) info.ldavg[1] / info.fscale;
  avg[2] = (double) info.ldavg[2] / info.fscale;
}


int uv_resident_set_memory(size_t* rss) {
  struct kinfo_proc kinfo;
  size_t page_size;
  size_t kinfo_size;
  int mib[4];

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID;
  mib[3] = getpid();

  kinfo_size = sizeof(kinfo);

  if (sysctl(mib, ARRAY_SIZE(mib), &kinfo, &kinfo_size, NULL, 0))
    return UV__ERR(errno);

  page_size = getpagesize();

#ifdef __DragonFly__
  *rss = kinfo.kp_vm_rssize * page_size;
#else
  *rss = kinfo.ki_rssize * page_size;
#endif

  return 0;
}


int uv_uptime(double* uptime) {
  int r;
  struct timespec sp;
  r = clock_gettime(CLOCK_MONOTONIC, &sp);
  if (r)
    return UV__ERR(errno);

  *uptime = sp.tv_sec;
  return 0;
}


int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  unsigned int ticks = (unsigned int)sysconf(_SC_CLK_TCK),
               multiplier = ((uint64_t)1000L / ticks), cpuspeed, maxcpus,
               cur = 0;
  uv_cpu_info_t* cpu_info;
  const char* maxcpus_key;
  const char* cptimes_key;
  const char* model_key;
  char model[512];
  long* cp_times;
  int numcpus;
  size_t size;
  int i;

#if defined(__DragonFly__)
  /* This is not quite correct but DragonFlyBSD doesn't seem to have anything
   * comparable to kern.smp.maxcpus or kern.cp_times (kern.cp_time is a total,
   * not per CPU). At least this stops uv_cpu_info() from failing completely.
   */
  maxcpus_key = "hw.ncpu";
  cptimes_key = "kern.cp_time";
#else
  maxcpus_key = "kern.smp.maxcpus";
  cptimes_key = "kern.cp_times";
#endif

#if defined(__arm__) || defined(__aarch64__)
  /* The key hw.model and hw.clockrate are not available on FreeBSD ARM. */
  model_key = "hw.machine";
  cpuspeed = 0;
#else
  model_key = "hw.model";

  size = sizeof(cpuspeed);
  if (sysctlbyname("hw.clockrate", &cpuspeed, &size, NULL, 0))
    return -errno;
#endif

  size = sizeof(model);
  if (sysctlbyname(model_key, &model, &size, NULL, 0))
    return UV__ERR(errno);

  size = sizeof(numcpus);
  if (sysctlbyname("hw.ncpu", &numcpus, &size, NULL, 0))
    return UV__ERR(errno);

  *cpu_infos = uv__malloc(numcpus * sizeof(**cpu_infos));
  if (!(*cpu_infos))
    return UV_ENOMEM;

  *count = numcpus;

  /* kern.cp_times on FreeBSD i386 gives an array up to maxcpus instead of
   * ncpu.
   */
  size = sizeof(maxcpus);
  if (sysctlbyname(maxcpus_key, &maxcpus, &size, NULL, 0)) {
    uv__free(*cpu_infos);
    return UV__ERR(errno);
  }

  size = maxcpus * CPUSTATES * sizeof(long);

  cp_times = uv__malloc(size);
  if (cp_times == NULL) {
    uv__free(*cpu_infos);
    return UV_ENOMEM;
  }

  if (sysctlbyname(cptimes_key, cp_times, &size, NULL, 0)) {
    uv__free(cp_times);
    uv__free(*cpu_infos);
    return UV__ERR(errno);
  }

  for (i = 0; i < numcpus; i++) {
    cpu_info = &(*cpu_infos)[i];

    cpu_info->cpu_times.user = (uint64_t)(cp_times[CP_USER+cur]) * multiplier;
    cpu_info->cpu_times.nice = (uint64_t)(cp_times[CP_NICE+cur]) * multiplier;
    cpu_info->cpu_times.sys = (uint64_t)(cp_times[CP_SYS+cur]) * multiplier;
    cpu_info->cpu_times.idle = (uint64_t)(cp_times[CP_IDLE+cur]) * multiplier;
    cpu_info->cpu_times.irq = (uint64_t)(cp_times[CP_INTR+cur]) * multiplier;

    cpu_info->model = uv__strdup(model);
    cpu_info->speed = cpuspeed;

    cur+=CPUSTATES;
  }

  uv__free(cp_times);
  return 0;
}


ssize_t
uv__fs_copy_file_range(int fd_in,
                       off_t* off_in,
                       int fd_out,
                       off_t* off_out,
                       size_t len,
                       unsigned int flags)
{
#if __FreeBSD__ >= 13 && !defined(__DragonFly__)
	return copy_file_range(fd_in, off_in, fd_out, off_out, len, flags);
#else
	return errno = ENOSYS, -1;
#endif
}
