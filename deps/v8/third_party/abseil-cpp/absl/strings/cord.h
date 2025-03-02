// Copyright 2020 The Abseil Authors.
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
// -----------------------------------------------------------------------------
// File: cord.h
// -----------------------------------------------------------------------------
//
// This file defines the `absl::Cord` data structure and operations on that data
// structure. A Cord is a string-like sequence of characters optimized for
// specific use cases. Unlike a `std::string`, which stores an array of
// contiguous characters, Cord data is stored in a structure consisting of
// separate, reference-counted "chunks."
//
// Because a Cord consists of these chunks, data can be added to or removed from
// a Cord during its lifetime. Chunks may also be shared between Cords. Unlike a
// `std::string`, a Cord can therefore accommodate data that changes over its
// lifetime, though it's not quite "mutable"; it can change only in the
// attachment, detachment, or rearrangement of chunks of its constituent data.
//
// A Cord provides some benefit over `std::string` under the following (albeit
// narrow) circumstances:
//
//   * Cord data is designed to grow and shrink over a Cord's lifetime. Cord
//     provides efficient insertions and deletions at the start and end of the
//     character sequences, avoiding copies in those cases. Static data should
//     generally be stored as strings.
//   * External memory consisting of string-like data can be directly added to
//     a Cord without requiring copies or allocations.
//   * Cord data may be shared and copied cheaply. Cord provides a copy-on-write
//     implementation and cheap sub-Cord operations. Copying a Cord is an O(1)
//     operation.
//
// As a consequence to the above, Cord data is generally large. Small data
// should generally use strings, as construction of a Cord requires some
// overhead. Small Cords (<= 15 bytes) are represented inline, but most small
// Cords are expected to grow over their lifetimes.
//
// Note that because a Cord is made up of separate chunked data, random access
// to character data within a Cord is slower than within a `std::string`.
//
// Thread Safety
//
// Cord has the same thread-safety properties as many other types like
// std::string, std::vector<>, int, etc -- it is thread-compatible. In
// particular, if threads do not call non-const methods, then it is safe to call
// const methods without synchronization. Copying a Cord produces a new instance
// that can be used concurrently with the original in arbitrary ways.

#ifndef ABSL_STRINGS_CORD_H_
#define ABSL_STRINGS_CORD_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/crc/internal/crc_cord_state.h"
#include "absl/functional/function_ref.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/cord_analysis.h"
#include "absl/strings/cord_buffer.h"
#include "absl/strings/internal/cord_data_edge.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_btree_reader.h"
#include "absl/strings/internal/cord_rep_crc.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cordz_info.h"
#include "absl/strings/internal/cordz_update_scope.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/strings/internal/string_constant.h"
#include "absl/strings/string_view.h"
#include "absl/types/compare.h"
#include "absl/types/optional.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
class Cord;
class CordTestPeer;
template <typename Releaser>
Cord MakeCordFromExternal(absl::string_view, Releaser&&);
void CopyCordToString(const Cord& src, absl::Nonnull<std::string*> dst);
void AppendCordToString(const Cord& src, absl::Nonnull<std::string*> dst);

// Cord memory accounting modes
enum class CordMemoryAccounting {
  // Counts the *approximate* number of bytes held in full or in part by this
  // Cord (which may not remain the same between invocations). Cords that share
  // memory could each be "charged" independently for the same shared memory.
  // See also comment on `kTotalMorePrecise` on internally shared memory.
  kTotal,

  // Counts the *approximate* number of bytes held in full or in part by this
  // Cord for the distinct memory held by this cord. This option is similar
  // to `kTotal`, except that if the cord has multiple references to the same
  // memory, that memory is only counted once.
  //
  // For example:
  //   absl::Cord cord;
  //   cord.Append(some_other_cord);
  //   cord.Append(some_other_cord);
  //   // Counts `some_other_cord` twice:
  //   cord.EstimatedMemoryUsage(kTotal);
  //   // Counts `some_other_cord` once:
  //   cord.EstimatedMemoryUsage(kTotalMorePrecise);
  //
  // The `kTotalMorePrecise` number is more expensive to compute as it requires
  // deduplicating all memory references. Applications should prefer to use
  // `kFairShare` or `kTotal` unless they really need a more precise estimate
  // on "how much memory is potentially held / kept alive by this cord?"
  kTotalMorePrecise,

  // Counts the *approximate* number of bytes held in full or in part by this
  // Cord weighted by the sharing ratio of that data. For example, if some data
  // edge is shared by 4 different Cords, then each cord is attributed 1/4th of
  // the total memory usage as a 'fair share' of the total memory usage.
  kFairShare,
};

// Cord
//
// A Cord is a sequence of characters, designed to be more efficient than a
// `std::string` in certain circumstances: namely, large string data that needs
// to change over its lifetime or shared, especially when such data is shared
// across API boundaries.
//
// A Cord stores its character data in a structure that allows efficient prepend
// and append operations. This makes a Cord useful for large string data sent
// over in a wire format that may need to be prepended or appended at some point
// during the data exchange (e.g. HTTP, protocol buffers). For example, a
// Cord is useful for storing an HTTP request, and prepending an HTTP header to
// such a request.
//
// Cords should not be used for storing general string data, however. They
// require overhead to construct and are slower than strings for random access.
//
// The Cord API provides the following common API operations:
//
// * Create or assign Cords out of existing string data, memory, or other Cords
// * Append and prepend data to an existing Cord
// * Create new Sub-Cords from existing Cord data
// * Swap Cord data and compare Cord equality
// * Write out Cord data by constructing a `std::string`
//
// Additionally, the API provides iterator utilities to iterate through Cord
// data via chunks or character bytes.
//
class Cord {
 private:
  template <typename T>
  using EnableIfString =
      absl::enable_if_t<std::is_same<T, std::string>::value, int>;

 public:
  // Cord::Cord() Constructors.

  // Creates an empty Cord.
  constexpr Cord() noexcept;

  // Creates a Cord from an existing Cord. Cord is copyable and efficiently
  // movable. The moved-from state is valid but unspecified.
  Cord(const Cord& src);
  Cord(Cord&& src) noexcept;
  Cord& operator=(const Cord& x);
  Cord& operator=(Cord&& x) noexcept;

  // Creates a Cord from a `src` string. This constructor is marked explicit to
  // prevent implicit Cord constructions from arguments convertible to an
  // `absl::string_view`.
  explicit Cord(absl::string_view src);
  Cord& operator=(absl::string_view src);

  // Creates a Cord from a `std::string&&` rvalue. These constructors are
  // templated to avoid ambiguities for types that are convertible to both
  // `absl::string_view` and `std::string`, such as `const char*`.
  template <typename T, EnableIfString<T> = 0>
  explicit Cord(T&& src);
  template <typename T, EnableIfString<T> = 0>
  Cord& operator=(T&& src);

  // Cord::~Cord()
  //
  // Destructs the Cord.
  ~Cord() {
    if (contents_.is_tree()) DestroyCordSlow();
  }

  // MakeCordFromExternal()
  //
  // Creates a Cord that takes ownership of external string memory. The
  // contents of `data` are not copied to the Cord; instead, the external
  // memory is added to the Cord and reference-counted. This data may not be
  // changed for the life of the Cord, though it may be prepended or appended
  // to.
  //
  // `MakeCordFromExternal()` takes a callable "releaser" that is invoked when
  // the reference count for `data` reaches zero. As noted above, this data must
  // remain live until the releaser is invoked. The callable releaser also must:
  //
  //   * be move constructible
  //   * support `void operator()(absl::string_view) const` or `void operator()`
  //
  // Example:
  //
  // Cord MakeCord(BlockPool* pool) {
  //   Block* block = pool->NewBlock();
  //   FillBlock(block);
  //   return absl::MakeCordFromExternal(
  //       block->ToStringView(),
  //       [pool, block](absl::string_view v) {
  //         pool->FreeBlock(block, v);
  //       });
  // }
  //
  // WARNING: Because a Cord can be reference-counted, it's likely a bug if your
  // releaser doesn't do anything. For example, consider the following:
  //
  // void Foo(const char* buffer, int len) {
  //   auto c = absl::MakeCordFromExternal(absl::string_view(buffer, len),
  //                                       [](absl::string_view) {});
  //
  //   // BUG: If Bar() copies its cord for any reason, including keeping a
  //   // substring of it, the lifetime of buffer might be extended beyond
  //   // when Foo() returns.
  //   Bar(c);
  // }
  template <typename Releaser>
  friend Cord MakeCordFromExternal(absl::string_view data, Releaser&& releaser);

