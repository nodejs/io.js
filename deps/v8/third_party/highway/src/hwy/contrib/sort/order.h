// Copyright 2023 Google LLC
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

// Tag arguments that determine the sort order. Used by both vqsort.h and the
// VQSortStatic in vqsort-inl.h. Moved to a separate header so that the latter
// can be used without pulling in the dllimport statements in vqsort.h.

#ifndef HIGHWAY_HWY_CONTRIB_SORT_ORDER_H_
#define HIGHWAY_HWY_CONTRIB_SORT_ORDER_H_

namespace hwy {

struct SortAscending {
  static constexpr bool IsAscending() { return true; }
};
struct SortDescending {
  static constexpr bool IsAscending() { return false; }
};

}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_SORT_ORDER_H_
