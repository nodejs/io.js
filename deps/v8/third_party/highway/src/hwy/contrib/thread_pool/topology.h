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

#ifndef HIGHWAY_HWY_CONTRIB_THREAD_POOL_TOPOLOGY_H_
#define HIGHWAY_HWY_CONTRIB_THREAD_POOL_TOPOLOGY_H_

// OS-specific functions for processor topology and thread affinity.

#include <stddef.h>

#include <vector>

#include "hwy/base.h"
#include "hwy/bit_set.h"

namespace hwy {

// Returns false if std::thread should not be used.
HWY_CONTRIB_DLLEXPORT bool HaveThreadingSupport();

// Upper bound on logical processors, including hyperthreads.
static constexpr size_t kMaxLogicalProcessors = 1024;  // matches glibc

// Set used by Get/SetThreadAffinity.
using LogicalProcessorSet = BitSet4096<kMaxLogicalProcessors>;

// Returns false, or sets `lps` to all logical processors which are online and
// available to the current thread.
HWY_CONTRIB_DLLEXPORT bool GetThreadAffinity(LogicalProcessorSet& lps);

// Ensures the current thread can only run on the logical processors in `lps`.
// Returns false if not supported (in particular on Apple), or if the
// intersection between `lps` and `GetThreadAffinity` is the empty set.
HWY_CONTRIB_DLLEXPORT bool SetThreadAffinity(const LogicalProcessorSet& lps);

// Returns false, or ensures the current thread will only run on `lp`, which
// must not exceed `TotalLogicalProcessors`. Note that this merely calls
// `SetThreadAffinity`, see the comment there.
static inline bool PinThreadToLogicalProcessor(size_t lp) {
  LogicalProcessorSet lps;
  lps.Set(lp);
  return SetThreadAffinity(lps);
}

// Returns 1 if unknown, otherwise the total number of logical processors
// provided by the hardware clamped to `kMaxLogicalProcessors`.
// These processors are not necessarily all usable; you can determine which are
// via GetThreadAffinity().
HWY_CONTRIB_DLLEXPORT size_t TotalLogicalProcessors();

struct Topology {
  // Caller must check packages.empty(); if so, do not use any fields.
  HWY_CONTRIB_DLLEXPORT Topology();

  // Clique of cores with lower latency to each other. On Apple M1 these are
  // four cores sharing an L2. On Zen4 these 'CCX' are up to eight cores sharing
  // an L3 and a memory controller, or for Zen4c up to 16 and half the L3 size.
  struct Cluster {
    LogicalProcessorSet lps;
    uint64_t private_kib = 0;  // 0 if unknown
    uint64_t shared_kib = 0;   // 0 if unknown
    uint64_t reserved1 = 0;
    uint64_t reserved2 = 0;
    uint64_t reserved3 = 0;
  };

  struct Core {
    LogicalProcessorSet lps;
    uint64_t reserved = 0;
  };

  struct Package {
    std::vector<Cluster> clusters;
    std::vector<Core> cores;
  };

  std::vector<Package> packages;

  // Several hundred instances, so prefer a compact representation.
#pragma pack(push, 1)
  struct LP {
    uint16_t cluster = 0;  // < packages[package].clusters.size()
    uint16_t core = 0;     // < packages[package].cores.size()
    uint8_t package = 0;   // < packages.size()
    uint8_t smt = 0;       // < packages[package].cores[core].lps.Count()
    uint8_t node = 0;

    uint8_t reserved = 0;
  };
#pragma pack(pop)
  std::vector<LP> lps;  // size() == TotalLogicalProcessors().
};

}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_THREAD_POOL_TOPOLOGY_H_