  // Cord::Clear()
  //
  // Releases the Cord data. Any nodes that share data with other Cords, if
  // applicable, will have their reference counts reduced by 1.
  ABSL_ATTRIBUTE_REINITIALIZES void Clear();

  // Cord::Append()
  //
  // Appends data to the Cord, which may come from another Cord or other string
  // data.
  void Append(const Cord& src);
  void Append(Cord&& src);
  void Append(absl::string_view src);
  template <typename T, EnableIfString<T> = 0>
  void Append(T&& src);

  // Appends `buffer` to this cord, unless `buffer` has a zero length in which
  // case this method has no effect on this cord instance.
  // This method is guaranteed to consume `buffer`.
  void Append(CordBuffer buffer);

  // Returns a CordBuffer, re-using potential existing capacity in this cord.
  //
  // Cord instances may have additional unused capacity in the last (or first)
  // nodes of the underlying tree to facilitate amortized growth. This method
  // allows applications to explicitly use this spare capacity if available,
  // or create a new CordBuffer instance otherwise.
  // If this cord has a final non-shared node with at least `min_capacity`
  // available, then this method will return that buffer including its data
  // contents. I.e.; the returned buffer will have a non-zero length, and
  // a capacity of at least `buffer.length + min_capacity`. Otherwise, this
  // method will return `CordBuffer::CreateWithDefaultLimit(capacity)`.
  //
  // Below an example of using GetAppendBuffer. Notice that in this example we
  // use `GetAppendBuffer()` only on the first iteration. As we know nothing
  // about any initial extra capacity in `cord`, we may be able to use the extra
  // capacity. But as we add new buffers with fully utilized contents after that
  // we avoid calling `GetAppendBuffer()` on subsequent iterations: while this
  // works fine, it results in an unnecessary inspection of cord contents:
  //
  //   void AppendRandomDataToCord(absl::Cord &cord, size_t n) {
  //     bool first = true;
  //     while (n > 0) {
  //       CordBuffer buffer = first ? cord.GetAppendBuffer(n)
  //                                 : CordBuffer::CreateWithDefaultLimit(n);
  //       absl::Span<char> data = buffer.available_up_to(n);
  //       FillRandomValues(data.data(), data.size());
  //       buffer.IncreaseLengthBy(data.size());
  //       cord.Append(std::move(buffer));
  //       n -= data.size();
  //       first = false;
  //     }
  //   }
  CordBuffer GetAppendBuffer(size_t capacity, size_t min_capacity = 16);

  // Returns a CordBuffer, re-using potential existing capacity in this cord.
  //
  // This function is identical to `GetAppendBuffer`, except that in the case
  // where a new `CordBuffer` is allocated, it is allocated using the provided
  // custom limit instead of the default limit. `GetAppendBuffer` will default
  // to `CordBuffer::CreateWithDefaultLimit(capacity)` whereas this method
  // will default to `CordBuffer::CreateWithCustomLimit(block_size, capacity)`.
  // This method is equivalent to `GetAppendBuffer` if `block_size` is zero.
  // See the documentation for `CreateWithCustomLimit` for more details on the
  // restrictions and legal values for `block_size`.
  CordBuffer GetCustomAppendBuffer(size_t block_size, size_t capacity,
                                   size_t min_capacity = 16);

  // Cord::Prepend()
  //
  // Prepends data to the Cord, which may come from another Cord or other string
  // data.
  void Prepend(const Cord& src);
  void Prepend(absl::string_view src);
  template <typename T, EnableIfString<T> = 0>
  void Prepend(T&& src);

  // Prepends `buffer` to this cord, unless `buffer` has a zero length in which
  // case this method has no effect on this cord instance.
  // This method is guaranteed to consume `buffer`.
  void Prepend(CordBuffer buffer);

  // Cord::RemovePrefix()
  //
  // Removes the first `n` bytes of a Cord.
  void RemovePrefix(size_t n);
  void RemoveSuffix(size_t n);

  // Cord::Subcord()
  //
  // Returns a new Cord representing the subrange [pos, pos + new_size) of
  // *this. If pos >= size(), the result is empty(). If
  // (pos + new_size) >= size(), the result is the subrange [pos, size()).
  Cord Subcord(size_t pos, size_t new_size) const;

  // Cord::swap()
  //
  // Swaps the contents of the Cord with `other`.
  void swap(Cord& other) noexcept;

  // swap()
  //
  // Swaps the contents of two Cords.
  friend void swap(Cord& x, Cord& y) noexcept { x.swap(y); }

  // Cord::size()
  //
  // Returns the size of the Cord.
  size_t size() const;

  // Cord::empty()
  //
  // Determines whether the given Cord is empty, returning `true` if so.
  bool empty() const;

  // Cord::EstimatedMemoryUsage()
  //
  // Returns the *approximate* number of bytes held by this cord.
  // See CordMemoryAccounting for more information on the accounting method.
  size_t EstimatedMemoryUsage(CordMemoryAccounting accounting_method =
                                  CordMemoryAccounting::kTotal) const;

  // Cord::Compare()
  //
  // Compares 'this' Cord with rhs. This function and its relatives treat Cords
  // as sequences of unsigned bytes. The comparison is a straightforward
  // lexicographic comparison. `Cord::Compare()` returns values as follows:
  //
  //   -1  'this' Cord is smaller
  //    0  two Cords are equal
  //    1  'this' Cord is larger
  int Compare(absl::string_view rhs) const;
  int Compare(const Cord& rhs) const;

  // Cord::StartsWith()
  //
  // Determines whether the Cord starts with the passed string data `rhs`.
  bool StartsWith(const Cord& rhs) const;
  bool StartsWith(absl::string_view rhs) const;

  // Cord::EndsWith()
  //
  // Determines whether the Cord ends with the passed string data `rhs`.
  bool EndsWith(absl::string_view rhs) const;
  bool EndsWith(const Cord& rhs) const;

  // Cord::Contains()
  //
  // Determines whether the Cord contains the passed string data `rhs`.
  bool Contains(absl::string_view rhs) const;
  bool Contains(const Cord& rhs) const;

  // Cord::operator std::string()
  //
  // Converts a Cord into a `std::string()`. This operator is marked explicit to
  // prevent unintended Cord usage in functions that take a string.
  explicit operator std::string() const;

  // CopyCordToString()
  //
  // Copies the contents of a `src` Cord into a `*dst` string.
  //
  // This function optimizes the case of reusing the destination string since it
  // can reuse previously allocated capacity. However, this function does not
  // guarantee that pointers previously returned by `dst->data()` remain valid
  // even if `*dst` had enough capacity to hold `src`. If `*dst` is a new
  // object, prefer to simply use the conversion operator to `std::string`.
  friend void CopyCordToString(const Cord& src,
                               absl::Nonnull<std::string*> dst);

  // AppendCordToString()
  //
  // Appends the contents of a `src` Cord to a `*dst` string.
  //
  // This function optimizes the case of appending to a non-empty destination
  // string. If `*dst` already has capacity to store the contents of the cord,
  // this function does not invalidate pointers previously returned by
  // `dst->data()`. If `*dst` is a new object, prefer to simply use the
  // conversion operator to `std::string`.
  friend void AppendCordToString(const Cord& src,
                                 absl::Nonnull<std::string*> dst);

  class CharIterator;

