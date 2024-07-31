// Copyright 2021 The Abseil Authors
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

#include "absl/strings/cord_analysis.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/strings/internal/cord_data_edge.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_crc.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

// Accounting mode for analyzing memory usage.
enum class Mode { kFairShare, kTotal, kTotalMorePrecise };

// CordRepRef holds a `const CordRep*` reference in rep, and depending on mode,
// holds a 'fraction' representing a cumulative inverse refcount weight.
template <Mode mode>
struct CordRepRef {
  // Instantiates a CordRepRef instance.
  explicit CordRepRef(absl::Nonnull<const CordRep*> r) : rep(r) {}

  // Creates a child reference holding the provided child.
  // Overloaded to add cumulative reference count for kFairShare.
  CordRepRef Child(absl::Nonnull<const CordRep*> child) const {
    return CordRepRef(child);
  }

  absl::Nonnull<const CordRep*> rep;
};

// RawUsage holds the computed total number of bytes.
template <Mode mode>
struct RawUsage {
  size_t total = 0;

  // Add 'size' to total, ignoring the CordRepRef argument.
  void Add(size_t size, CordRepRef<mode>) { total += size; }
};

// Overloaded representation of RawUsage that tracks the set of objects
// counted, and avoids double-counting objects referenced more than once
// by the same Cord.
template <>
struct RawUsage<Mode::kTotalMorePrecise> {
  size_t total = 0;
  // TODO(b/289250880): Replace this with a flat_hash_set.
  std::unordered_set<absl::Nonnull<const CordRep*>> counted;

  void Add(size_t size, CordRepRef<Mode::kTotalMorePrecise> repref) {
    if (counted.insert(repref.rep).second) {
      total += size;
    }
  }
};

// Returns n / refcount avoiding a div for the common refcount == 1.
template <typename refcount_t>
double MaybeDiv(double d, refcount_t refcount) {
  return refcount == 1 ? d : d / refcount;
}

// Overloaded 'kFairShare' specialization for CordRepRef. This class holds a
// `fraction` value which represents a cumulative inverse refcount weight.
// For example, a top node with a reference count of 2 will have a fraction
// value of 1/2 = 0.5, representing the 'fair share' of memory it references.
// A node below such a node with a reference count of 5 then has a fraction of
// 0.5 / 5 = 0.1 representing the fair share of memory below that node, etc.
template <>
struct CordRepRef<Mode::kFairShare> {
  // Creates a CordRepRef with the provided rep and top (parent) fraction.
  explicit CordRepRef(absl::Nonnull<const CordRep*> r, double frac = 1.0)
      : rep(r), fraction(MaybeDiv(frac, r->refcount.Get())) {}

  // Returns a CordRepRef with a fraction of `this->fraction / child.refcount`
  CordRepRef Child(absl::Nonnull<const CordRep*> child) const {
    return CordRepRef(child, fraction);
  }

  absl::Nonnull<const CordRep*> rep;
  double fraction;
};

// Overloaded 'kFairShare' specialization for RawUsage
template <>
struct RawUsage<Mode::kFairShare> {
  double total = 0;

  // Adds `size` multiplied by `rep.fraction` to the total size.
  void Add(size_t size, CordRepRef<Mode::kFairShare> rep) {
    total += static_cast<double>(size) * rep.fraction;
  }
};

// Computes the estimated memory size of the provided data edge.
// External reps are assumed 'heap allocated at their exact size'.
template <Mode mode>
void AnalyzeDataEdge(CordRepRef<mode> rep, RawUsage<mode>& raw_usage) {
  assert(IsDataEdge(rep.rep));

  // Consume all substrings
  if (rep.rep->tag == SUBSTRING) {
    raw_usage.Add(sizeof(CordRepSubstring), rep);
    rep = rep.Child(rep.rep->substring()->child);
  }

  // Consume FLAT / EXTERNAL
  const size_t size =
      rep.rep->tag >= FLAT
          ? rep.rep->flat()->AllocatedSize()
          : rep.rep->length + sizeof(CordRepExternalImpl<intptr_t>);
  raw_usage.Add(size, rep);
}

// Computes the memory size of the provided Btree tree.
template <Mode mode>
void AnalyzeBtree(CordRepRef<mode> rep, RawUsage<mode>& raw_usage) {
  raw_usage.Add(sizeof(CordRepBtree), rep);
  const CordRepBtree* tree = rep.rep->btree();
  if (tree->height() > 0) {
    for (CordRep* edge : tree->Edges()) {
      AnalyzeBtree(rep.Child(edge), raw_usage);
    }
  } else {
    for (CordRep* edge : tree->Edges()) {
      AnalyzeDataEdge(rep.Child(edge), raw_usage);
    }
  }
}

template <Mode mode>
size_t GetEstimatedUsage(absl::Nonnull<const CordRep*> rep) {
  // Zero initialized memory usage totals.
  RawUsage<mode> raw_usage;

  // Capture top level node and refcount into a CordRepRef.
  CordRepRef<mode> repref(rep);

  // Consume the top level CRC node if present.
  if (repref.rep->tag == CRC) {
    raw_usage.Add(sizeof(CordRepCrc), repref);
    if (repref.rep->crc()->child == nullptr) {
      return static_cast<size_t>(raw_usage.total);
    }
    repref = repref.Child(repref.rep->crc()->child);
  }

  if (IsDataEdge(repref.rep)) {
    AnalyzeDataEdge(repref, raw_usage);
  } else if (repref.rep->tag == BTREE) {
    AnalyzeBtree(repref, raw_usage);
  } else {
    assert(false);
  }

  return static_cast<size_t>(raw_usage.total);
}

}  // namespace

size_t GetEstimatedMemoryUsage(absl::Nonnull<const CordRep*> rep) {
  return GetEstimatedUsage<Mode::kTotal>(rep);
}

size_t GetEstimatedFairShareMemoryUsage(absl::Nonnull<const CordRep*> rep) {
  return GetEstimatedUsage<Mode::kFairShare>(rep);
}

size_t GetMorePreciseMemoryUsage(absl::Nonnull<const CordRep*> rep) {
  return GetEstimatedUsage<Mode::kTotalMorePrecise>(rep);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
