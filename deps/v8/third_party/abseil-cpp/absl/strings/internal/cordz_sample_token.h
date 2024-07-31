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

#include "absl/base/config.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_info.h"

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_SAMPLE_TOKEN_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_SAMPLE_TOKEN_H_

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// The existence of a CordzSampleToken guarantees that a reader can traverse the
// global_cordz_infos_head linked-list without needing to hold a mutex. When a
// CordzSampleToken exists, all CordzInfo objects that would be destroyed are
// instead appended to a deletion queue. When the CordzSampleToken is destroyed,
// it will also clean up any of these CordzInfo objects.
//
// E.g., ST are CordzSampleToken objects and CH are CordzHandle objects.
//   ST1 <- CH1 <- CH2 <- ST2 <- CH3 <- global_delete_queue_tail
//
// This list tracks that CH1 and CH2 were created after ST1, so the thread
// holding ST1 might have a reference to CH1, CH2, ST2, and CH3. However, ST2
// was created later, so the thread holding the ST2 token cannot have a
// reference to ST1, CH1, or CH2. If ST1 is cleaned up first, that thread will
// delete ST1, CH1, and CH2. If instead ST2 is cleaned up first, that thread
// will only delete ST2.
//
// If ST1 is cleaned up first, the new list will be:
//   ST2 <- CH3 <- global_delete_queue_tail
//
// If ST2 is cleaned up first, the new list will be:
//   ST1 <- CH1 <- CH2 <- CH3 <- global_delete_queue_tail
//
// All new CordzHandle objects are appended to the list, so if a new thread
// comes along before either ST1 or ST2 are cleaned up, the new list will be:
//   ST1 <- CH1 <- CH2 <- ST2 <- CH3 <- ST3 <- global_delete_queue_tail
//
// A thread must hold the global_delete_queue_mu mutex whenever it's altering
// this list.
//
// It is safe for thread that holds a CordzSampleToken to read
// global_cordz_infos at any time since the objects it is able to retrieve will
// not be deleted while the CordzSampleToken exists.
class CordzSampleToken : public CordzSnapshot {
 public:
  class Iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = const CordzInfo&;
    using difference_type = ptrdiff_t;
    using pointer = const CordzInfo*;
    using reference = value_type;

    Iterator() = default;

    Iterator& operator++();
    Iterator operator++(int);
    friend bool operator==(const Iterator& lhs, const Iterator& rhs);
    friend bool operator!=(const Iterator& lhs, const Iterator& rhs);
    reference operator*() const;
    pointer operator->() const;

   private:
    friend class CordzSampleToken;
    explicit Iterator(const CordzSampleToken* token);

    const CordzSampleToken* token_ = nullptr;
    pointer current_ = nullptr;
  };

  CordzSampleToken() = default;
  CordzSampleToken(const CordzSampleToken&) = delete;
  CordzSampleToken& operator=(const CordzSampleToken&) = delete;

  Iterator begin() { return Iterator(this); }
  Iterator end() { return Iterator(); }
};

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_SAMPLE_TOKEN_H_