  //----------------------------------------------------------------------------
  // Cord::ChunkIterator
  //----------------------------------------------------------------------------
  //
  // A `Cord::ChunkIterator` allows iteration over the constituent chunks of its
  // Cord. Such iteration allows you to perform non-const operations on the data
  // of a Cord without modifying it.
  //
  // Generally, you do not instantiate a `Cord::ChunkIterator` directly;
  // instead, you create one implicitly through use of the `Cord::Chunks()`
  // member function.
  //
  // The `Cord::ChunkIterator` has the following properties:
  //
  //   * The iterator is invalidated after any non-const operation on the
  //     Cord object over which it iterates.
  //   * The `string_view` returned by dereferencing a valid, non-`end()`
  //     iterator is guaranteed to be non-empty.
  //   * Two `ChunkIterator` objects can be compared equal if and only if they
  //     remain valid and iterate over the same Cord.
  //   * The iterator in this case is a proxy iterator; the `string_view`
  //     returned by the iterator does not live inside the Cord, and its
  //     lifetime is limited to the lifetime of the iterator itself. To help
  //     prevent lifetime issues, `ChunkIterator::reference` is not a true
  //     reference type and is equivalent to `value_type`.
  //   * The iterator keeps state that can grow for Cords that contain many
  //     nodes and are imbalanced due to sharing. Prefer to pass this type by
  //     const reference instead of by value.
  class ChunkIterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = absl::string_view;
    using difference_type = ptrdiff_t;
    using pointer = absl::Nonnull<const value_type*>;
    using reference = value_type;

    ChunkIterator() = default;

    ChunkIterator& operator++();
    ChunkIterator operator++(int);
    bool operator==(const ChunkIterator& other) const;
    bool operator!=(const ChunkIterator& other) const;
    reference operator*() const;
    pointer operator->() const;

    friend class Cord;
    friend class CharIterator;

   private:
    using CordRep = absl::cord_internal::CordRep;
    using CordRepBtree = absl::cord_internal::CordRepBtree;
    using CordRepBtreeReader = absl::cord_internal::CordRepBtreeReader;

    // Constructs a `begin()` iterator from `tree`.
    explicit ChunkIterator(absl::Nonnull<cord_internal::CordRep*> tree);

    // Constructs a `begin()` iterator from `cord`.
    explicit ChunkIterator(absl::Nonnull<const Cord*> cord);

    // Initializes this instance from a tree. Invoked by constructors.
    void InitTree(absl::Nonnull<cord_internal::CordRep*> tree);

    // Removes `n` bytes from `current_chunk_`. Expects `n` to be smaller than
    // `current_chunk_.size()`.
    void RemoveChunkPrefix(size_t n);
    Cord AdvanceAndReadBytes(size_t n);
    void AdvanceBytes(size_t n);

    // Btree specific operator++
    ChunkIterator& AdvanceBtree();
    void AdvanceBytesBtree(size_t n);

    // A view into bytes of the current `CordRep`. It may only be a view to a
    // suffix of bytes if this is being used by `CharIterator`.
    absl::string_view current_chunk_;
    // The current leaf, or `nullptr` if the iterator points to short data.
    // If the current chunk is a substring node, current_leaf_ points to the
    // underlying flat or external node.
    absl::Nullable<absl::cord_internal::CordRep*> current_leaf_ = nullptr;
    // The number of bytes left in the `Cord` over which we are iterating.
    size_t bytes_remaining_ = 0;

