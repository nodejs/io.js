// Copyright 2024 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/contrib/thread_pool/topology.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>  // strchr

#include <map>
#include <vector>

#include "hwy/detect_compiler_arch.h"  // HWY_OS_WIN

#if HWY_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif  // HWY_OS_WIN

#if HWY_OS_LINUX || HWY_OS_FREEBSD
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>  // sysconf
#endif  // HWY_OS_LINUX || HWY_OS_FREEBSD

#if HWY_OS_FREEBSD
// must come after sys/types.h.
#include <sys/cpuset.h>  // CPU_SET
#endif                   // HWY_OS_FREEBSD

#if HWY_ARCH_WASM
#include <emscripten/threading.h>
#endif

#include "hwy/base.h"

namespace hwy {

HWY_CONTRIB_DLLEXPORT bool HaveThreadingSupport() {
#if HWY_ARCH_WASM
  return emscripten_has_threading_support() != 0;
#else
  return true;
#endif
}

HWY_CONTRIB_DLLEXPORT size_t TotalLogicalProcessors() {
  size_t lp = 0;
#if HWY_ARCH_WASM
  const int num_cores = emscripten_num_logical_cores();
  if (num_cores > 0) lp = static_cast<size_t>(num_cores);
#elif HWY_OS_WIN
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);  // always succeeds
  // WARNING: this is only for the current group, hence limited to 64.
  lp = static_cast<size_t>(sysinfo.dwNumberOfProcessors);
#elif HWY_OS_LINUX
  // Use configured, not "online" (_SC_NPROCESSORS_ONLN), because we want an
  // upper bound.
  const long ret = sysconf(_SC_NPROCESSORS_CONF);  // NOLINT(runtime/int)
  if (ret < 0) {
    fprintf(stderr, "Unexpected value of _SC_NPROCESSORS_CONF: %d\n",
            static_cast<int>(ret));
  } else {
    lp = static_cast<size_t>(ret);
  }
#endif

  if (HWY_UNLIKELY(lp == 0)) {  // Failed to detect.
    HWY_IF_CONSTEXPR(HWY_IS_DEBUG_BUILD) {
      fprintf(stderr,
              "Unknown TotalLogicalProcessors, assuming 1. "
              "HWY_OS_: WIN=%d LINUX=%d APPLE=%d;\n"
              "HWY_ARCH_: WASM=%d X86=%d PPC=%d ARM=%d RISCV=%d S390X=%d\n",
              HWY_OS_WIN, HWY_OS_LINUX, HWY_OS_APPLE, HWY_ARCH_WASM,
              HWY_ARCH_X86, HWY_ARCH_PPC, HWY_ARCH_ARM, HWY_ARCH_RISCV,
              HWY_ARCH_S390X);
    }
    return 1;
  }

  // Warn that we are clamping.
  if (HWY_UNLIKELY(lp > kMaxLogicalProcessors)) {
    HWY_IF_CONSTEXPR(HWY_IS_DEBUG_BUILD) {
      fprintf(stderr, "OS reports %zu processors but clamping to %zu\n", lp,
              kMaxLogicalProcessors);
    }
    lp = kMaxLogicalProcessors;
  }

  return lp;
}

#ifdef __ANDROID__
#include <sys/syscall.h>
#endif

HWY_CONTRIB_DLLEXPORT bool GetThreadAffinity(LogicalProcessorSet& lps) {
#if HWY_OS_WIN
  // Only support the first 64 because WINE does not support processor groups.
  const HANDLE hThread = GetCurrentThread();
  const DWORD_PTR prev = SetThreadAffinityMask(hThread, ~DWORD_PTR(0));
  if (!prev) return false;
  (void)SetThreadAffinityMask(hThread, prev);
  lps = LogicalProcessorSet();  // clear all
  lps.SetNonzeroBitsFrom64(prev);
  return true;
#elif HWY_OS_LINUX
  cpu_set_t set;
  CPU_ZERO(&set);
  const pid_t pid = 0;  // current thread
#ifdef __ANDROID__
  const int err = syscall(__NR_sched_getaffinity, pid, sizeof(cpu_set_t), &set);
#else
  const int err = sched_getaffinity(pid, sizeof(cpu_set_t), &set);
#endif  // __ANDROID__
  if (err != 0) return false;
  for (size_t lp = 0; lp < kMaxLogicalProcessors; ++lp) {
#if HWY_COMPILER_GCC_ACTUAL
    // Workaround for GCC compiler warning with CPU_ISSET macro
    HWY_DIAGNOSTICS(push)
    HWY_DIAGNOSTICS_OFF(disable : 4305 4309, ignored "-Wsign-conversion")
#endif
    if (CPU_ISSET(static_cast<int>(lp), &set)) {
      lps.Set(lp);
    }
#if HWY_COMPILER_GCC_ACTUAL
    HWY_DIAGNOSTICS(pop)
#endif
  }
  return true;
#elif HWY_OS_FREEBSD
  cpuset_t set;
  CPU_ZERO(&set);
  const pid_t pid = getpid();  // current thread
  const int err = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, pid,
                                     sizeof(cpuset_t), &set);
  if (err != 0) return false;
  for (size_t lp = 0; lp < kMaxLogicalProcessors; ++lp) {
#if HWY_COMPILER_GCC_ACTUAL
    // Workaround for GCC compiler warning with CPU_ISSET macro
    HWY_DIAGNOSTICS(push)
    HWY_DIAGNOSTICS_OFF(disable : 4305 4309, ignored "-Wsign-conversion")
#endif
    if (CPU_ISSET(static_cast<int>(lp), &set)) {
      lps.Set(lp);
    }
#if HWY_COMPILER_GCC_ACTUAL
    HWY_DIAGNOSTICS(pop)
#endif
  }
  return true;
