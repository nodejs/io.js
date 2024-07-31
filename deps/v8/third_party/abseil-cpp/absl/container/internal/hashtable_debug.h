// Copyright 2018 The Abseil Authors.
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
//
// This library provides APIs to debug the probing behavior of hash tables.
//
// In general, the probing behavior is a black box for users and only the
// side effects can be measured in the form of performance differences.
// These APIs give a glimpse on the actual behavior of the probing algorithms in
// these hashtables given a specified hash function and a set of elements.
//
// The probe count distribution can be used to assess the quality of the hash
// function for that particular hash table. Note that a hash function that
// performs well in one hash table implementation does not necessarily performs
// well in a different one.
//
// This library supports std::unordered_{set,map}, dense_hash_{set,map} and
// absl::{flat,node,string}_hash_{set,map}.

#ifndef ABSL_CONTAINER_INTERNAL_HASHTABLE_DEBUG_H_
#define ABSL_CONTAINER_INTERNAL_HASHTABLE_DEBUG_H_

#include <cstddef>
#include <algorithm>
#include <type_traits>
#include <vector>

#include "absl/container/internal/hashtable_debug_hooks.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// Returns the number of probes required to lookup `key`.  Returns 0 for a
// search with no collisions.  Higher values mean more hash collisions occurred;
// however, the exact meaning of this number varies according to the container
// type.
template <typename C>
size_t GetHashtableDebugNumProbes(
    const C& c, const typename C::key_type& key) {
  return absl::container_internal::hashtable_debug_internal::
      HashtableDebugAccess<C>::GetNumProbes(c, key);
}

// Gets a histogram of the number of probes for each elements in the container.
// The sum of all the values in the vector is equal to container.size().
template <typename C>
std::vector<size_t> GetHashtableDebugNumProbesHistogram(const C& container) {
  std::vector<size_t> v;
  for (auto it = container.begin(); it != container.end(); ++it) {
    size_t num_probes = GetHashtableDebugNumProbes(
        container,
        absl::container_internal::hashtable_debug_internal::GetKey<C>(*it, 0));
    v.resize((std::max)(v.size(), num_probes + 1));
    v[num_probes]++;
  }
  return v;
}

struct HashtableDebugProbeSummary {
  size_t total_elements;
  size_t total_num_probes;
  double mean;
};

// Gets a summary of the probe count distribution for the elements in the
// container.
template <typename C>
HashtableDebugProbeSummary GetHashtableDebugProbeSummary(const C& container) {
  auto probes = GetHashtableDebugNumProbesHistogram(container);
  HashtableDebugProbeSummary summary = {};
  for (size_t i = 0; i < probes.size(); ++i) {
    summary.total_elements += probes[i];
    summary.total_num_probes += probes[i] * i;
  }
  summary.mean = 1.0 * summary.total_num_probes / summary.total_elements;
  return summary;
}

// Returns the number of bytes requested from the allocator by the container
// and not freed.
template <typename C>
size_t AllocatedByteSize(const C& c) {
  return absl::container_internal::hashtable_debug_internal::
      HashtableDebugAccess<C>::AllocatedByteSize(c);
}

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_HASHTABLE_DEBUG_H_