    // Cord reader for cord btrees. Empty if not traversing a btree.
    CordRepBtreeReader btree_reader_;
  };

  // Cord::chunk_begin()
  //
  // Returns an iterator to the first chunk of the `Cord`.
  //
  // Generally, prefer using `Cord::Chunks()` within a range-based for loop for
  // iterating over the chunks of a Cord. This method may be useful for getting
  // a `ChunkIterator` where range-based for-loops are not useful.
  //
  // Example:
  //
  //   absl::Cord::ChunkIterator FindAsChunk(const absl::Cord& c,
  //                                         absl::string_view s) {
  //     return std::find(c.chunk_begin(), c.chunk_end(), s);
  //   }
  ChunkIterator chunk_begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Cord::chunk_end()
  //
  // Returns an iterator one increment past the last chunk of the `Cord`.
  //
  // Generally, prefer using `Cord::Chunks()` within a range-based for loop for
  // iterating over the chunks of a Cord. This method may be useful for getting
  // a `ChunkIterator` where range-based for-loops may not be available.
  ChunkIterator chunk_end() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  //----------------------------------------------------------------------------
  // Cord::ChunkRange
  //----------------------------------------------------------------------------
  //
  // `ChunkRange` is a helper class for iterating over the chunks of the `Cord`,
  // producing an iterator which can be used within a range-based for loop.
  // Construction of a `ChunkRange` will return an iterator pointing to the
  // first chunk of the Cord. Generally, do not construct a `ChunkRange`
  // directly; instead, prefer to use the `Cord::Chunks()` method.
  //
  // Implementation note: `ChunkRange` is simply a convenience wrapper over
  // `Cord::chunk_begin()` and `Cord::chunk_end()`.
  class ChunkRange {
   public:
    // Fulfill minimum c++ container requirements [container.requirements]
    // These (partial) container type definitions allow ChunkRange to be used
    // in various utilities expecting a subset of [container.requirements].
    // For example, the below enables using `::testing::ElementsAre(...)`
    using value_type = absl::string_view;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = ChunkIterator;
    using const_iterator = ChunkIterator;

    explicit ChunkRange(absl::Nonnull<const Cord*> cord) : cord_(cord) {}

    ChunkIterator begin() const;
    ChunkIterator end() const;

   private:
    absl::Nonnull<const Cord*> cord_;
  };

  // Cord::Chunks()
  //
  // Returns a `Cord::ChunkRange` for iterating over the chunks of a `Cord` with
  // a range-based for-loop. For most iteration tasks on a Cord, use
  // `Cord::Chunks()` to retrieve this iterator.
  //
  // Example:
  //
  //   void ProcessChunks(const Cord& cord) {
  //     for (absl::string_view chunk : cord.Chunks()) { ... }
  //   }
  //
  // Note that the ordinary caveats of temporary lifetime extension apply:
  //
  //   void Process() {
  //     for (absl::string_view chunk : CordFactory().Chunks()) {
  //       // The temporary Cord returned by CordFactory has been destroyed!
  //     }
  //   }
  ChunkRange Chunks() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  //----------------------------------------------------------------------------
  // Cord::CharIterator
  //----------------------------------------------------------------------------
  //
  // A `Cord::CharIterator` allows iteration over the constituent characters of
  // a `Cord`.
  //
  // Generally, you do not instantiate a `Cord::CharIterator` directly; instead,
  // you create one implicitly through use of the `Cord::Chars()` member
  // function.
  //
  // A `Cord::CharIterator` has the following properties:
  //
  //   * The iterator is invalidated after any non-const operation on the
  //     Cord object over which it iterates.
  //   * Two `CharIterator` objects can be compared equal if and only if they
  //     remain valid and iterate over the same Cord.
  //   * The iterator keeps state that can grow for Cords that contain many
  //     nodes and are imbalanced due to sharing. Prefer to pass this type by
  //     const reference instead of by value.
  //   * This type cannot act as a forward iterator because a `Cord` can reuse
  //     sections of memory. This fact violates the requirement for forward
  //     iterators to compare equal if dereferencing them returns the same
  //     object.
  class CharIterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = char;
    using difference_type = ptrdiff_t;
    using pointer = absl::Nonnull<const char*>;
    using reference = const char&;

    CharIterator() = default;

    CharIterator& operator++();
    CharIterator operator++(int);
    bool operator==(const CharIterator& other) const;
    bool operator!=(const CharIterator& other) const;
    reference operator*() const;

    friend Cord;

   private:
    explicit CharIterator(absl::Nonnull<const Cord*> cord)
        : chunk_iterator_(cord) {}

    ChunkIterator chunk_iterator_;
  };

  // Cord::AdvanceAndRead()
  //
  // Advances the `Cord::CharIterator` by `n_bytes` and returns the bytes
  // advanced as a separate `Cord`. `n_bytes` must be less than or equal to the
  // number of bytes within the Cord; otherwise, behavior is undefined. It is
  // valid to pass `char_end()` and `0`.
  static Cord AdvanceAndRead(absl::Nonnull<CharIterator*> it, size_t n_bytes);

  // Cord::Advance()
  //
  // Advances the `Cord::CharIterator` by `n_bytes`. `n_bytes` must be less than
  // or equal to the number of bytes remaining within the Cord; otherwise,
  // behavior is undefined. It is valid to pass `char_end()` and `0`.
  static void Advance(absl::Nonnull<CharIterator*> it, size_t n_bytes);

  // Cord::ChunkRemaining()
  //
  // Returns the longest contiguous view starting at the iterator's position.
  //
  // `it` must be dereferenceable.
  static absl::string_view ChunkRemaining(const CharIterator& it);

  // Cord::char_begin()
  //
  // Returns an iterator to the first character of the `Cord`.
  //
  // Generally, prefer using `Cord::Chars()` within a range-based for loop for
  // iterating over the chunks of a Cord. This method may be useful for getting
  // a `CharIterator` where range-based for-loops may not be available.
  CharIterator char_begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Cord::char_end()
  //
  // Returns an iterator to one past the last character of the `Cord`.
  //
  // Generally, prefer using `Cord::Chars()` within a range-based for loop for
  // iterating over the chunks of a Cord. This method may be useful for getting
  // a `CharIterator` where range-based for-loops are not useful.
  CharIterator char_end() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Cord::CharRange
  //
  // `CharRange` is a helper class for iterating over the characters of a
  // producing an iterator which can be used within a range-based for loop.
  // Construction of a `CharRange` will return an iterator pointing to the first
  // character of the Cord. Generally, do not construct a `CharRange` directly;
  // instead, prefer to use the `Cord::Chars()` method shown below.
  //
  // Implementation note: `CharRange` is simply a convenience wrapper over
  // `Cord::char_begin()` and `Cord::char_end()`.
  class CharRange {
   public:
    // Fulfill minimum c++ container requirements [container.requirements]
    // These (partial) container type definitions allow CharRange to be used
    // in various utilities expecting a subset of [container.requirements].
    // For example, the below enables using `::testing::ElementsAre(...)`
    using value_type = char;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = CharIterator;
    using const_iterator = CharIterator;

    explicit CharRange(absl::Nonnull<const Cord*> cord) : cord_(cord) {}

    CharIterator begin() const;
    CharIterator end() const;

   private:
    absl::Nonnull<const Cord*> cord_;
  };

  // Cord::Chars()
  //
  // Returns a `Cord::CharRange` for iterating over the characters of a `Cord`
  // with a range-based for-loop. For most character-based iteration tasks on a
  // Cord, use `Cord::Chars()` to retrieve this iterator.
  //
  // Example:
  //
  //   void ProcessCord(const Cord& cord) {
  //     for (char c : cord.Chars()) { ... }
  //   }
  //
  // Note that the ordinary caveats of temporary lifetime extension apply:
  //
  //   void Process() {
  //     for (char c : CordFactory().Chars()) {
  //       // The temporary Cord returned by CordFactory has been destroyed!
  //     }
  //   }
  CharRange Chars() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Cord::operator[]
  //
  // Gets the "i"th character of the Cord and returns it, provided that
  // 0 <= i < Cord.size().
  //
  // NOTE: This routine is reasonably efficient. It is roughly
  // logarithmic based on the number of chunks that make up the cord. Still,
  // if you need to iterate over the contents of a cord, you should
  // use a CharIterator/ChunkIterator rather than call operator[] or Get()
  // repeatedly in a loop.
  char operator[](size_t i) const;

  // Cord::TryFlat()
  //
  // If this cord's representation is a single flat array, returns a
  // string_view referencing that array.  Otherwise returns nullopt.
  absl::optional<absl::string_view> TryFlat() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Cord::Flatten()
  //
  // Flattens the cord into a single array and returns a view of the data.
  //
  // If the cord was already flat, the contents are not modified.
  absl::string_view Flatten() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Cord::Find()
  //
  // Returns an iterator to the first occurrence of the substring `needle`.
  //
  // If the substring `needle` does not occur, `Cord::char_end()` is returned.
  CharIterator Find(absl::string_view needle) const;
  CharIterator Find(const absl::Cord& needle) const;

  // Supports absl::Cord as a sink object for absl::Format().
  friend void AbslFormatFlush(absl::Nonnull<absl::Cord*> cord,
                              absl::string_view part) {
    cord->Append(part);
  }

  // Support automatic stringification with absl::StrCat and absl::StrFormat.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const absl::Cord& cord) {
    for (absl::string_view chunk : cord.Chunks()) {
      sink.Append(chunk);
    }
  }

  // Cord::SetExpectedChecksum()
  //
  // Stores a checksum value with this non-empty cord instance, for later
  // retrieval.
  //
  // The expected checksum is a number stored out-of-band, alongside the data.
  // It is preserved across copies and assignments, but any mutations to a cord
  // will cause it to lose its expected checksum.
  //
  // The expected checksum is not part of a Cord's value, and does not affect
  // operations such as equality or hashing.
  //
  // This field is intended to store a CRC32C checksum for later validation, to
  // help support end-to-end checksum workflows.  However, the Cord API itself
  // does no CRC validation, and assigns no meaning to this number.
  //
  // This call has no effect if this cord is empty.
  void SetExpectedChecksum(uint32_t crc);

  // Returns this cord's expected checksum, if it has one.  Otherwise, returns
  // nullopt.
  absl::optional<uint32_t> ExpectedChecksum() const;

  template <typename H>
  friend H AbslHashValue(H hash_state, const absl::Cord& c) {
    absl::optional<absl::string_view> maybe_flat = c.TryFlat();
    if (maybe_flat.has_value()) {
      return H::combine(std::move(hash_state), *maybe_flat);
    }
    return c.HashFragmented(std::move(hash_state));
  }

  // Create a Cord with the contents of StringConstant<T>::value.
  // No allocations will be done and no data will be copied.
  // This is an INTERNAL API and subject to change or removal. This API can only
  // be used by spelling absl::strings_internal::MakeStringConstant, which is
  // also an internal API.
  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr Cord(strings_internal::StringConstant<T>);

 private:
  using CordRep = absl::cord_internal::CordRep;
  using CordRepFlat = absl::cord_internal::CordRepFlat;
  using CordzInfo = cord_internal::CordzInfo;
  using CordzUpdateScope = cord_internal::CordzUpdateScope;
  using CordzUpdateTracker = cord_internal::CordzUpdateTracker;
  using InlineData = cord_internal::InlineData;
  using MethodIdentifier = CordzUpdateTracker::MethodIdentifier;

  // Creates a cord instance with `method` representing the originating
  // public API call causing the cord to be created.
  explicit Cord(absl::string_view src, MethodIdentifier method);

  friend class CordTestPeer;
  friend bool operator==(const Cord& lhs, const Cord& rhs);
  friend bool operator==(const Cord& lhs, absl::string_view rhs);

#ifdef __cpp_impl_three_way_comparison

  // Cords support comparison with other Cords and string_views via operator<
  // and others; here we provide a wrapper for the C++20 three-way comparison
  // <=> operator.

  static inline std::strong_ordering ConvertCompareResultToStrongOrdering(
      int c) {
    if (c == 0) {
      return std::strong_ordering::equal;
    } else if (c < 0) {
      return std::strong_ordering::less;
    } else {
      return std::strong_ordering::greater;
    }
  }

  friend inline std::strong_ordering operator<=>(const Cord& x, const Cord& y) {
    return ConvertCompareResultToStrongOrdering(x.Compare(y));
  }

  friend inline std::strong_ordering operator<=>(const Cord& lhs,
                                                 absl::string_view rhs) {
    return ConvertCompareResultToStrongOrdering(lhs.Compare(rhs));
  }

  friend inline std::strong_ordering operator<=>(absl::string_view lhs,
                                                 const Cord& rhs) {
    return ConvertCompareResultToStrongOrdering(-rhs.Compare(lhs));
  }
