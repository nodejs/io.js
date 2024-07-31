// Copyright 2017 The Abseil Authors.
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

#ifndef ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_
#define ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/seed_material.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// This class conforms to the C++ Standard "Seed Sequence" concept
// [rand.req.seedseq].
//
// A `SaltedSeedSeq` is meant to wrap an existing seed sequence and modify
// generated sequence by mixing with extra entropy. This entropy may be
// build-dependent or process-dependent. The implementation may change to be
// have either or both kinds of entropy. If salt is not available sequence is
// not modified.
template <typename SSeq>
class SaltedSeedSeq {
 public:
  using inner_sequence_type = SSeq;
  using result_type = typename SSeq::result_type;

  SaltedSeedSeq() : seq_(absl::make_unique<SSeq>()) {}

  template <typename Iterator>
  SaltedSeedSeq(Iterator begin, Iterator end)
      : seq_(absl::make_unique<SSeq>(begin, end)) {}

  template <typename T>
  SaltedSeedSeq(std::initializer_list<T> il)
      : SaltedSeedSeq(il.begin(), il.end()) {}

  SaltedSeedSeq(const SaltedSeedSeq&) = delete;
  SaltedSeedSeq& operator=(const SaltedSeedSeq&) = delete;

  SaltedSeedSeq(SaltedSeedSeq&&) = default;
  SaltedSeedSeq& operator=(SaltedSeedSeq&&) = default;

  template <typename RandomAccessIterator>
  void generate(RandomAccessIterator begin, RandomAccessIterator end) {
    using U = typename std::iterator_traits<RandomAccessIterator>::value_type;

    // The common case is that generate is called with ContiguousIterators
    // to uint arrays. Such contiguous memory regions may be optimized,
    // which we detect here.
    using TagType = absl::conditional_t<
        (std::is_same<U, uint32_t>::value &&
         (std::is_pointer<RandomAccessIterator>::value ||
          std::is_same<RandomAccessIterator,
                       typename std::vector<U>::iterator>::value)),
        ContiguousAndUint32Tag, DefaultTag>;
    if (begin != end) {
      generate_impl(TagType{}, begin, end, std::distance(begin, end));
    }
  }

  template <typename OutIterator>
  void param(OutIterator out) const {
    seq_->param(out);
  }

  size_t size() const { return seq_->size(); }

 private:
  struct ContiguousAndUint32Tag {};
  struct DefaultTag {};

  // Generate which requires the iterators are contiguous pointers to uint32_t.
  // Fills the initial seed buffer the underlying SSeq::generate() call,
  // then mixes in the salt material.
  template <typename Contiguous>
  void generate_impl(ContiguousAndUint32Tag, Contiguous begin, Contiguous end,
                     size_t n) {
    seq_->generate(begin, end);
    const uint32_t salt = absl::random_internal::GetSaltMaterial().value_or(0);
    auto span = absl::Span<uint32_t>(&*begin, n);
    MixIntoSeedMaterial(absl::MakeConstSpan(&salt, 1), span);
  }

  // The uncommon case for generate is that it is called with iterators over
  // some other buffer type which is assignable from a 32-bit value. In this
  // case we allocate a temporary 32-bit buffer and then copy-assign back
  // to the initial inputs.
  template <typename RandomAccessIterator>
  void generate_impl(DefaultTag, RandomAccessIterator begin,
                     RandomAccessIterator, size_t n) {
    // Allocates a seed buffer of `n` elements, generates the seed, then
    // copies the result into the `out` iterator.
    absl::InlinedVector<uint32_t, 8> data(n, 0);
    generate_impl(ContiguousAndUint32Tag{}, data.begin(), data.end(), n);
    std::copy(data.begin(), data.end(), begin);
  }

  // Because [rand.req.seedseq] is not required to be copy-constructible,
  // copy-assignable nor movable, we wrap it with unique pointer to be able
  // to move SaltedSeedSeq.
  std::unique_ptr<SSeq> seq_;
};

// is_salted_seed_seq indicates whether the type is a SaltedSeedSeq.
template <typename T, typename = void>
struct is_salted_seed_seq : public std::false_type {};

template <typename T>
struct is_salted_seed_seq<
    T, typename std::enable_if<std::is_same<
           T, SaltedSeedSeq<typename T::inner_sequence_type>>::value>::type>
    : public std::true_type {};

// MakeSaltedSeedSeq returns a salted variant of the seed sequence.
// When provided with an existing SaltedSeedSeq, returns the input parameter,
// otherwise constructs a new SaltedSeedSeq which embodies the original
// non-salted seed parameters.
template <
    typename SSeq,  //
    typename EnableIf = absl::enable_if_t<is_salted_seed_seq<SSeq>::value>>
SSeq MakeSaltedSeedSeq(SSeq&& seq) {
  return SSeq(std::forward<SSeq>(seq));
}

template <
    typename SSeq,  //
    typename EnableIf = absl::enable_if_t<!is_salted_seed_seq<SSeq>::value>>
SaltedSeedSeq<typename std::decay<SSeq>::type> MakeSaltedSeedSeq(SSeq&& seq) {
  using sseq_type = typename std::decay<SSeq>::type;
  using result_type = typename sseq_type::result_type;

  absl::InlinedVector<result_type, 8> data;
  seq.param(std::back_inserter(data));
  return SaltedSeedSeq<sseq_type>(data.begin(), data.end());
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_SALTED_SEED_SEQ_H_
