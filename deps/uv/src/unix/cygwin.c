/* Copyright libuv project contributors. All rights reserved.
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
#include "internal.h"

#include <sys/sysinfo.h>
#include <unistd.h>

int uv_uptime(double* uptime) {
  struct sysinfo info;

  if (sysinfo(&info) < 0)
    return UV__ERR(errno);

  *uptime = info.uptime;
  return 0;
}

int uv_resident_set_memory(size_t* rss) {
  char buf[1024];
  const char* s;
  long val;
  int rc;
  int i;
  struct sysinfo si;

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

  do
    rc = sysinfo(&si);
  while (rc == -1 && errno == EINTR);
  if (rc == -1)
    return UV__ERR(errno);

  *rss = val * si.mem_unit;
  return 0;

err:
  return UV_EINVAL;
}

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  /* FIXME: read /proc/stat? */
  *cpu_infos = NULL;
  *count = 0;
  return UV_ENOSYS;
}

uint64_t uv_get_constrained_memory(void) {
  return 0;  /* Memory constraints are unknown. */
}

uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}
