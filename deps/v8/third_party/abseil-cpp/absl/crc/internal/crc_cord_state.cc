// Copyright 2022 The Abseil Authors
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

#include "absl/crc/internal/crc_cord_state.h"

#include <cassert>

#include "absl/base/config.h"
#include "absl/base/no_destructor.h"
#include "absl/numeric/bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

CrcCordState::RefcountedRep* CrcCordState::RefSharedEmptyRep() {
  static absl::NoDestructor<CrcCordState::RefcountedRep> empty;

  assert(empty->count.load(std::memory_order_relaxed) >= 1);
  assert(empty->rep.removed_prefix.length == 0);
  assert(empty->rep.prefix_crc.empty());

  Ref(empty.get());
  return empty.get();
}

CrcCordState::CrcCordState() : refcounted_rep_(new RefcountedRep) {}

CrcCordState::CrcCordState(const CrcCordState& other)
    : refcounted_rep_(other.refcounted_rep_) {
  Ref(refcounted_rep_);
}

CrcCordState::CrcCordState(CrcCordState&& other)
    : refcounted_rep_(other.refcounted_rep_) {
  // Make `other` valid for use after move.
  other.refcounted_rep_ = RefSharedEmptyRep();
}

CrcCordState& CrcCordState::operator=(const CrcCordState& other) {
  if (this != &other) {
    Unref(refcounted_rep_);
    refcounted_rep_ = other.refcounted_rep_;
    Ref(refcounted_rep_);
  }
  return *this;
}

CrcCordState& CrcCordState::operator=(CrcCordState&& other) {
  if (this != &other) {
    Unref(refcounted_rep_);
    refcounted_rep_ = other.refcounted_rep_;
    // Make `other` valid for use after move.
    other.refcounted_rep_ = RefSharedEmptyRep();
  }
  return *this;
}

CrcCordState::~CrcCordState() {
  Unref(refcounted_rep_);
}

crc32c_t CrcCordState::Checksum() const {
  if (rep().prefix_crc.empty()) {
    return absl::crc32c_t{0};
  }
  if (IsNormalized()) {
    return rep().prefix_crc.back().crc;
  }
  return absl::RemoveCrc32cPrefix(
      rep().removed_prefix.crc, rep().prefix_crc.back().crc,
      rep().prefix_crc.back().length - rep().removed_prefix.length);
}

CrcCordState::PrefixCrc CrcCordState::NormalizedPrefixCrcAtNthChunk(
    size_t n) const {
  assert(n < NumChunks());
  if (IsNormalized()) {
    return rep().prefix_crc[n];
  }
  size_t length = rep().prefix_crc[n].length - rep().removed_prefix.length;
  return PrefixCrc(length,
                   absl::RemoveCrc32cPrefix(rep().removed_prefix.crc,
                                            rep().prefix_crc[n].crc, length));
}

void CrcCordState::Normalize() {
  if (IsNormalized() || rep().prefix_crc.empty()) {
    return;
  }

  Rep* r = mutable_rep();
  for (auto& prefix_crc : r->prefix_crc) {
    size_t remaining = prefix_crc.length - r->removed_prefix.length;
    prefix_crc.crc = absl::RemoveCrc32cPrefix(r->removed_prefix.crc,
                                              prefix_crc.crc, remaining);
    prefix_crc.length = remaining;
  }
  r->removed_prefix = PrefixCrc();
}

void CrcCordState::Poison() {
  Rep* rep = mutable_rep();
  if (NumChunks() > 0) {
    for (auto& prefix_crc : rep->prefix_crc) {
      // This is basically CRC32::Scramble().
      uint32_t crc = static_cast<uint32_t>(prefix_crc.crc);
      crc += 0x2e76e41b;
      crc = absl::rotr(crc, 17);
      prefix_crc.crc = crc32c_t{crc};
    }
  } else {
    // Add a fake corrupt chunk.
    rep->prefix_crc.emplace_back(0, crc32c_t{1});
  }
}

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl
