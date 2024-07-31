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
#ifndef ABSL_STATUS_INTERNAL_STATUS_INTERNAL_H_
#define ABSL_STATUS_INTERNAL_STATUS_INTERNAL_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#ifndef SWIG
// Disabled for SWIG as it doesn't parse attributes correctly.
namespace absl {
ABSL_NAMESPACE_BEGIN
// Returned Status objects may not be ignored. Codesearch doesn't handle ifdefs
// as part of a class definitions (b/6995610), so we use a forward declaration.
//
// TODO(b/176172494): ABSL_MUST_USE_RESULT should expand to the more strict
// [[nodiscard]]. For now, just use [[nodiscard]] directly when it is available.
#if ABSL_HAVE_CPP_ATTRIBUTE(nodiscard)
class [[nodiscard]] ABSL_ATTRIBUTE_TRIVIAL_ABI Status;
#else
class ABSL_MUST_USE_RESULT ABSL_ATTRIBUTE_TRIVIAL_ABI Status;
#endif
ABSL_NAMESPACE_END
}  // namespace absl
#endif  // !SWIG

namespace absl {
ABSL_NAMESPACE_BEGIN

enum class StatusCode : int;
enum class StatusToStringMode : int;

namespace status_internal {

// Container for status payloads.
struct Payload {
  std::string type_url;
  absl::Cord payload;
};

using Payloads = absl::InlinedVector<Payload, 1>;

// Reference-counted representation of Status data.
class StatusRep {
 public:
  StatusRep(absl::StatusCode code_arg, absl::string_view message_arg,
            std::unique_ptr<status_internal::Payloads> payloads_arg)
      : ref_(int32_t{1}),
        code_(code_arg),
        message_(message_arg),
        payloads_(std::move(payloads_arg)) {}

  absl::StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

  // Ref and unref are const to allow access through a const pointer, and are
  // used during copying operations.
  void Ref() const { ref_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() const;

  // Payload methods correspond to the same methods in absl::Status.
  absl::optional<absl::Cord> GetPayload(absl::string_view type_url) const;
  void SetPayload(absl::string_view type_url, absl::Cord payload);
  struct EraseResult {
    bool erased;
    uintptr_t new_rep;
  };
  EraseResult ErasePayload(absl::string_view type_url);
  void ForEachPayload(
      absl::FunctionRef<void(absl::string_view, const absl::Cord&)> visitor)
      const;

  std::string ToString(StatusToStringMode mode) const;

  bool operator==(const StatusRep& other) const;
  bool operator!=(const StatusRep& other) const { return !(*this == other); }

  // Returns an equivalent heap allocated StatusRep with refcount 1.
  //
  // `this` is not safe to be used after calling as it may have been deleted.
  absl::Nonnull<StatusRep*> CloneAndUnref() const;

 private:
  mutable std::atomic<int32_t> ref_;
  absl::StatusCode code_;

  // As an internal implementation detail, we guarantee that if status.message()
  // is non-empty, then the resulting string_view is null terminated.
  // This is required to implement 'StatusMessageAsCStr(...)'
  std::string message_;
  std::unique_ptr<status_internal::Payloads> payloads_;
};

absl::StatusCode MapToLocalCode(int value);

// Returns a pointer to a newly-allocated string with the given `prefix`,
// suitable for output as an error message in assertion/`CHECK()` failures.
//
// This is an internal implementation detail for Abseil logging.
ABSL_ATTRIBUTE_PURE_FUNCTION
absl::Nonnull<std::string*> MakeCheckFailString(
    absl::Nonnull<const absl::Status*> status,
    absl::Nonnull<const char*> prefix);

}  // namespace status_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STATUS_INTERNAL_STATUS_INTERNAL_H_