#else
  // Do not even set lp=0 to force callers to handle this case.
  (void)lps;
  return false;
#endif
}

HWY_CONTRIB_DLLEXPORT bool SetThreadAffinity(const LogicalProcessorSet& lps) {
#if HWY_OS_WIN
  const HANDLE hThread = GetCurrentThread();
  const DWORD_PTR prev = SetThreadAffinityMask(hThread, lps.Get64());
  return prev != 0;
#elif HWY_OS_LINUX
  cpu_set_t set;
  CPU_ZERO(&set);
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for GCC compiler warning with CPU_SET macro
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4305 4309, ignored "-Wsign-conversion")
#endif
  lps.Foreach([&set](size_t lp) { CPU_SET(static_cast<int>(lp), &set); });
#if HWY_COMPILER_GCC_ACTUAL
  HWY_DIAGNOSTICS(pop)
#endif
  const pid_t pid = 0;  // current thread
#ifdef __ANDROID__
  const int err = syscall(__NR_sched_setaffinity, pid, sizeof(cpu_set_t), &set);
#else
  const int err = sched_setaffinity(pid, sizeof(cpu_set_t), &set);
#endif  // __ANDROID__
  if (err != 0) return false;
  return true;
#elif HWY_OS_FREEBSD
  cpuset_t set;
  CPU_ZERO(&set);
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for GCC compiler warning with CPU_SET macro
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4305 4309, ignored "-Wsign-conversion")
#endif
  lps.Foreach([&set](size_t lp) { CPU_SET(static_cast<int>(lp), &set); });
#if HWY_COMPILER_GCC_ACTUAL
  HWY_DIAGNOSTICS(pop)
#endif
  const pid_t pid = getpid();  // current thread
  const int err = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, pid,
                                     sizeof(cpuset_t), &set);
  if (err != 0) return false;
  return true;
#else
  // Apple THREAD_AFFINITY_POLICY is only an (often ignored) hint.
  (void)lps;
  return false;
#endif
}

#if HWY_OS_LINUX
namespace {

class File {
 public:
  explicit File(const char* path) {
    for (;;) {
      fd_ = open(path, O_RDONLY);
      if (fd_ > 0) return;           // success
      if (errno == EINTR) continue;  // signal: retry
      if (errno == ENOENT) return;   // not found, give up
      if (HWY_IS_DEBUG_BUILD) {
        fprintf(stderr, "Unexpected error opening %s: %d\n", path, errno);
      }
      return;  // unknown error, give up
    }
  }

  ~File() {
    if (fd_ > 0) {
      for (;;) {
        const int ret = close(fd_);
        if (ret == 0) break;           // success
        if (errno == EINTR) continue;  // signal: retry
        if (HWY_IS_DEBUG_BUILD) {
          fprintf(stderr, "Unexpected error closing file: %d\n", errno);
        }
        return;  // unknown error, ignore
      }
    }
  }

  // Returns number of bytes read or 0 on failure.
  size_t Read(char* buf200) const {
    if (fd_ < 0) return 0;
    size_t pos = 0;
    for (;;) {
      // read instead of `pread`, which might not work for sysfs.
      const auto bytes_read = read(fd_, buf200 + pos, 200 - pos);
      if (bytes_read == 0) {  // EOF: done
        buf200[pos++] = '\0';
        return pos;
      }
      if (bytes_read == -1) {
        if (errno == EINTR) continue;  // signal: retry
        if (HWY_IS_DEBUG_BUILD) {
          fprintf(stderr, "Unexpected error reading file: %d\n", errno);
        }
        return 0;
      }
      pos += static_cast<size_t>(bytes_read);
      HWY_ASSERT(pos <= 200);
    }
  }

