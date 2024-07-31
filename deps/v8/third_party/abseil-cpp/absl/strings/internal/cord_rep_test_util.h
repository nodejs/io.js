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

#ifndef ABSL_STRINGS_INTERNAL_CORD_REP_TEST_UTIL_H_
#define ABSL_STRINGS_INTERNAL_CORD_REP_TEST_UTIL_H_

#include <cassert>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cordrep_testing {

inline cord_internal::CordRepSubstring* MakeSubstring(
    size_t start, size_t len, cord_internal::CordRep* rep) {
  auto* sub = new cord_internal::CordRepSubstring;
  sub->tag = cord_internal::SUBSTRING;
  sub->start = start;
  sub->length = len <= 0 ? rep->length - start + len : len;
  sub->child = rep;
  return sub;
}

inline cord_internal::CordRepFlat* MakeFlat(absl::string_view value) {
  assert(value.length() <= cord_internal::kMaxFlatLength);
  auto* flat = cord_internal::CordRepFlat::New(value.length());
  flat->length = value.length();
  memcpy(flat->Data(), value.data(), value.length());
  return flat;
}

// Creates an external node for testing
inline cord_internal::CordRepExternal* MakeExternal(absl::string_view s) {
  struct Rep : public cord_internal::CordRepExternal {
    std::string s;
    explicit Rep(absl::string_view sv) : s(sv) {
      this->tag = cord_internal::EXTERNAL;
      this->base = s.data();
      this->length = s.length();
      this->releaser_invoker = [](cord_internal::CordRepExternal* self) {
        delete static_cast<Rep*>(self);
      };
    }
  };
  return new Rep(s);
}

inline std::string CreateRandomString(size_t n) {
  absl::string_view data =
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789~!@#$%^&*()_+=-<>?:\"{}[]|";
  std::minstd_rand rnd;
  std::uniform_int_distribution<size_t> dist(0, data.size() - 1);
  std::string s(n, ' ');
  for (size_t i = 0; i < n; ++i) {
    s[i] = data[dist(rnd)];
  }
  return s;
}

// Creates an array of flats from the provided string, chopping
// the provided string up into flats of size `chunk_size` characters
// resulting in roughly `data.size() / chunk_size` total flats.
inline std::vector<cord_internal::CordRep*> CreateFlatsFromString(
    absl::string_view data, size_t chunk_size) {
  assert(chunk_size > 0);
  std::vector<cord_internal::CordRep*> flats;
  for (absl::string_view s = data; !s.empty(); s.remove_prefix(chunk_size)) {
    flats.push_back(MakeFlat(s.substr(0, chunk_size)));
  }
  return flats;
}

inline cord_internal::CordRepBtree* CordRepBtreeFromFlats(
    absl::Span<cord_internal::CordRep* const> flats) {
  assert(!flats.empty());
  auto* node = cord_internal::CordRepBtree::Create(flats[0]);
  for (size_t i = 1; i < flats.size(); ++i) {
    node = cord_internal::CordRepBtree::Append(node, flats[i]);
  }
  return node;
}

template <typename Fn>
inline void CordVisitReps(cord_internal::CordRep* rep, Fn&& fn) {
  fn(rep);
  while (rep->tag == cord_internal::SUBSTRING) {
    rep = rep->substring()->child;
    fn(rep);
  }
  if (rep->tag == cord_internal::BTREE) {
    for (cord_internal::CordRep* edge : rep->btree()->Edges()) {
      CordVisitReps(edge, fn);
    }
  }
}

template <typename Predicate>
inline std::vector<cord_internal::CordRep*> CordCollectRepsIf(
    Predicate&& predicate, cord_internal::CordRep* rep) {
  std::vector<cord_internal::CordRep*> reps;
  CordVisitReps(rep, [&reps, &predicate](cord_internal::CordRep* rep) {
    if (predicate(rep)) reps.push_back(rep);
  });
  return reps;
}

inline std::vector<cord_internal::CordRep*> CordCollectReps(
    cord_internal::CordRep* rep) {
  std::vector<cord_internal::CordRep*> reps;
  auto fn = [&reps](cord_internal::CordRep* rep) { reps.push_back(rep); };
  CordVisitReps(rep, fn);
  return reps;
}

inline void CordToString(cord_internal::CordRep* rep, std::string& s) {
  size_t offset = 0;
  size_t length = rep->length;
  while (rep->tag == cord_internal::SUBSTRING) {
    offset += rep->substring()->start;
    rep = rep->substring()->child;
  }
  if (rep->tag == cord_internal::BTREE) {
    for (cord_internal::CordRep* edge : rep->btree()->Edges()) {
      CordToString(edge, s);
    }
  } else if (rep->tag >= cord_internal::FLAT) {
    s.append(rep->flat()->Data() + offset, length);
  } else if (rep->tag == cord_internal::EXTERNAL) {
    s.append(rep->external()->base + offset, length);
  } else {
    ABSL_RAW_LOG(FATAL, "Unsupported tag %d", rep->tag);
  }
}

inline std::string CordToString(cord_internal::CordRep* rep) {
  std::string s;
  s.reserve(rep->length);
  CordToString(rep, s);
  return s;
}

// RAII Helper class to automatically unref reps on destruction.
class AutoUnref {
 public:
  ~AutoUnref() {
    for (CordRep* rep : unrefs_) CordRep::Unref(rep);
  }

  // Adds `rep` to the list of reps to be unreffed at destruction.
  template <typename CordRepType>
  CordRepType* Add(CordRepType* rep) {
    unrefs_.push_back(rep);
    return rep;
  }

  // Increments the reference count of `rep` by one, and adds it to
  // the list of reps to be unreffed at destruction.
  template <typename CordRepType>
  CordRepType* Ref(CordRepType* rep) {
    unrefs_.push_back(CordRep::Ref(rep));
    return rep;
  }

  // Increments the reference count of `rep` by one if `condition` is true,
  // and adds it to the list of reps to be unreffed at destruction.
  template <typename CordRepType>
  CordRepType* RefIf(bool condition, CordRepType* rep) {
    if (condition) unrefs_.push_back(CordRep::Ref(rep));
    return rep;
  }

 private:
  using CordRep = absl::cord_internal::CordRep;

  std::vector<CordRep*> unrefs_;
};

}  // namespace cordrep_testing
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_REP_TEST_UTIL_H_
