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
#include <stdio.h>

#include <vector>

#include "hwy/base.h"
#include "hwy/tests/hwy_gtest.h"
#include "hwy/tests/test_util-inl.h"
#include "hwy/timer.h"

namespace hwy {
namespace {

TEST(TopologyTest, TestNum) {
  const size_t total = TotalLogicalProcessors();
  fprintf(stderr, "TotalLogical %zu\n", total);

  LogicalProcessorSet lps;
  if (GetThreadAffinity(lps)) {
    fprintf(stderr, "Active %zu\n", lps.Count());
    HWY_ASSERT(lps.Count() <= total);
  }
}

TEST(TopologyTest, TestTopology) {
  char cpu100[100];
  if (hwy::platform::GetCpuString(cpu100)) {
    fprintf(stderr, "%s\n", cpu100);
  }

  Topology topology;
  if (topology.packages.empty()) return;

  HWY_ASSERT(!topology.lps.empty());
  LogicalProcessorSet nodes;
  for (size_t lp = 0; lp < topology.lps.size(); ++lp) {
    const size_t node = static_cast<size_t>(topology.lps[lp].node);
    if (!nodes.Get(node)) {
      fprintf(stderr, "Found NUMA node %zu, LP %zu\n", node, lp);
      nodes.Set(node);
    }
  }

  size_t lps_by_cluster = 0;
  size_t lps_by_core = 0;
  LogicalProcessorSet all_lps;
  for (size_t p = 0; p < topology.packages.size(); ++p) {
    const Topology::Package& pkg = topology.packages[p];
    HWY_ASSERT(!pkg.clusters.empty());
    HWY_ASSERT(!pkg.cores.empty());
    HWY_ASSERT(pkg.clusters.size() <= pkg.cores.size());

    for (const Topology::Cluster& c : pkg.clusters) {
      lps_by_cluster += c.lps.Count();
      c.lps.Foreach([&all_lps](size_t lp) { all_lps.Set(lp); });
    }
    for (const Topology::Core& c : pkg.cores) {
      lps_by_core += c.lps.Count();
      c.lps.Foreach([&all_lps](size_t lp) { all_lps.Set(lp); });
    }
  }
  // Ensure the per-cluster and per-core sets sum to the total.
  HWY_ASSERT(lps_by_cluster == topology.lps.size());
  HWY_ASSERT(lps_by_core == topology.lps.size());
  // .. and are a partition of unity (all LPs are covered)
  HWY_ASSERT(all_lps.Count() == topology.lps.size());
}

}  // namespace
}  // namespace hwy

HWY_TEST_MAIN();