 private:
  int fd_;
};

// Returns bytes read, or 0 on failure.
size_t ReadSysfs(const char* format, size_t lp, char* buf200) {
  char path[200];
  const int bytes_written = snprintf(path, sizeof(path), format, lp);
  HWY_ASSERT(0 < bytes_written &&
             bytes_written < static_cast<int>(sizeof(path) - 1));

  const File file(path);
  return file.Read(buf200);
}

// Interprets [str + pos, str + end) as base-10 ASCII. Stops when any non-digit
// is found, or at end. Returns false if no digits found.
bool ParseDigits(const char* str, const size_t end, size_t& pos, size_t* out) {
  HWY_ASSERT(pos <= end);
  // 9 digits cannot overflow even 32-bit size_t.
  const size_t stop = pos + 9;
  *out = 0;
  for (; pos < HWY_MIN(end, stop); ++pos) {
    const int c = str[pos];
    if (c < '0' || c > '9') break;
    *out *= 10;
    *out += static_cast<size_t>(c - '0');
  }
  if (pos == 0) {  // No digits found
    *out = 0;
    return false;
  }
  return true;
}

// Number, plus optional K or M suffix, plus terminator.
bool ParseNumberWithOptionalSuffix(const char* str, size_t len, size_t* out) {
  size_t pos = 0;
  if (!ParseDigits(str, len, pos, out)) return false;
  if (str[pos] == 'K') {
    *out <<= 10;
    ++pos;
  }
  if (str[pos] == 'M') {
    *out <<= 20;
    ++pos;
  }
  if (str[pos] != '\0' && str[pos] != '\n') {
    HWY_ABORT("Expected [suffix] terminator at %zu %s\n", pos, str);
  }
  return true;
}

bool ReadNumberWithOptionalSuffix(const char* format, size_t lp, size_t* out) {
  char buf200[200];
  const size_t pos = ReadSysfs(format, lp, buf200);
  if (pos == 0) return false;
  return ParseNumberWithOptionalSuffix(buf200, pos, out);
}

const char* kPackage =
    "/sys/devices/system/cpu/cpu%zu/topology/physical_package_id";
const char* kCluster = "/sys/devices/system/cpu/cpu%zu/cache/index3/id";
const char* kCore = "/sys/devices/system/cpu/cpu%zu/topology/core_id";
const char* kL2Size = "/sys/devices/system/cpu/cpu%zu/cache/index2/size";
const char* kL3Size = "/sys/devices/system/cpu/cpu%zu/cache/index3/size";
const char* kNode = "/sys/devices/system/node/node%zu/cpulist";

// sysfs values can be arbitrarily large, so store in a map and replace with
// indices in order of appearance.
class Remapper {
 public:
  // Returns false on error, or sets `out_index` to the index of the sysfs
  // value selected by `format` and `lp`.
  template <typename T>
  bool operator()(const char* format, size_t lp, T* HWY_RESTRICT out_index) {
    size_t opaque;
    if (!ReadNumberWithOptionalSuffix(format, lp, &opaque)) return false;

    const auto ib = indices_.insert({opaque, num_});
    num_ += ib.second;                      // increment if inserted
    const size_t index = ib.first->second;  // new or existing
    HWY_ASSERT(index < num_);
    HWY_ASSERT(index < hwy::LimitsMax<T>());
    *out_index = static_cast<T>(index);
    return true;
  }

  size_t Num() const { return num_; }

