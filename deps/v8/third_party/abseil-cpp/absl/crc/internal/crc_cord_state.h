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

#ifndef ABSL_CRC_INTERNAL_CRC_CORD_STATE_H_
#define ABSL_CRC_INTERNAL_CRC_CORD_STATE_H_

#include <atomic>
#include <cstddef>
#include <deque>

#include "absl/base/config.h"
#include "absl/crc/crc32c.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

// CrcCordState is a copy-on-write class that holds the chunked CRC32C data
// that allows CrcCord to perform efficient substring operations. CrcCordState
// is used as a member variable in CrcCord. When a CrcCord is converted to a
// Cord, the CrcCordState is shallow-copied into the root node of the Cord. If
// the converted Cord is modified outside of CrcCord, the CrcCordState is
// discarded from the Cord. If the Cord is converted back to a CrcCord, and the
// Cord is still carrying the CrcCordState in its root node, the CrcCord can
// re-use the CrcCordState, making the construction of the CrcCord cheap.
//
// CrcCordState does not try to encapsulate the CRC32C state (CrcCord requires
// knowledge of how CrcCordState represents the CRC32C state). It does
// encapsulate the copy-on-write nature of the state.
class CrcCordState {
 public:
  // Constructors.
  CrcCordState();
  CrcCordState(const CrcCordState&);
  CrcCordState(CrcCordState&&);

  // Destructor. Atomically unreferences the data.
  ~CrcCordState();

  // Copy and move operators.
  CrcCordState& operator=(const CrcCordState&);
  CrcCordState& operator=(CrcCordState&&);

  // A (length, crc) pair.
  struct PrefixCrc {
    PrefixCrc() = default;
    PrefixCrc(size_t length_arg, absl::crc32c_t crc_arg)
        : length(length_arg), crc(crc_arg) {}

    size_t length = 0;

    // TODO(absl-team): Memory stomping often zeros out memory. If this struct
    // gets overwritten, we could end up with {0, 0}, which is the correct CRC
    // for a string of length 0. Consider storing a scrambled value and
    // unscrambling it before verifying it.
    absl::crc32c_t crc = absl::crc32c_t{0};
  };

  // The representation of the chunked CRC32C data.
  struct Rep {
    // `removed_prefix` is the crc and length of any prefix that has been
    // removed from the Cord (for example, by calling
    // `CrcCord::RemovePrefix()`). To get the checksum of any prefix of the
    // cord, this value must be subtracted from `prefix_crc`. See `Checksum()`
    // for an example.
    //
    // CrcCordState is said to be "normalized" if removed_prefix.length == 0.
    PrefixCrc removed_prefix;

    // A deque of (length, crc) pairs, representing length and crc of a prefix
    // of the Cord, before removed_prefix has been subtracted. The lengths of
    // the prefixes are stored in increasing order. If the Cord is not empty,
    // the last value in deque is the contains the CRC32C of the entire Cord
    // when removed_prefix is subtracted from it.
    std::deque<PrefixCrc> prefix_crc;
  };

  // Returns a reference to the representation of the chunked CRC32C data.
  const Rep& rep() const { return refcounted_rep_->rep; }

  // Returns a mutable reference to the representation of the chunked CRC32C
  // data. Calling this function will copy the data if another instance also
  // holds a reference to the data, so it is important to call rep() instead if
  // the data may not be mutated.
  Rep* mutable_rep() {
    if (refcounted_rep_->count.load(std::memory_order_acquire) != 1) {
      RefcountedRep* copy = new RefcountedRep;
      copy->rep = refcounted_rep_->rep;
      Unref(refcounted_rep_);
      refcounted_rep_ = copy;
    }
    return &refcounted_rep_->rep;
  }

  // Returns the CRC32C of the entire Cord.
  absl::crc32c_t Checksum() const;

  // Returns true if the chunked CRC32C cached is normalized.
  bool IsNormalized() const { return rep().removed_prefix.length == 0; }

  // Normalizes the chunked CRC32C checksum cache by subtracting any removed
  // prefix from the chunks.
  void Normalize();

  // Returns the number of cached chunks.
  size_t NumChunks() const { return rep().prefix_crc.size(); }

  // Helper that returns the (length, crc) of the `n`-th cached chunked.
  PrefixCrc NormalizedPrefixCrcAtNthChunk(size_t n) const;

  // Poisons all chunks to so that Checksum() will likely be incorrect with high
  // probability.
  void Poison();

 private:
  struct RefcountedRep {
    std::atomic<int32_t> count{1};
    Rep rep;
  };

  // Adds a reference to the shared global empty `RefcountedRep`, and returns a
  // pointer to the `RefcountedRep`. This is an optimization to avoid unneeded
  // allocations when the allocation is unlikely to ever be used. The returned
  // pointer can be `Unref()`ed when it is no longer needed.  Since the returned
  // instance will always have a reference counter greater than 1, attempts to
  // modify it (by calling `mutable_rep()`) will create a new unshared copy.
  static RefcountedRep* RefSharedEmptyRep();

  static void Ref(RefcountedRep* r) {
    assert(r != nullptr);
    r->count.fetch_add(1, std::memory_order_relaxed);
  }

  static void Unref(RefcountedRep* r) {
    assert(r != nullptr);
    if (r->count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete r;
    }
  }

  RefcountedRep* refcounted_rep_;
};

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CRC_INTERNAL_CRC_CORD_STATE_H_