#endif

  friend absl::Nullable<const CordzInfo*> GetCordzInfoForTesting(
      const Cord& cord);

  // Calls the provided function once for each cord chunk, in order.  Unlike
  // Chunks(), this API will not allocate memory.
  void ForEachChunk(absl::FunctionRef<void(absl::string_view)>) const;

  // Allocates new contiguous storage for the contents of the cord. This is
  // called by Flatten() when the cord was not already flat.
  absl::string_view FlattenSlowPath();

  // Actual cord contents are hidden inside the following simple
  // class so that we can isolate the bulk of cord.cc from changes
  // to the representation.
  //
  // InlineRep holds either a tree pointer, or an array of kMaxInline bytes.
  class InlineRep {
   public:
    static constexpr unsigned char kMaxInline = cord_internal::kMaxInline;
    static_assert(kMaxInline >= sizeof(absl::cord_internal::CordRep*), "");

    constexpr InlineRep() : data_() {}
    explicit InlineRep(InlineData::DefaultInitType init) : data_(init) {}
    InlineRep(const InlineRep& src);
    InlineRep(InlineRep&& src);
    InlineRep& operator=(const InlineRep& src);
    InlineRep& operator=(InlineRep&& src) noexcept;

    explicit constexpr InlineRep(absl::string_view sv,
                                 absl::Nullable<CordRep*> rep);

    void Swap(absl::Nonnull<InlineRep*> rhs);
    size_t size() const;
    // Returns nullptr if holding pointer
    absl::Nullable<const char*> data() const;
    // Discards pointer, if any
    void set_data(absl::Nonnull<const char*> data, size_t n);
    absl::Nonnull<char*> set_data(size_t n);  // Write data to the result
    // Returns nullptr if holding bytes
    absl::Nullable<absl::cord_internal::CordRep*> tree() const;
    absl::Nonnull<absl::cord_internal::CordRep*> as_tree() const;
    absl::Nonnull<const char*> as_chars() const;
    // Returns non-null iff was holding a pointer
    absl::Nullable<absl::cord_internal::CordRep*> clear();
    // Converts to pointer if necessary.
    void reduce_size(size_t n);    // REQUIRES: holding data
    void remove_prefix(size_t n);  // REQUIRES: holding data
    void AppendArray(absl::string_view src, MethodIdentifier method);
    absl::string_view FindFlatStartPiece() const;

    // Creates a CordRepFlat instance from the current inlined data with `extra'
    // bytes of desired additional capacity.
    absl::Nonnull<CordRepFlat*> MakeFlatWithExtraCapacity(size_t extra);

    // Sets the tree value for this instance. `rep` must not be null.
    // Requires the current instance to hold a tree, and a lock to be held on
    // any CordzInfo referenced by this instance. The latter is enforced through
    // the CordzUpdateScope argument. If the current instance is sampled, then
    // the CordzInfo instance is updated to reference the new `rep` value.
    void SetTree(absl::Nonnull<CordRep*> rep, const CordzUpdateScope& scope);

    // Identical to SetTree(), except that `rep` is allowed to be null, in
    // which case the current instance is reset to an empty value.
    void SetTreeOrEmpty(absl::Nullable<CordRep*> rep,
                        const CordzUpdateScope& scope);

    // Sets the tree value for this instance, and randomly samples this cord.
    // This function disregards existing contents in `data_`, and should be
    // called when a Cord is 'promoted' from an 'uninitialized' or 'inlined'
    // value to a non-inlined (tree / ring) value.
    void EmplaceTree(absl::Nonnull<CordRep*> rep, MethodIdentifier method);

    // Identical to EmplaceTree, except that it copies the parent stack from
    // the provided `parent` data if the parent is sampled.
    void EmplaceTree(absl::Nonnull<CordRep*> rep, const InlineData& parent,
                     MethodIdentifier method);

    // Commits the change of a newly created, or updated `rep` root value into
    // this cord. `old_rep` indicates the old (inlined or tree) value of the
    // cord, and determines if the commit invokes SetTree() or EmplaceTree().
    void CommitTree(absl::Nullable<const CordRep*> old_rep,
                    absl::Nonnull<CordRep*> rep, const CordzUpdateScope& scope,
                    MethodIdentifier method);

    void AppendTreeToInlined(absl::Nonnull<CordRep*> tree,
                             MethodIdentifier method);
    void AppendTreeToTree(absl::Nonnull<CordRep*> tree,
                          MethodIdentifier method);
    void AppendTree(absl::Nonnull<CordRep*> tree, MethodIdentifier method);
    void PrependTreeToInlined(absl::Nonnull<CordRep*> tree,
                              MethodIdentifier method);
    void PrependTreeToTree(absl::Nonnull<CordRep*> tree,
                           MethodIdentifier method);
    void PrependTree(absl::Nonnull<CordRep*> tree, MethodIdentifier method);

    bool IsSame(const InlineRep& other) const { return data_ == other.data_; }

    // Copies the inline contents into `dst`. Assumes the cord is not empty.
    void CopyTo(absl::Nonnull<std::string*> dst) const {
      data_.CopyInlineToString(dst);
    }

    // Copies the inline contents into `dst`. Assumes the cord is not empty.
    void CopyToArray(absl::Nonnull<char*> dst) const;

    bool is_tree() const { return data_.is_tree(); }

    // Returns true if the Cord is being profiled by cordz.
    bool is_profiled() const { return data_.is_tree() && data_.is_profiled(); }

    // Returns the available inlined capacity, or 0 if is_tree() == true.
    size_t remaining_inline_capacity() const {
      return data_.is_tree() ? 0 : kMaxInline - data_.inline_size();
    }

    // Returns the profiled CordzInfo, or nullptr if not sampled.
    absl::Nullable<absl::cord_internal::CordzInfo*> cordz_info() const {
      return data_.cordz_info();
    }

    // Sets the profiled CordzInfo.
    void set_cordz_info(absl::Nonnull<cord_internal::CordzInfo*> cordz_info) {
      assert(cordz_info != nullptr);
      data_.set_cordz_info(cordz_info);
    }

    // Resets the current cordz_info to null / empty.
    void clear_cordz_info() { data_.clear_cordz_info(); }

   private:
    friend class Cord;

    void AssignSlow(const InlineRep& src);
    // Unrefs the tree and stops profiling.
    void UnrefTree();

    void ResetToEmpty() { data_ = {}; }

    void set_inline_size(size_t size) { data_.set_inline_size(size); }
    size_t inline_size() const { return data_.inline_size(); }

    // Empty cords that carry a checksum have a CordRepCrc node with a null
    // child node. The code can avoid lots of special cases where it would
    // otherwise transition from tree to inline storage if we just remove the
    // CordRepCrc node before mutations. Must never be called inside a
    // CordzUpdateScope since it untracks the cordz info.
    void MaybeRemoveEmptyCrcNode();

    cord_internal::InlineData data_;
  };
  InlineRep contents_;

  // Helper for GetFlat() and TryFlat().
  static bool GetFlatAux(absl::Nonnull<absl::cord_internal::CordRep*> rep,
                         absl::Nonnull<absl::string_view*> fragment);

  // Helper for ForEachChunk().
  static void ForEachChunkAux(
      absl::Nonnull<absl::cord_internal::CordRep*> rep,
      absl::FunctionRef<void(absl::string_view)> callback);

  // The destructor for non-empty Cords.
  void DestroyCordSlow();

  // Out-of-line implementation of slower parts of logic.
  void CopyToArraySlowPath(absl::Nonnull<char*> dst) const;
  int CompareSlowPath(absl::string_view rhs, size_t compared_size,
                      size_t size_to_compare) const;
  int CompareSlowPath(const Cord& rhs, size_t compared_size,
                      size_t size_to_compare) const;
  bool EqualsImpl(absl::string_view rhs, size_t size_to_compare) const;
  bool EqualsImpl(const Cord& rhs, size_t size_to_compare) const;
  int CompareImpl(const Cord& rhs) const;

  template <typename ResultType, typename RHS>
  friend ResultType GenericCompare(const Cord& lhs, const RHS& rhs,
                                   size_t size_to_compare);
  static absl::string_view GetFirstChunk(const Cord& c);
  static absl::string_view GetFirstChunk(absl::string_view sv);

  // Returns a new reference to contents_.tree(), or steals an existing
  // reference if called on an rvalue.
  absl::Nonnull<absl::cord_internal::CordRep*> TakeRep() const&;
  absl::Nonnull<absl::cord_internal::CordRep*> TakeRep() &&;

  // Helper for Append().
  template <typename C>
  void AppendImpl(C&& src);

  // Appends / Prepends `src` to this instance, using precise sizing.
  // This method does explicitly not attempt to use any spare capacity
  // in any pending last added private owned flat.
  // Requires `src` to be <= kMaxFlatLength.
  void AppendPrecise(absl::string_view src, MethodIdentifier method);
  void PrependPrecise(absl::string_view src, MethodIdentifier method);

  CordBuffer GetAppendBufferSlowPath(size_t block_size, size_t capacity,
                                     size_t min_capacity);

  // Prepends the provided data to this instance. `method` contains the public
  // API method for this action which is tracked for Cordz sampling purposes.
  void PrependArray(absl::string_view src, MethodIdentifier method);

  // Assigns the value in 'src' to this instance, 'stealing' its contents.
  // Requires src.length() > kMaxBytesToCopy.
  Cord& AssignLargeString(std::string&& src);

  // Helper for AbslHashValue().
  template <typename H>
  H HashFragmented(H hash_state) const {
    typename H::AbslInternalPiecewiseCombiner combiner;
    ForEachChunk([&combiner, &hash_state](absl::string_view chunk) {
      hash_state = combiner.add_buffer(std::move(hash_state), chunk.data(),
                                       chunk.size());
    });
    return H::combine(combiner.finalize(std::move(hash_state)), size());
  }

  friend class CrcCord;
  void SetCrcCordState(crc_internal::CrcCordState state);
  absl::Nullable<const crc_internal::CrcCordState*> MaybeGetCrcCordState()
      const;

  CharIterator FindImpl(CharIterator it, absl::string_view needle) const;

  void CopyToArrayImpl(absl::Nonnull<char*> dst) const;
};

