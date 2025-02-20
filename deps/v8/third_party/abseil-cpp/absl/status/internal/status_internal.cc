// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/status/internal/status_internal.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/debugging/leak_check.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/status_payload_printer.h"
#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace status_internal {

void StatusRep::Unref() const {
  // Fast path: if ref==1, there is no need for a RefCountDec (since
  // this is the only reference and therefore no other thread is
  // allowed to be mucking with r).
  if (ref_.load(std::memory_order_acquire) == 1 ||
      ref_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
    delete this;
  }
}

static absl::optional<size_t> FindPayloadIndexByUrl(
    const Payloads* payloads, absl::string_view type_url) {
  if (payloads == nullptr) return absl::nullopt;

  for (size_t i = 0; i < payloads->size(); ++i) {
    if ((*payloads)[i].type_url == type_url) return i;
  }

  return absl::nullopt;
}

absl::optional<absl::Cord> StatusRep::GetPayload(
    absl::string_view type_url) const {
  absl::optional<size_t> index =
      status_internal::FindPayloadIndexByUrl(payloads_.get(), type_url);
  if (index.has_value()) return (*payloads_)[index.value()].payload;

  return absl::nullopt;
}

void StatusRep::SetPayload(absl::string_view type_url, absl::Cord payload) {
  if (payloads_ == nullptr) {
    payloads_ = absl::make_unique<status_internal::Payloads>();
  }

  absl::optional<size_t> index =
      status_internal::FindPayloadIndexByUrl(payloads_.get(), type_url);
  if (index.has_value()) {
    (*payloads_)[index.value()].payload = std::move(payload);
    return;
  }

  payloads_->push_back({std::string(type_url), std::move(payload)});
}

StatusRep::EraseResult StatusRep::ErasePayload(absl::string_view type_url) {
  absl::optional<size_t> index =
      status_internal::FindPayloadIndexByUrl(payloads_.get(), type_url);
  if (!index.has_value()) return {false, Status::PointerToRep(this)};
  payloads_->erase(payloads_->begin() + index.value());
  if (payloads_->empty() && message_.empty()) {
    // Special case: If this can be represented inlined, it MUST be inlined
    // (== depends on this behavior).
    EraseResult result = {true, Status::CodeToInlinedRep(code_)};
    Unref();
    return result;
  }
  return {true, Status::PointerToRep(this)};
}

void StatusRep::ForEachPayload(
    absl::FunctionRef<void(absl::string_view, const absl::Cord&)> visitor)
    const {
  if (auto* payloads = payloads_.get()) {
    bool in_reverse =
        payloads->size() > 1 && reinterpret_cast<uintptr_t>(payloads) % 13 > 6;

    for (size_t index = 0; index < payloads->size(); ++index) {
      const auto& elem =
          (*payloads)[in_reverse ? payloads->size() - 1 - index : index];

#ifdef NDEBUG
      visitor(elem.type_url, elem.payload);
#else
      // In debug mode invalidate the type url to prevent users from relying on
      // this string lifetime.

      // NOLINTNEXTLINE intentional extra conversion to force temporary.
      visitor(std::string(elem.type_url), elem.payload);
#endif  // NDEBUG
    }
  }
}

std::string StatusRep::ToString(StatusToStringMode mode) const {
  std::string text;
  absl::StrAppend(&text, absl::StatusCodeToString(code()), ": ", message());

  const bool with_payload = (mode & StatusToStringMode::kWithPayload) ==
                            StatusToStringMode::kWithPayload;

  if (with_payload) {
    status_internal::StatusPayloadPrinter printer =
        status_internal::GetStatusPayloadPrinter();
    this->ForEachPayload([&](absl::string_view type_url,
                             const absl::Cord& payload) {
      absl::optional<std::string> result;
      if (printer) result = printer(type_url, payload);
      absl::StrAppend(
          &text, " [", type_url, "='",
          result.has_value() ? *result : absl::CHexEscape(std::string(payload)),
          "']");
    });
  }

  return text;
}

bool StatusRep::operator==(const StatusRep& other) const {
  assert(this != &other);
  if (code_ != other.code_) return false;
  if (message_ != other.message_) return false;
  const status_internal::Payloads* this_payloads = payloads_.get();
  const status_internal::Payloads* other_payloads = other.payloads_.get();

  const status_internal::Payloads no_payloads;
  const status_internal::Payloads* larger_payloads =
      this_payloads ? this_payloads : &no_payloads;
  const status_internal::Payloads* smaller_payloads =
      other_payloads ? other_payloads : &no_payloads;
  if (larger_payloads->size() < smaller_payloads->size()) {
    std::swap(larger_payloads, smaller_payloads);
  }
  if ((larger_payloads->size() - smaller_payloads->size()) > 1) return false;
  // Payloads can be ordered differently, so we can't just compare payload
  // vectors.
  for (const auto& payload : *larger_payloads) {

    bool found = false;
    for (const auto& other_payload : *smaller_payloads) {
      if (payload.type_url == other_payload.type_url) {
        if (payload.payload != other_payload.payload) {
          return false;
        }
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

absl::Nonnull<StatusRep*> StatusRep::CloneAndUnref() const {
  // Optimization: no need to create a clone if we already have a refcount of 1.
  if (ref_.load(std::memory_order_acquire) == 1) {
    // All StatusRep instances are heap allocated and mutable, therefore this
    // const_cast will never cast away const from a stack instance.
    //
    // CloneAndUnref is the only method that doesn't involve an external cast to
    // get a mutable StatusRep* from the uintptr_t rep stored in Status.
    return const_cast<StatusRep*>(this);
  }
  std::unique_ptr<status_internal::Payloads> payloads;
  if (payloads_) {
    payloads = absl::make_unique<status_internal::Payloads>(*payloads_);
  }
  auto* new_rep = new StatusRep(code_, message_, std::move(payloads));
  Unref();
  return new_rep;
}

// Convert canonical code to a value known to this binary.
absl::StatusCode MapToLocalCode(int value) {
  absl::StatusCode code = static_cast<absl::StatusCode>(value);
  switch (code) {
    case absl::StatusCode::kOk:
    case absl::StatusCode::kCancelled:
    case absl::StatusCode::kUnknown:
    case absl::StatusCode::kInvalidArgument:
    case absl::StatusCode::kDeadlineExceeded:
    case absl::StatusCode::kNotFound:
    case absl::StatusCode::kAlreadyExists:
    case absl::StatusCode::kPermissionDenied:
    case absl::StatusCode::kResourceExhausted:
    case absl::StatusCode::kFailedPrecondition:
    case absl::StatusCode::kAborted:
    case absl::StatusCode::kOutOfRange:
    case absl::StatusCode::kUnimplemented:
    case absl::StatusCode::kInternal:
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kDataLoss:
    case absl::StatusCode::kUnauthenticated:
      return code;
    default:
      return absl::StatusCode::kUnknown;
  }
}

absl::Nonnull<const char*> MakeCheckFailString(
    absl::Nonnull<const absl::Status*> status,
    absl::Nonnull<const char*> prefix) {
  // There's no need to free this string since the process is crashing.
  return absl::IgnoreLeak(
             new std::string(absl::StrCat(
                 prefix, " (",
                 status->ToString(StatusToStringMode::kWithEverything), ")")))
      ->c_str();
}

}  // namespace status_internal

ABSL_NAMESPACE_END
}  // namespace absl
