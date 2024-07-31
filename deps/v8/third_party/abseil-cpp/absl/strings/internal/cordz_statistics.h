// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_STATISTICS_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_STATISTICS_H_

#include <cstdint>

#include "absl/base/config.h"
#include "absl/strings/internal/cordz_update_tracker.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// CordzStatistics captures some meta information about a Cord's shape.
struct CordzStatistics {
  using MethodIdentifier = CordzUpdateTracker::MethodIdentifier;

  // Node counts information
  struct NodeCounts {
    size_t flat = 0;       // #flats
    size_t flat_64 = 0;    // #flats up to 64 bytes
    size_t flat_128 = 0;   // #flats up to 128 bytes
    size_t flat_256 = 0;   // #flats up to 256 bytes
    size_t flat_512 = 0;   // #flats up to 512 bytes
    size_t flat_1k = 0;    // #flats up to 1K bytes
    size_t external = 0;   // #external reps
    size_t substring = 0;  // #substring reps
    size_t concat = 0;     // #concat reps
    size_t ring = 0;       // #ring buffer reps
    size_t btree = 0;      // #btree reps
    size_t crc = 0;        // #crc reps
  };

  // The size of the cord in bytes. This matches the result of Cord::size().
  size_t size = 0;

  // The estimated memory used by the sampled cord. This value matches the
  // value as reported by Cord::EstimatedMemoryUsage().
  // A value of 0 implies the property has not been recorded.
  size_t estimated_memory_usage = 0;

  // The effective memory used by the sampled cord, inversely weighted by the
  // effective indegree of each allocated node. This is a representation of the
  // fair share of memory usage that should be attributed to the sampled cord.
  // This value is more useful for cases where one or more nodes are referenced
  // by multiple Cord instances, and for cases where a Cord includes the same
  // node multiple times (either directly or indirectly).
  // A value of 0 implies the property has not been recorded.
  size_t estimated_fair_share_memory_usage = 0;

  // The total number of nodes referenced by this cord.
  // For ring buffer Cords, this includes the 'ring buffer' node.
  // For btree Cords, this includes all 'CordRepBtree' tree nodes as well as all
  // the substring, flat and external nodes referenced by the tree.
  // A value of 0 implies the property has not been recorded.
  size_t node_count = 0;

  // Detailed node counts per type
  NodeCounts node_counts;

  // The cord method responsible for sampling the cord.
  MethodIdentifier method = MethodIdentifier::kUnknown;

  // The cord method responsible for sampling the parent cord if applicable.
  MethodIdentifier parent_method = MethodIdentifier::kUnknown;

  // Update tracker tracking invocation count per cord method.
  CordzUpdateTracker update_tracker;
};

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_STATISTICS_H_