 private:
  std::map<size_t, size_t> indices_;
  size_t num_ = 0;
};

// Stores the global cluster/core values separately for each package so we can
// return per-package arrays.
struct PerPackage {
  Remapper clusters;
  Remapper cores;
  uint8_t smt_per_core[kMaxLogicalProcessors] = {0};
};

// Initializes `lps` and returns a PerPackage vector (empty on failure).
std::vector<PerPackage> DetectPackages(std::vector<Topology::LP>& lps) {
  std::vector<PerPackage> empty;

  Remapper packages;
  for (size_t lp = 0; lp < lps.size(); ++lp) {
    if (!packages(kPackage, lp, &lps[lp].package)) return empty;
  }
  std::vector<PerPackage> per_package(packages.Num());

  for (size_t lp = 0; lp < lps.size(); ++lp) {
    PerPackage& pp = per_package[lps[lp].package];
    if (!pp.clusters(kCluster, lp, &lps[lp].cluster)) return empty;
    if (!pp.cores(kCore, lp, &lps[lp].core)) return empty;

    // SMT ID is how many LP we have already seen assigned to the same core.
    HWY_ASSERT(lps[lp].core < kMaxLogicalProcessors);
    lps[lp].smt = pp.smt_per_core[lps[lp].core]++;
    HWY_ASSERT(lps[lp].smt < 16);
  }

  return per_package;
}

// Sets LP.node for all `lps`.
void SetNodes(std::vector<Topology::LP>& lps) {
  // For each NUMA node found via sysfs:
  for (size_t node = 0;; node++) {
    // Read its cpulist so we can scatter `node` to all its `lps`.
    char buf200[200];
    const size_t bytes_read = ReadSysfs(kNode, node, buf200);
    if (bytes_read == 0) break;

    constexpr size_t kNotFound = ~size_t{0};
    size_t pos = 0;

    // Returns first `found_pos >= pos` where `buf200[found_pos] == c`, or
    // `kNotFound`.
    const auto find = [buf200, &pos](char c) -> size_t {
      const char* found_ptr = strchr(buf200 + pos, c);
      if (found_ptr == nullptr) return kNotFound;
      HWY_ASSERT(found_ptr >= buf200);
      const size_t found_pos = static_cast<size_t>(found_ptr - buf200);
      HWY_ASSERT(found_pos >= pos && buf200[found_pos] == c);
      return found_pos;
    };

    // Reads LP number and advances `pos`. `end` is for verifying we did not
    // read past a known terminator, or the end of string.
    const auto parse_lp = [buf200, bytes_read, &pos,
                           &lps](size_t end) -> size_t {
      end = HWY_MIN(end, bytes_read);
      size_t lp;
      HWY_ASSERT(ParseDigits(buf200, end, pos, &lp));
      HWY_IF_CONSTEXPR(HWY_ARCH_RISCV) {
        // On RISC-V, both TotalLogicalProcessors and GetThreadAffinity may
        // under-report the count, hence clamp.
        lp = HWY_MIN(lp, lps.size() - 1);
      }
      HWY_ASSERT(lp < lps.size());
      HWY_ASSERT(pos <= end);
      return lp;
    };

    // Parse all [first-]last separated by commas.
    for (;;) {
      // Single number or first of range: ends with dash, comma, or end.
      const size_t lp_range_first = parse_lp(HWY_MIN(find('-'), find(',')));

      if (buf200[pos] == '-') {  // range
        ++pos;                   // skip dash
        // Last of range ends with comma or end.
        const size_t lp_range_last = parse_lp(find(','));

        for (size_t lp = lp_range_first; lp <= lp_range_last; ++lp) {
          lps[lp].node = static_cast<uint8_t>(node);
        }
      } else {  // single number
        lps[lp_range_first].node = static_cast<uint8_t>(node);
      }

      // Done if reached end of string.
      if (pos == bytes_read || buf200[pos] == '\0' || buf200[pos] == '\n') {
        break;
      }
      // Comma means at least one more term is coming.
      if (buf200[pos] == ',') {
        ++pos;
        continue;
      }
      HWY_ABORT("Unexpected character at %zu in %s\n", pos, buf200);
    }  // for pos
  }  // for node
}

}  // namespace
#endif  // HWY_OS_LINUX

HWY_CONTRIB_DLLEXPORT Topology::Topology() {
#if HWY_OS_LINUX
  lps.resize(TotalLogicalProcessors());
  const std::vector<PerPackage>& per_package = DetectPackages(lps);
  if (per_package.empty()) return;
  SetNodes(lps);

  // Allocate per-package/cluster/core vectors. This indicates to callers that
  // detection succeeded.
  packages.resize(per_package.size());
  for (size_t p = 0; p < packages.size(); ++p) {
    packages[p].clusters.resize(per_package[p].clusters.Num());
    packages[p].cores.resize(per_package[p].cores.Num());
  }

  // Populate the per-cluster/core sets of LP.
  for (size_t lp = 0; lp < lps.size(); ++lp) {
    Package& p = packages[lps[lp].package];
    p.clusters[lps[lp].cluster].lps.Set(lp);
    p.cores[lps[lp].core].lps.Set(lp);
  }

  // Detect cache sizes (only once per cluster)
  for (size_t ip = 0; ip < packages.size(); ++ip) {
    Package& p = packages[ip];
    for (size_t ic = 0; ic < p.clusters.size(); ++ic) {
      Cluster& c = p.clusters[ic];
      const size_t lp = c.lps.First();
      size_t bytes;
      if (ReadNumberWithOptionalSuffix(kL2Size, lp, &bytes)) {
        c.private_kib = bytes >> 10;
      }
      if (ReadNumberWithOptionalSuffix(kL3Size, lp, &bytes)) {
        c.shared_kib = bytes >> 10;
      }
    }
  }
#endif
}

}  // namespace hwy