ABSL_NAMESPACE_END
}  // namespace absl

namespace absl {
ABSL_NAMESPACE_BEGIN

// allow a Cord to be logged
extern std::ostream& operator<<(std::ostream& out, const Cord& cord);

// ------------------------------------------------------------------
// Internal details follow.  Clients should ignore.

namespace cord_internal {

// Does non-template-specific `CordRepExternal` initialization.
// Requires `data` to be non-empty.
void InitializeCordRepExternal(absl::string_view data,
                               absl::Nonnull<CordRepExternal*> rep);

// Creates a new `CordRep` that owns `data` and `releaser` and returns a pointer
// to it. Requires `data` to be non-empty.
template <typename Releaser>
// NOLINTNEXTLINE - suppress clang-tidy raw pointer return.
absl::Nonnull<CordRep*> NewExternalRep(absl::string_view data,
                                       Releaser&& releaser) {
  assert(!data.empty());
  using ReleaserType = absl::decay_t<Releaser>;
  CordRepExternal* rep = new CordRepExternalImpl<ReleaserType>(
      std::forward<Releaser>(releaser), 0);
  InitializeCordRepExternal(data, rep);
  return rep;
}

// Overload for function reference types that dispatches using a function
// pointer because there are no `alignof()` or `sizeof()` a function reference.
// NOLINTNEXTLINE - suppress clang-tidy raw pointer return.
inline absl::Nonnull<CordRep*> NewExternalRep(
    absl::string_view data, void (&releaser)(absl::string_view)) {
  return NewExternalRep(data, &releaser);
}

}  // namespace cord_internal

template <typename Releaser>
Cord MakeCordFromExternal(absl::string_view data, Releaser&& releaser) {
  Cord cord;
  if (ABSL_PREDICT_TRUE(!data.empty())) {
    cord.contents_.EmplaceTree(::absl::cord_internal::NewExternalRep(
                                   data, std::forward<Releaser>(releaser)),
                               Cord::MethodIdentifier::kMakeCordFromExternal);
  } else {
    using ReleaserType = absl::decay_t<Releaser>;
    cord_internal::InvokeReleaser(
        cord_internal::Rank1{}, ReleaserType(std::forward<Releaser>(releaser)),
        data);
  }
  return cord;
}

constexpr Cord::InlineRep::InlineRep(absl::string_view sv,
                                     absl::Nullable<CordRep*> rep)
    : data_(sv, rep) {}

inline Cord::InlineRep::InlineRep(const Cord::InlineRep& src)
    : data_(InlineData::kDefaultInit) {
  if (CordRep* tree = src.tree()) {
    EmplaceTree(CordRep::Ref(tree), src.data_,
                CordzUpdateTracker::kConstructorCord);
  } else {
    data_ = src.data_;
  }
}

inline Cord::InlineRep::InlineRep(Cord::InlineRep&& src) : data_(src.data_) {
  src.ResetToEmpty();
}

inline Cord::InlineRep& Cord::InlineRep::operator=(const Cord::InlineRep& src) {
  if (this == &src) {
    return *this;
  }
  if (!is_tree() && !src.is_tree()) {
    data_ = src.data_;
    return *this;
  }
  AssignSlow(src);
  return *this;
}

inline Cord::InlineRep& Cord::InlineRep::operator=(
    Cord::InlineRep&& src) noexcept {
  if (is_tree()) {
    UnrefTree();
  }
  data_ = src.data_;
  src.ResetToEmpty();
  return *this;
}

inline void Cord::InlineRep::Swap(absl::Nonnull<Cord::InlineRep*> rhs) {
  if (rhs == this) {
    return;
  }
  using std::swap;
  swap(data_, rhs->data_);
}

inline absl::Nullable<const char*> Cord::InlineRep::data() const {
  return is_tree() ? nullptr : data_.as_chars();
}

inline absl::Nonnull<const char*> Cord::InlineRep::as_chars() const {
  assert(!data_.is_tree());
  return data_.as_chars();
}

inline absl::Nonnull<absl::cord_internal::CordRep*> Cord::InlineRep::as_tree()
    const {
  assert(data_.is_tree());
  return data_.as_tree();
}

inline absl::Nullable<absl::cord_internal::CordRep*> Cord::InlineRep::tree()
    const {
  if (is_tree()) {
    return as_tree();
  } else {
    return nullptr;
  }
}

inline size_t Cord::InlineRep::size() const {
  return is_tree() ? as_tree()->length : inline_size();
}

inline absl::Nonnull<cord_internal::CordRepFlat*>
Cord::InlineRep::MakeFlatWithExtraCapacity(size_t extra) {
  static_assert(cord_internal::kMinFlatLength >= sizeof(data_), "");
  size_t len = data_.inline_size();
  auto* result = CordRepFlat::New(len + extra);
  result->length = len;
  data_.copy_max_inline_to(result->Data());
  return result;
}

inline void Cord::InlineRep::EmplaceTree(absl::Nonnull<CordRep*> rep,
                                         MethodIdentifier method) {
  assert(rep);
  data_.make_tree(rep);
  CordzInfo::MaybeTrackCord(data_, method);
}

inline void Cord::InlineRep::EmplaceTree(absl::Nonnull<CordRep*> rep,
                                         const InlineData& parent,
                                         MethodIdentifier method) {
  data_.make_tree(rep);
  CordzInfo::MaybeTrackCord(data_, parent, method);
}

inline void Cord::InlineRep::SetTree(absl::Nonnull<CordRep*> rep,
                                     const CordzUpdateScope& scope) {
  assert(rep);
  assert(data_.is_tree());
  data_.set_tree(rep);
  scope.SetCordRep(rep);
}

inline void Cord::InlineRep::SetTreeOrEmpty(absl::Nullable<CordRep*> rep,
                                            const CordzUpdateScope& scope) {
  assert(data_.is_tree());
  if (rep) {
    data_.set_tree(rep);
  } else {
    data_ = {};
  }
  scope.SetCordRep(rep);
}

inline void Cord::InlineRep::CommitTree(absl::Nullable<const CordRep*> old_rep,
                                        absl::Nonnull<CordRep*> rep,
                                        const CordzUpdateScope& scope,
                                        MethodIdentifier method) {
  if (old_rep) {
    SetTree(rep, scope);
  } else {
    EmplaceTree(rep, method);
  }
}

inline absl::Nullable<absl::cord_internal::CordRep*> Cord::InlineRep::clear() {
  if (is_tree()) {
    CordzInfo::MaybeUntrackCord(cordz_info());
  }
  absl::cord_internal::CordRep* result = tree();
  ResetToEmpty();
  return result;
}

inline void Cord::InlineRep::CopyToArray(absl::Nonnull<char*> dst) const {
  assert(!is_tree());
  size_t n = inline_size();
  assert(n != 0);
  cord_internal::SmallMemmove(dst, data_.as_chars(), n);
}

inline void Cord::InlineRep::MaybeRemoveEmptyCrcNode() {
  CordRep* rep = tree();
  if (rep == nullptr || ABSL_PREDICT_TRUE(rep->length > 0)) {
    return;
  }
  assert(rep->IsCrc());
  assert(rep->crc()->child == nullptr);
  CordzInfo::MaybeUntrackCord(cordz_info());
  CordRep::Unref(rep);
  ResetToEmpty();
}

constexpr inline Cord::Cord() noexcept {}

inline Cord::Cord(absl::string_view src)
    : Cord(src, CordzUpdateTracker::kConstructorString) {}

template <typename T>
constexpr Cord::Cord(strings_internal::StringConstant<T>)
    : contents_(strings_internal::StringConstant<T>::value,
                strings_internal::StringConstant<T>::value.size() <=
                        cord_internal::kMaxInline
                    ? nullptr
                    : &cord_internal::ConstInitExternalStorage<
                          strings_internal::StringConstant<T>>::value) {}

inline Cord& Cord::operator=(const Cord& x) {
  contents_ = x.contents_;
  return *this;
}

template <typename T, Cord::EnableIfString<T>>
Cord& Cord::operator=(T&& src) {
  if (src.size() <= cord_internal::kMaxBytesToCopy) {
    return operator=(absl::string_view(src));
  } else {
    return AssignLargeString(std::forward<T>(src));
  }
}

inline Cord::Cord(const Cord& src) : contents_(src.contents_) {}

inline Cord::Cord(Cord&& src) noexcept : contents_(std::move(src.contents_)) {}

inline void Cord::swap(Cord& other) noexcept {
  contents_.Swap(&other.contents_);
}

inline Cord& Cord::operator=(Cord&& x) noexcept {
  contents_ = std::move(x.contents_);
  return *this;
}

extern template Cord::Cord(std::string&& src);

inline size_t Cord::size() const {
  // Length is 1st field in str.rep_
  return contents_.size();
}

inline bool Cord::empty() const { return size() == 0; }

inline size_t Cord::EstimatedMemoryUsage(
    CordMemoryAccounting accounting_method) const {
  size_t result = sizeof(Cord);
  if (const absl::cord_internal::CordRep* rep = contents_.tree()) {
    switch (accounting_method) {
      case CordMemoryAccounting::kFairShare:
        result += cord_internal::GetEstimatedFairShareMemoryUsage(rep);
        break;
      case CordMemoryAccounting::kTotalMorePrecise:
        result += cord_internal::GetMorePreciseMemoryUsage(rep);
        break;
      case CordMemoryAccounting::kTotal:
        result += cord_internal::GetEstimatedMemoryUsage(rep);
        break;
    }
  }
  return result;
}

inline absl::optional<absl::string_view> Cord::TryFlat() const
    ABSL_ATTRIBUTE_LIFETIME_BOUND {
  absl::cord_internal::CordRep* rep = contents_.tree();
  if (rep == nullptr) {
    return absl::string_view(contents_.data(), contents_.size());
  }
  absl::string_view fragment;
  if (GetFlatAux(rep, &fragment)) {
    return fragment;
  }
  return absl::nullopt;
}

inline absl::string_view Cord::Flatten() ABSL_ATTRIBUTE_LIFETIME_BOUND {
  absl::cord_internal::CordRep* rep = contents_.tree();
  if (rep == nullptr) {
    return absl::string_view(contents_.data(), contents_.size());
  } else {
    absl::string_view already_flat_contents;
    if (GetFlatAux(rep, &already_flat_contents)) {
      return already_flat_contents;
    }
  }
  return FlattenSlowPath();
}

inline void Cord::Append(absl::string_view src) {
  contents_.AppendArray(src, CordzUpdateTracker::kAppendString);
}

inline void Cord::Prepend(absl::string_view src) {
  PrependArray(src, CordzUpdateTracker::kPrependString);
}

inline void Cord::Append(CordBuffer buffer) {
  if (ABSL_PREDICT_FALSE(buffer.length() == 0)) return;
  contents_.MaybeRemoveEmptyCrcNode();
  absl::string_view short_value;
  if (CordRep* rep = buffer.ConsumeValue(short_value)) {
    contents_.AppendTree(rep, CordzUpdateTracker::kAppendCordBuffer);
  } else {
    AppendPrecise(short_value, CordzUpdateTracker::kAppendCordBuffer);
  }
}

inline void Cord::Prepend(CordBuffer buffer) {
  if (ABSL_PREDICT_FALSE(buffer.length() == 0)) return;
  contents_.MaybeRemoveEmptyCrcNode();
  absl::string_view short_value;
  if (CordRep* rep = buffer.ConsumeValue(short_value)) {
    contents_.PrependTree(rep, CordzUpdateTracker::kPrependCordBuffer);
  } else {
    PrependPrecise(short_value, CordzUpdateTracker::kPrependCordBuffer);
  }
}

inline CordBuffer Cord::GetAppendBuffer(size_t capacity, size_t min_capacity) {
  if (empty()) return CordBuffer::CreateWithDefaultLimit(capacity);
  return GetAppendBufferSlowPath(0, capacity, min_capacity);
}

inline CordBuffer Cord::GetCustomAppendBuffer(size_t block_size,
                                              size_t capacity,
                                              size_t min_capacity) {
  if (empty()) {
    return block_size ? CordBuffer::CreateWithCustomLimit(block_size, capacity)
                      : CordBuffer::CreateWithDefaultLimit(capacity);
  }
  return GetAppendBufferSlowPath(block_size, capacity, min_capacity);
}

extern template void Cord::Append(std::string&& src);
extern template void Cord::Prepend(std::string&& src);

inline int Cord::Compare(const Cord& rhs) const {
  if (!contents_.is_tree() && !rhs.contents_.is_tree()) {
    return contents_.data_.Compare(rhs.contents_.data_);
  }

  return CompareImpl(rhs);
}

// Does 'this' cord start/end with rhs
inline bool Cord::StartsWith(const Cord& rhs) const {
  if (contents_.IsSame(rhs.contents_)) return true;
  size_t rhs_size = rhs.size();
  if (size() < rhs_size) return false;
  return EqualsImpl(rhs, rhs_size);
}

inline bool Cord::StartsWith(absl::string_view rhs) const {
  size_t rhs_size = rhs.size();
  if (size() < rhs_size) return false;
  return EqualsImpl(rhs, rhs_size);
}

inline void Cord::CopyToArrayImpl(absl::Nonnull<char*> dst) const {
  if (!contents_.is_tree()) {
    if (!empty()) contents_.CopyToArray(dst);
  } else {
    CopyToArraySlowPath(dst);
  }
}

inline void Cord::ChunkIterator::InitTree(
    absl::Nonnull<cord_internal::CordRep*> tree) {
  tree = cord_internal::SkipCrcNode(tree);
  if (tree->tag == cord_internal::BTREE) {
    current_chunk_ = btree_reader_.Init(tree->btree());
  } else {
    current_leaf_ = tree;
    current_chunk_ = cord_internal::EdgeData(tree);
  }
}

inline Cord::ChunkIterator::ChunkIterator(
    absl::Nonnull<cord_internal::CordRep*> tree) {
  bytes_remaining_ = tree->length;
  InitTree(tree);
}

inline Cord::ChunkIterator::ChunkIterator(absl::Nonnull<const Cord*> cord) {
  if (CordRep* tree = cord->contents_.tree()) {
    bytes_remaining_ = tree->length;
    if (ABSL_PREDICT_TRUE(bytes_remaining_ != 0)) {
      InitTree(tree);
    } else {
      current_chunk_ = {};
    }
  } else {
    bytes_remaining_ = cord->contents_.inline_size();
    current_chunk_ = {cord->contents_.data(), bytes_remaining_};
  }
}

inline Cord::ChunkIterator& Cord::ChunkIterator::AdvanceBtree() {
  current_chunk_ = btree_reader_.Next();
  return *this;
}

inline void Cord::ChunkIterator::AdvanceBytesBtree(size_t n) {
  assert(n >= current_chunk_.size());
  bytes_remaining_ -= n;
  if (bytes_remaining_) {
    if (n == current_chunk_.size()) {
      current_chunk_ = btree_reader_.Next();
    } else {
      size_t offset = btree_reader_.length() - bytes_remaining_;
      current_chunk_ = btree_reader_.Seek(offset);
    }
  } else {
    current_chunk_ = {};
  }
}

inline Cord::ChunkIterator& Cord::ChunkIterator::operator++() {
  ABSL_HARDENING_ASSERT(bytes_remaining_ > 0 &&
                        "Attempted to iterate past `end()`");
  assert(bytes_remaining_ >= current_chunk_.size());
  bytes_remaining_ -= current_chunk_.size();
  if (bytes_remaining_ > 0) {
    if (btree_reader_) {
      return AdvanceBtree();
    } else {
      assert(!current_chunk_.empty());  // Called on invalid iterator.
    }
    current_chunk_ = {};
  }
  return *this;
}

inline Cord::ChunkIterator Cord::ChunkIterator::operator++(int) {
  ChunkIterator tmp(*this);
  operator++();
  return tmp;
}

inline bool Cord::ChunkIterator::operator==(const ChunkIterator& other) const {
  return bytes_remaining_ == other.bytes_remaining_;
}

inline bool Cord::ChunkIterator::operator!=(const ChunkIterator& other) const {
  return !(*this == other);
}

inline Cord::ChunkIterator::reference Cord::ChunkIterator::operator*() const {
  ABSL_HARDENING_ASSERT(bytes_remaining_ != 0);
  return current_chunk_;
}

inline Cord::ChunkIterator::pointer Cord::ChunkIterator::operator->() const {
  ABSL_HARDENING_ASSERT(bytes_remaining_ != 0);
  return &current_chunk_;
}

inline void Cord::ChunkIterator::RemoveChunkPrefix(size_t n) {
  assert(n < current_chunk_.size());
  current_chunk_.remove_prefix(n);
  bytes_remaining_ -= n;
}

inline void Cord::ChunkIterator::AdvanceBytes(size_t n) {
  assert(bytes_remaining_ >= n);
  if (ABSL_PREDICT_TRUE(n < current_chunk_.size())) {
    RemoveChunkPrefix(n);
  } else if (n != 0) {
    if (btree_reader_) {
      AdvanceBytesBtree(n);
    } else {
      bytes_remaining_ = 0;
    }
  }
}

inline Cord::ChunkIterator Cord::chunk_begin() const {
  return ChunkIterator(this);
}

inline Cord::ChunkIterator Cord::chunk_end() const { return ChunkIterator(); }

inline Cord::ChunkIterator Cord::ChunkRange::begin() const {
  return cord_->chunk_begin();
}

inline Cord::ChunkIterator Cord::ChunkRange::end() const {
  return cord_->chunk_end();
}

inline Cord::ChunkRange Cord::Chunks() const { return ChunkRange(this); }

inline Cord::CharIterator& Cord::CharIterator::operator++() {
  if (ABSL_PREDICT_TRUE(chunk_iterator_->size() > 1)) {
    chunk_iterator_.RemoveChunkPrefix(1);
  } else {
    ++chunk_iterator_;
  }
  return *this;
}

inline Cord::CharIterator Cord::CharIterator::operator++(int) {
  CharIterator tmp(*this);
  operator++();
  return tmp;
}

inline bool Cord::CharIterator::operator==(const CharIterator& other) const {
  return chunk_iterator_ == other.chunk_iterator_;
}

inline bool Cord::CharIterator::operator!=(const CharIterator& other) const {
  return !(*this == other);
}

inline Cord::CharIterator::reference Cord::CharIterator::operator*() const {
  return *chunk_iterator_->data();
}

inline Cord Cord::AdvanceAndRead(absl::Nonnull<CharIterator*> it,
                                 size_t n_bytes) {
  assert(it != nullptr);
  return it->chunk_iterator_.AdvanceAndReadBytes(n_bytes);
}

inline void Cord::Advance(absl::Nonnull<CharIterator*> it, size_t n_bytes) {
  assert(it != nullptr);
  it->chunk_iterator_.AdvanceBytes(n_bytes);
}

inline absl::string_view Cord::ChunkRemaining(const CharIterator& it) {
  return *it.chunk_iterator_;
}

inline Cord::CharIterator Cord::char_begin() const {
  return CharIterator(this);
}

inline Cord::CharIterator Cord::char_end() const { return CharIterator(); }

inline Cord::CharIterator Cord::CharRange::begin() const {
  return cord_->char_begin();
}

inline Cord::CharIterator Cord::CharRange::end() const {
  return cord_->char_end();
}

inline Cord::CharRange Cord::Chars() const { return CharRange(this); }

inline void Cord::ForEachChunk(
    absl::FunctionRef<void(absl::string_view)> callback) const {
  absl::cord_internal::CordRep* rep = contents_.tree();
  if (rep == nullptr) {
    callback(absl::string_view(contents_.data(), contents_.size()));
  } else {
    ForEachChunkAux(rep, callback);
  }
}

// Nonmember Cord-to-Cord relational operators.
inline bool operator==(const Cord& lhs, const Cord& rhs) {
  if (lhs.contents_.IsSame(rhs.contents_)) return true;
  size_t rhs_size = rhs.size();
  if (lhs.size() != rhs_size) return false;
  return lhs.EqualsImpl(rhs, rhs_size);
}

inline bool operator!=(const Cord& x, const Cord& y) { return !(x == y); }
inline bool operator<(const Cord& x, const Cord& y) { return x.Compare(y) < 0; }
inline bool operator>(const Cord& x, const Cord& y) { return x.Compare(y) > 0; }
inline bool operator<=(const Cord& x, const Cord& y) {
  return x.Compare(y) <= 0;
}
inline bool operator>=(const Cord& x, const Cord& y) {
  return x.Compare(y) >= 0;
}

// Nonmember Cord-to-absl::string_view relational operators.
//
// Due to implicit conversions, these also enable comparisons of Cord with
// std::string and const char*.
inline bool operator==(const Cord& lhs, absl::string_view rhs) {
  size_t lhs_size = lhs.size();
  size_t rhs_size = rhs.size();
  if (lhs_size != rhs_size) return false;
  return lhs.EqualsImpl(rhs, rhs_size);
}

inline bool operator==(absl::string_view x, const Cord& y) { return y == x; }
inline bool operator!=(const Cord& x, absl::string_view y) { return !(x == y); }
inline bool operator!=(absl::string_view x, const Cord& y) { return !(x == y); }
inline bool operator<(const Cord& x, absl::string_view y) {
  return x.Compare(y) < 0;
}
inline bool operator<(absl::string_view x, const Cord& y) {
  return y.Compare(x) > 0;
}
inline bool operator>(const Cord& x, absl::string_view y) { return y < x; }
inline bool operator>(absl::string_view x, const Cord& y) { return y < x; }
inline bool operator<=(const Cord& x, absl::string_view y) { return !(y < x); }
inline bool operator<=(absl::string_view x, const Cord& y) { return !(y < x); }
inline bool operator>=(const Cord& x, absl::string_view y) { return !(x < y); }
inline bool operator>=(absl::string_view x, const Cord& y) { return !(x < y); }

// Some internals exposed to test code.
namespace strings_internal {
class CordTestAccess {
 public:
  static size_t FlatOverhead();
  static size_t MaxFlatLength();
  static size_t SizeofCordRepExternal();
  static size_t SizeofCordRepSubstring();
  static size_t FlatTagToLength(uint8_t tag);
  static uint8_t LengthToTag(size_t s);
};
}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_CORD_H_
