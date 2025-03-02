// Copyright 2018 The Abseil Authors.
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
// An open-addressing
// hashtable with quadratic probing.
//
// This is a low level hashtable on top of which different interfaces can be
// implemented, like flat_hash_set, node_hash_set, string_hash_set, etc.
//
// The table interface is similar to that of std::unordered_set. Notable
// differences are that most member functions support heterogeneous keys when
// BOTH the hash and eq functions are marked as transparent. They do so by
// providing a typedef called `is_transparent`.
//
// When heterogeneous lookup is enabled, functions that take key_type act as if
// they have an overload set like:
//
//   iterator find(const key_type& key);
//   template <class K>
//   iterator find(const K& key);
//
//   size_type erase(const key_type& key);
//   template <class K>
//   size_type erase(const K& key);
//
//   std::pair<iterator, iterator> equal_range(const key_type& key);
//   template <class K>
//   std::pair<iterator, iterator> equal_range(const K& key);
//
// When heterogeneous lookup is disabled, only the explicit `key_type` overloads
// exist.
//
// In addition the pointer to element and iterator stability guarantees are
// weaker: all iterators and pointers are invalidated after a new element is
// inserted.
//
// IMPLEMENTATION DETAILS
//
// # Table Layout
//
// A raw_hash_set's backing array consists of control bytes followed by slots
// that may or may not contain objects.
//
// The layout of the backing array, for `capacity` slots, is thus, as a
// pseudo-struct:
//
//   struct BackingArray {
//     // Sampling handler. This field isn't present when the sampling is
//     // disabled or this allocation hasn't been selected for sampling.
//     HashtablezInfoHandle infoz_;
//     // The number of elements we can insert before growing the capacity.
//     size_t growth_left;
//     // Control bytes for the "real" slots.
//     ctrl_t ctrl[capacity];
//     // Always `ctrl_t::kSentinel`. This is used by iterators to find when to
//     // stop and serves no other purpose.
//     ctrl_t sentinel;
//     // A copy of the first `kWidth - 1` elements of `ctrl`. This is used so
//     // that if a probe sequence picks a value near the end of `ctrl`,
//     // `Group` will have valid control bytes to look at.
//     ctrl_t clones[kWidth - 1];
//     // The actual slot data.
//     slot_type slots[capacity];
//   };
//
// The length of this array is computed by `RawHashSetLayout::alloc_size` below.
//
// Control bytes (`ctrl_t`) are bytes (collected into groups of a
// platform-specific size) that define the state of the corresponding slot in
// the slot array. Group manipulation is tightly optimized to be as efficient
// as possible: SSE and friends on x86, clever bit operations on other arches.
//
//      Group 1         Group 2        Group 3
// +---------------+---------------+---------------+
// | | | | | | | | | | | | | | | | | | | | | | | | |
// +---------------+---------------+---------------+
//
// Each control byte is either a special value for empty slots, deleted slots
// (sometimes called *tombstones*), and a special end-of-table marker used by
// iterators, or, if occupied, seven bits (H2) from the hash of the value in the
// corresponding slot.
//
// Storing control bytes in a separate array also has beneficial cache effects,
// since more logical slots will fit into a cache line.
//
// # Small Object Optimization (SOO)
//
// When the size/alignment of the value_type and the capacity of the table are
// small, we enable small object optimization and store the values inline in
// the raw_hash_set object. This optimization allows us to avoid
// allocation/deallocation as well as cache/dTLB misses.
//
// # Hashing
//
// We compute two separate hashes, `H1` and `H2`, from the hash of an object.
// `H1(hash(x))` is an index into `slots`, and essentially the starting point
// for the probe sequence. `H2(hash(x))` is a 7-bit value used to filter out
// objects that cannot possibly be the one we are looking for.
//
// # Table operations.
//
// The key operations are `insert`, `find`, and `erase`.
//
// Since `insert` and `erase` are implemented in terms of `find`, we describe
// `find` first. To `find` a value `x`, we compute `hash(x)`. From
// `H1(hash(x))` and the capacity, we construct a `probe_seq` that visits every
// group of slots in some interesting order.
//
// We now walk through these indices. At each index, we select the entire group
// starting with that index and extract potential candidates: occupied slots
// with a control byte equal to `H2(hash(x))`. If we find an empty slot in the
// group, we stop and return an error. Each candidate slot `y` is compared with
// `x`; if `x == y`, we are done and return `&y`; otherwise we continue to the
// next probe index. Tombstones effectively behave like full slots that never
// match the value we're looking for.
//
// The `H2` bits ensure when we compare a slot to an object with `==`, we are
// likely to have actually found the object.  That is, the chance is low that
// `==` is called and returns `false`.  Thus, when we search for an object, we
// are unlikely to call `==` many times.  This likelyhood can be analyzed as
// follows (assuming that H2 is a random enough hash function).
//
// Let's assume that there are `k` "wrong" objects that must be examined in a
// probe sequence.  For example, when doing a `find` on an object that is in the
// table, `k` is the number of objects between the start of the probe sequence
// and the final found object (not including the final found object).  The
// expected number of objects with an H2 match is then `k/128`.  Measurements
// and analysis indicate that even at high load factors, `k` is less than 32,
// meaning that the number of "false positive" comparisons we must perform is
// less than 1/8 per `find`.

// `insert` is implemented in terms of `unchecked_insert`, which inserts a
// value presumed to not be in the table (violating this requirement will cause
// the table to behave erratically). Given `x` and its hash `hash(x)`, to insert
// it, we construct a `probe_seq` once again, and use it to find the first
// group with an unoccupied (empty *or* deleted) slot. We place `x` into the
// first such slot in the group and mark it as full with `x`'s H2.
//
// To `insert`, we compose `unchecked_insert` with `find`. We compute `h(x)` and
// perform a `find` to see if it's already present; if it is, we're done. If
// it's not, we may decide the table is getting overcrowded (i.e. the load
// factor is greater than 7/8 for big tables; `is_small()` tables use a max load
// factor of 1); in this case, we allocate a bigger array, `unchecked_insert`
// each element of the table into the new array (we know that no insertion here
// will insert an already-present value), and discard the old backing array. At
// this point, we may `unchecked_insert` the value `x`.
//
// Below, `unchecked_insert` is partly implemented by `prepare_insert`, which
// presents a viable, initialized slot pointee to the caller.
//
// `erase` is implemented in terms of `erase_at`, which takes an index to a
// slot. Given an offset, we simply create a tombstone and destroy its contents.
// If we can prove that the slot would not appear in a probe sequence, we can
// make the slot as empty, instead. We can prove this by observing that if a
// group has any empty slots, it has never been full (assuming we never create
// an empty slot in a group with no empties, which this heuristic guarantees we
// never do) and find would stop at this group anyways (since it does not probe
// beyond groups with empties).
//
// `erase` is `erase_at` composed with `find`: if we
// have a value `x`, we can perform a `find`, and then `erase_at` the resulting
// slot.
//
// To iterate, we simply traverse the array, skipping empty and deleted slots
// and stopping when we hit a `kSentinel`.

#ifndef ABSL_CONTAINER_INTERNAL_RAW_HASH_SET_H_
#define ABSL_CONTAINER_INTERNAL_RAW_HASH_SET_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/options.h"
#include "absl/base/port.h"
#include "absl/base/prefetch.h"
#include "absl/container/internal/common.h"  // IWYU pragma: export // for node_handle
#include "absl/container/internal/common_policy_traits.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_function_defaults.h"
#include "absl/container/internal/hash_policy_traits.h"
#include "absl/container/internal/hashtable_debug_hooks.h"
#include "absl/container/internal/hashtablez_sampler.h"
#include "absl/hash/hash.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/bits.h"
#include "absl/utility/utility.h"

#ifdef ABSL_INTERNAL_HAVE_SSE2
#include <emmintrin.h>
#endif

#ifdef ABSL_INTERNAL_HAVE_SSSE3
#include <tmmintrin.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#endif

#ifdef ABSL_INTERNAL_HAVE_ARM_NEON
#include <arm_neon.h>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

#ifdef ABSL_SWISSTABLE_ENABLE_GENERATIONS
#error ABSL_SWISSTABLE_ENABLE_GENERATIONS cannot be directly set
#elif (defined(ABSL_HAVE_ADDRESS_SANITIZER) ||   \
       defined(ABSL_HAVE_HWADDRESS_SANITIZER) || \
       defined(ABSL_HAVE_MEMORY_SANITIZER)) &&   \
    !defined(NDEBUG_SANITIZER)  // If defined, performance is important.
// When compiled in sanitizer mode, we add generation integers to the backing
// array and iterators. In the backing array, we store the generation between
// the control bytes and the slots. When iterators are dereferenced, we assert
// that the container has not been mutated in a way that could cause iterator
// invalidation since the iterator was initialized.
#define ABSL_SWISSTABLE_ENABLE_GENERATIONS
#endif

#ifdef ABSL_SWISSTABLE_ASSERT
#error ABSL_SWISSTABLE_ASSERT cannot be directly set
#else
// We use this macro for assertions that users may see when the table is in an
// invalid state that sanitizers may help diagnose.
#define ABSL_SWISSTABLE_ASSERT(CONDITION) \
  assert((CONDITION) && "Try enabling sanitizers.")
#endif

// We use uint8_t so we don't need to worry about padding.
using GenerationType = uint8_t;

// A sentinel value for empty generations. Using 0 makes it easy to constexpr
// initialize an array of this value.
constexpr GenerationType SentinelEmptyGeneration() { return 0; }

constexpr GenerationType NextGeneration(GenerationType generation) {
  return ++generation == SentinelEmptyGeneration() ? ++generation : generation;
}

#ifdef ABSL_SWISSTABLE_ENABLE_GENERATIONS
constexpr bool SwisstableGenerationsEnabled() { return true; }
constexpr size_t NumGenerationBytes() { return sizeof(GenerationType); }
#else
constexpr bool SwisstableGenerationsEnabled() { return false; }
constexpr size_t NumGenerationBytes() { return 0; }
#endif

template <typename AllocType>
void SwapAlloc(AllocType& lhs, AllocType& rhs,
               std::true_type /* propagate_on_container_swap */) {
  using std::swap;
  swap(lhs, rhs);
}
template <typename AllocType>
void SwapAlloc(AllocType& lhs, AllocType& rhs,
               std::false_type /* propagate_on_container_swap */) {
  (void)lhs;
  (void)rhs;
  assert(lhs == rhs &&
         "It's UB to call swap with unequal non-propagating allocators.");
}

template <typename AllocType>
void CopyAlloc(AllocType& lhs, AllocType& rhs,
               std::true_type /* propagate_alloc */) {
  lhs = rhs;
}
template <typename AllocType>
void CopyAlloc(AllocType&, AllocType&, std::false_type /* propagate_alloc */) {}

// The state for a probe sequence.
//
// Currently, the sequence is a triangular progression of the form
//
//   p(i) := Width * (i^2 + i)/2 + hash (mod mask + 1)
//
// The use of `Width` ensures that each probe step does not overlap groups;
// the sequence effectively outputs the addresses of *groups* (although not
// necessarily aligned to any boundary). The `Group` machinery allows us
// to check an entire group with minimal branching.
//
// Wrapping around at `mask + 1` is important, but not for the obvious reason.
// As described above, the first few entries of the control byte array
// are mirrored at the end of the array, which `Group` will find and use
// for selecting candidates. However, when those candidates' slots are
// actually inspected, there are no corresponding slots for the cloned bytes,
// so we need to make sure we've treated those offsets as "wrapping around".
//
// It turns out that this probe sequence visits every group exactly once if the
// number of groups is a power of two, since (i^2+i)/2 is a bijection in
// Z/(2^m). See https://en.wikipedia.org/wiki/Quadratic_probing
template <size_t Width>
class probe_seq {
 public:
  // Creates a new probe sequence using `hash` as the initial value of the
  // sequence and `mask` (usually the capacity of the table) as the mask to
  // apply to each value in the progression.
  probe_seq(size_t hash, size_t mask) {
    ABSL_SWISSTABLE_ASSERT(((mask + 1) & mask) == 0 && "not a mask");
    mask_ = mask;
    offset_ = hash & mask_;
  }

  // The offset within the table, i.e., the value `p(i)` above.
  size_t offset() const { return offset_; }
  size_t offset(size_t i) const { return (offset_ + i) & mask_; }

  void next() {
    index_ += Width;
    offset_ += index_;
    offset_ &= mask_;
  }
  // 0-based probe index, a multiple of `Width`.
  size_t index() const { return index_; }

 private:
  size_t mask_;
  size_t offset_;
  size_t index_ = 0;
};

template <class ContainerKey, class Hash, class Eq>
struct RequireUsableKey {
  template <class PassedKey, class... Args>
  std::pair<
      decltype(std::declval<const Hash&>()(std::declval<const PassedKey&>())),
      decltype(std::declval<const Eq&>()(std::declval<const ContainerKey&>(),
                                         std::declval<const PassedKey&>()))>*
  operator()(const PassedKey&, const Args&...) const;
};

template <class E, class Policy, class Hash, class Eq, class... Ts>
struct IsDecomposable : std::false_type {};

template <class Policy, class Hash, class Eq, class... Ts>
struct IsDecomposable<
    absl::void_t<decltype(Policy::apply(
        RequireUsableKey<typename Policy::key_type, Hash, Eq>(),
        std::declval<Ts>()...))>,
    Policy, Hash, Eq, Ts...> : std::true_type {};

// TODO(alkis): Switch to std::is_nothrow_swappable when gcc/clang supports it.
template <class T>
constexpr bool IsNoThrowSwappable(std::true_type = {} /* is_swappable */) {
  using std::swap;
  return noexcept(swap(std::declval<T&>(), std::declval<T&>()));
}
template <class T>
constexpr bool IsNoThrowSwappable(std::false_type /* is_swappable */) {
  return false;
}

template <typename T>
uint32_t TrailingZeros(T x) {
  ABSL_ASSUME(x != 0);
  return static_cast<uint32_t>(countr_zero(x));
}

// 8 bytes bitmask with most significant bit set for every byte.
constexpr uint64_t kMsbs8Bytes = 0x8080808080808080ULL;

// An abstract bitmask, such as that emitted by a SIMD instruction.
//
// Specifically, this type implements a simple bitset whose representation is
// controlled by `SignificantBits` and `Shift`. `SignificantBits` is the number
// of abstract bits in the bitset, while `Shift` is the log-base-two of the
// width of an abstract bit in the representation.
// This mask provides operations for any number of real bits set in an abstract
// bit. To add iteration on top of that, implementation must guarantee no more
// than the most significant real bit is set in a set abstract bit.
template <class T, int SignificantBits, int Shift = 0>
class NonIterableBitMask {
 public:
  explicit NonIterableBitMask(T mask) : mask_(mask) {}

  explicit operator bool() const { return this->mask_ != 0; }

  // Returns the index of the lowest *abstract* bit set in `self`.
  uint32_t LowestBitSet() const {
    return container_internal::TrailingZeros(mask_) >> Shift;
  }

  // Returns the index of the highest *abstract* bit set in `self`.
  uint32_t HighestBitSet() const {
    return static_cast<uint32_t>((bit_width(mask_) - 1) >> Shift);
  }

  // Returns the number of trailing zero *abstract* bits.
  uint32_t TrailingZeros() const {
    return container_internal::TrailingZeros(mask_) >> Shift;
  }

  // Returns the number of leading zero *abstract* bits.
  uint32_t LeadingZeros() const {
    constexpr int total_significant_bits = SignificantBits << Shift;
    constexpr int extra_bits = sizeof(T) * 8 - total_significant_bits;
    return static_cast<uint32_t>(
               countl_zero(static_cast<T>(mask_ << extra_bits))) >>
           Shift;
  }

  T mask_;
};

// Mask that can be iterable
//
// For example, when `SignificantBits` is 16 and `Shift` is zero, this is just
// an ordinary 16-bit bitset occupying the low 16 bits of `mask`. When
// `SignificantBits` is 8 and `Shift` is 3, abstract bits are represented as
// the bytes `0x00` and `0x80`, and it occupies all 64 bits of the bitmask.
// If NullifyBitsOnIteration is true (only allowed for Shift == 3),
// non zero abstract bit is allowed to have additional bits
// (e.g., `0xff`, `0x83` and `0x9c` are ok, but `0x6f` is not).
//
// For example:
//   for (int i : BitMask<uint32_t, 16>(0b101)) -> yields 0, 2
//   for (int i : BitMask<uint64_t, 8, 3>(0x0000000080800000)) -> yields 2, 3
template <class T, int SignificantBits, int Shift = 0,
          bool NullifyBitsOnIteration = false>
class BitMask : public NonIterableBitMask<T, SignificantBits, Shift> {
  using Base = NonIterableBitMask<T, SignificantBits, Shift>;
  static_assert(std::is_unsigned<T>::value, "");
  static_assert(Shift == 0 || Shift == 3, "");
  static_assert(!NullifyBitsOnIteration || Shift == 3, "");

 public:
  explicit BitMask(T mask) : Base(mask) {
    if (Shift == 3 && !NullifyBitsOnIteration) {
      ABSL_SWISSTABLE_ASSERT(this->mask_ == (this->mask_ & kMsbs8Bytes));
    }
  }
  // BitMask is an iterator over the indices of its abstract bits.
  using value_type = int;
  using iterator = BitMask;
  using const_iterator = BitMask;

  BitMask& operator++() {
    if (Shift == 3 && NullifyBitsOnIteration) {
      this->mask_ &= kMsbs8Bytes;
    }
    this->mask_ &= (this->mask_ - 1);
    return *this;
  }

  uint32_t operator*() const { return Base::LowestBitSet(); }

  BitMask begin() const { return *this; }
  BitMask end() const { return BitMask(0); }

 private:
  friend bool operator==(const BitMask& a, const BitMask& b) {
    return a.mask_ == b.mask_;
  }
  friend bool operator!=(const BitMask& a, const BitMask& b) {
    return a.mask_ != b.mask_;
  }
};

using h2_t = uint8_t;

// The values here are selected for maximum performance. See the static asserts
// below for details.

// A `ctrl_t` is a single control byte, which can have one of four
// states: empty, deleted, full (which has an associated seven-bit h2_t value)
// and the sentinel. They have the following bit patterns:
//
//      empty: 1 0 0 0 0 0 0 0
//    deleted: 1 1 1 1 1 1 1 0
//       full: 0 h h h h h h h  // h represents the hash bits.
//   sentinel: 1 1 1 1 1 1 1 1
//
// These values are specifically tuned for SSE-flavored SIMD.
// The static_asserts below detail the source of these choices.
//
// We use an enum class so that when strict aliasing is enabled, the compiler
// knows ctrl_t doesn't alias other types.
enum class ctrl_t : int8_t {
  kEmpty = -128,   // 0b10000000
  kDeleted = -2,   // 0b11111110
  kSentinel = -1,  // 0b11111111
};
static_assert(
    (static_cast<int8_t>(ctrl_t::kEmpty) &
     static_cast<int8_t>(ctrl_t::kDeleted) &
     static_cast<int8_t>(ctrl_t::kSentinel) & 0x80) != 0,
    "Special markers need to have the MSB to make checking for them efficient");
static_assert(
    ctrl_t::kEmpty < ctrl_t::kSentinel && ctrl_t::kDeleted < ctrl_t::kSentinel,
    "ctrl_t::kEmpty and ctrl_t::kDeleted must be smaller than "
    "ctrl_t::kSentinel to make the SIMD test of IsEmptyOrDeleted() efficient");
static_assert(
    ctrl_t::kSentinel == static_cast<ctrl_t>(-1),
    "ctrl_t::kSentinel must be -1 to elide loading it from memory into SIMD "
    "registers (pcmpeqd xmm, xmm)");
static_assert(ctrl_t::kEmpty == static_cast<ctrl_t>(-128),
              "ctrl_t::kEmpty must be -128 to make the SIMD check for its "
              "existence efficient (psignb xmm, xmm)");
static_assert(
    (~static_cast<int8_t>(ctrl_t::kEmpty) &
     ~static_cast<int8_t>(ctrl_t::kDeleted) &
     static_cast<int8_t>(ctrl_t::kSentinel) & 0x7F) != 0,
    "ctrl_t::kEmpty and ctrl_t::kDeleted must share an unset bit that is not "
    "shared by ctrl_t::kSentinel to make the scalar test for "
    "MaskEmptyOrDeleted() efficient");
static_assert(ctrl_t::kDeleted == static_cast<ctrl_t>(-2),
              "ctrl_t::kDeleted must be -2 to make the implementation of "
              "ConvertSpecialToEmptyAndFullToDeleted efficient");

// See definition comment for why this is size 32.
ABSL_DLL extern const ctrl_t kEmptyGroup[32];

// We use these sentinel capacity values in debug mode to indicate different
// classes of bugs.
enum InvalidCapacity : size_t {
  kAboveMaxValidCapacity = ~size_t{} - 100,
  kReentrance,
  kDestroyed,

  // These two must be last because we use `>= kMovedFrom` to mean moved-from.
  kMovedFrom,
  kSelfMovedFrom,
};

// Returns a pointer to a control byte group that can be used by empty tables.
inline ctrl_t* EmptyGroup() {
  // Const must be cast away here; no uses of this function will actually write
  // to it because it is only used for empty tables.
  return const_cast<ctrl_t*>(kEmptyGroup + 16);
}

// For use in SOO iterators.
// TODO(b/289225379): we could potentially get rid of this by adding an is_soo
// bit in iterators. This would add branches but reduce cache misses.
ABSL_DLL extern const ctrl_t kSooControl[17];

// Returns a pointer to a full byte followed by a sentinel byte.
inline ctrl_t* SooControl() {
  // Const must be cast away here; no uses of this function will actually write
  // to it because it is only used for SOO iterators.
  return const_cast<ctrl_t*>(kSooControl);
}
// Whether ctrl is from the SooControl array.
inline bool IsSooControl(const ctrl_t* ctrl) { return ctrl == SooControl(); }

// Returns a pointer to a generation to use for an empty hashtable.
GenerationType* EmptyGeneration();

// Returns whether `generation` is a generation for an empty hashtable that
// could be returned by EmptyGeneration().
inline bool IsEmptyGeneration(const GenerationType* generation) {
  return *generation == SentinelEmptyGeneration();
}

// Mixes a randomly generated per-process seed with `hash` and `ctrl` to
// randomize insertion order within groups.
bool ShouldInsertBackwardsForDebug(size_t capacity, size_t hash,
                                   const ctrl_t* ctrl);

ABSL_ATTRIBUTE_ALWAYS_INLINE inline bool ShouldInsertBackwards(
    ABSL_ATTRIBUTE_UNUSED size_t capacity, ABSL_ATTRIBUTE_UNUSED size_t hash,
    ABSL_ATTRIBUTE_UNUSED const ctrl_t* ctrl) {
#if defined(NDEBUG)
  return false;
#else
  return ShouldInsertBackwardsForDebug(capacity, hash, ctrl);
#endif
}

// Returns insert position for the given mask.
// We want to add entropy even when ASLR is not enabled.
// In debug build we will randomly insert in either the front or back of
// the group.
// TODO(kfm,sbenza): revisit after we do unconditional mixing
template <class Mask>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline auto GetInsertionOffset(
    Mask mask, ABSL_ATTRIBUTE_UNUSED size_t capacity,
    ABSL_ATTRIBUTE_UNUSED size_t hash,
    ABSL_ATTRIBUTE_UNUSED const ctrl_t* ctrl) {
#if defined(NDEBUG)
  return mask.LowestBitSet();
#else
  return ShouldInsertBackwardsForDebug(capacity, hash, ctrl)
             ? mask.HighestBitSet()
             : mask.LowestBitSet();
#endif
}

// Returns a per-table, hash salt, which changes on resize. This gets mixed into
// H1 to randomize iteration order per-table.
//
// The seed consists of the ctrl_ pointer, which adds enough entropy to ensure
// non-determinism of iteration order in most cases.
inline size_t PerTableSalt(const ctrl_t* ctrl) {
  // The low bits of the pointer have little or no entropy because of
  // alignment. We shift the pointer to try to use higher entropy bits. A
  // good number seems to be 12 bits, because that aligns with page size.
  return reinterpret_cast<uintptr_t>(ctrl) >> 12;
}
// Extracts the H1 portion of a hash: 57 bits mixed with a per-table salt.
inline size_t H1(size_t hash, const ctrl_t* ctrl) {
  return (hash >> 7) ^ PerTableSalt(ctrl);
}

// Extracts the H2 portion of a hash: the 7 bits not used for H1.
//
// These are used as an occupied control byte.
inline h2_t H2(size_t hash) { return hash & 0x7F; }

// Helpers for checking the state of a control byte.
inline bool IsEmpty(ctrl_t c) { return c == ctrl_t::kEmpty; }
inline bool IsFull(ctrl_t c) {
  // Cast `c` to the underlying type instead of casting `0` to `ctrl_t` as `0`
  // is not a value in the enum. Both ways are equivalent, but this way makes
  // linters happier.
  return static_cast<std::underlying_type_t<ctrl_t>>(c) >= 0;
}
inline bool IsDeleted(ctrl_t c) { return c == ctrl_t::kDeleted; }
inline bool IsEmptyOrDeleted(ctrl_t c) { return c < ctrl_t::kSentinel; }

#ifdef ABSL_INTERNAL_HAVE_SSE2
// Quick reference guide for intrinsics used below:
//
// * __m128i: An XMM (128-bit) word.
//
// * _mm_setzero_si128: Returns a zero vector.
// * _mm_set1_epi8:     Returns a vector with the same i8 in each lane.
//
// * _mm_subs_epi8:    Saturating-subtracts two i8 vectors.
// * _mm_and_si128:    Ands two i128s together.
// * _mm_or_si128:     Ors two i128s together.
// * _mm_andnot_si128: And-nots two i128s together.
//
// * _mm_cmpeq_epi8: Component-wise compares two i8 vectors for equality,
//                   filling each lane with 0x00 or 0xff.
// * _mm_cmpgt_epi8: Same as above, but using > rather than ==.
//
// * _mm_loadu_si128:  Performs an unaligned load of an i128.
// * _mm_storeu_si128: Performs an unaligned store of an i128.
//
// * _mm_sign_epi8:     Retains, negates, or zeroes each i8 lane of the first
//                      argument if the corresponding lane of the second
//                      argument is positive, negative, or zero, respectively.
// * _mm_movemask_epi8: Selects the sign bit out of each i8 lane and produces a
//                      bitmask consisting of those bits.
// * _mm_shuffle_epi8:  Selects i8s from the first argument, using the low
//                      four bits of each i8 lane in the second argument as
//                      indices.

// https://github.com/abseil/abseil-cpp/issues/209
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=87853
// _mm_cmpgt_epi8 is broken under GCC with -funsigned-char
// Work around this by using the portable implementation of Group
// when using -funsigned-char under GCC.
inline __m128i _mm_cmpgt_epi8_fixed(__m128i a, __m128i b) {
#if defined(__GNUC__) && !defined(__clang__)
  if (std::is_unsigned<char>::value) {
    const __m128i mask = _mm_set1_epi8(0x80);
    const __m128i diff = _mm_subs_epi8(b, a);
    return _mm_cmpeq_epi8(_mm_and_si128(diff, mask), mask);
  }
#endif
  return _mm_cmpgt_epi8(a, b);
}

struct GroupSse2Impl {
  static constexpr size_t kWidth = 16;  // the number of slots per group

  explicit GroupSse2Impl(const ctrl_t* pos) {
    ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
  }

  // Returns a bitmask representing the positions of slots that match hash.
  BitMask<uint16_t, kWidth> Match(h2_t hash) const {
    auto match = _mm_set1_epi8(static_cast<char>(hash));
    return BitMask<uint16_t, kWidth>(
        static_cast<uint16_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(match, ctrl))));
  }

  // Returns a bitmask representing the positions of empty slots.
  NonIterableBitMask<uint16_t, kWidth> MaskEmpty() const {
#ifdef ABSL_INTERNAL_HAVE_SSSE3
    // This only works because ctrl_t::kEmpty is -128.
    return NonIterableBitMask<uint16_t, kWidth>(
        static_cast<uint16_t>(_mm_movemask_epi8(_mm_sign_epi8(ctrl, ctrl))));
#else
    auto match = _mm_set1_epi8(static_cast<char>(ctrl_t::kEmpty));
    return NonIterableBitMask<uint16_t, kWidth>(
        static_cast<uint16_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(match, ctrl))));
#endif
  }

  // Returns a bitmask representing the positions of full slots.
  // Note: for `is_small()` tables group may contain the "same" slot twice:
  // original and mirrored.
  BitMask<uint16_t, kWidth> MaskFull() const {
    return BitMask<uint16_t, kWidth>(
        static_cast<uint16_t>(_mm_movemask_epi8(ctrl) ^ 0xffff));
  }

  // Returns a bitmask representing the positions of non full slots.
  // Note: this includes: kEmpty, kDeleted, kSentinel.
  // It is useful in contexts when kSentinel is not present.
  auto MaskNonFull() const {
    return BitMask<uint16_t, kWidth>(
        static_cast<uint16_t>(_mm_movemask_epi8(ctrl)));
  }

  // Returns a bitmask representing the positions of empty or deleted slots.
  NonIterableBitMask<uint16_t, kWidth> MaskEmptyOrDeleted() const {
    auto special = _mm_set1_epi8(static_cast<char>(ctrl_t::kSentinel));
    return NonIterableBitMask<uint16_t, kWidth>(static_cast<uint16_t>(
        _mm_movemask_epi8(_mm_cmpgt_epi8_fixed(special, ctrl))));
  }

  // Returns the number of trailing empty or deleted elements in the group.
  uint32_t CountLeadingEmptyOrDeleted() const {
    auto special = _mm_set1_epi8(static_cast<char>(ctrl_t::kSentinel));
    return TrailingZeros(static_cast<uint32_t>(
        _mm_movemask_epi8(_mm_cmpgt_epi8_fixed(special, ctrl)) + 1));
  }

  void ConvertSpecialToEmptyAndFullToDeleted(ctrl_t* dst) const {
    auto msbs = _mm_set1_epi8(static_cast<char>(-128));
    auto x126 = _mm_set1_epi8(126);
#ifdef ABSL_INTERNAL_HAVE_SSSE3
    auto res = _mm_or_si128(_mm_shuffle_epi8(x126, ctrl), msbs);
#else
    auto zero = _mm_setzero_si128();
    auto special_mask = _mm_cmpgt_epi8_fixed(zero, ctrl);
    auto res = _mm_or_si128(msbs, _mm_andnot_si128(special_mask, x126));
#endif
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
  }

  __m128i ctrl;
};
#endif  // ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2

#if defined(ABSL_INTERNAL_HAVE_ARM_NEON) && defined(ABSL_IS_LITTLE_ENDIAN)
struct GroupAArch64Impl {
  static constexpr size_t kWidth = 8;

  explicit GroupAArch64Impl(const ctrl_t* pos) {
    ctrl = vld1_u8(reinterpret_cast<const uint8_t*>(pos));
  }

  auto Match(h2_t hash) const {
    uint8x8_t dup = vdup_n_u8(hash);
    auto mask = vceq_u8(ctrl, dup);
    return BitMask<uint64_t, kWidth, /*Shift=*/3,
                   /*NullifyBitsOnIteration=*/true>(
        vget_lane_u64(vreinterpret_u64_u8(mask), 0));
  }

  NonIterableBitMask<uint64_t, kWidth, 3> MaskEmpty() const {
    uint64_t mask =
        vget_lane_u64(vreinterpret_u64_u8(vceq_s8(
                          vdup_n_s8(static_cast<int8_t>(ctrl_t::kEmpty)),
                          vreinterpret_s8_u8(ctrl))),
                      0);
    return NonIterableBitMask<uint64_t, kWidth, 3>(mask);
  }

  // Returns a bitmask representing the positions of full slots.
  // Note: for `is_small()` tables group may contain the "same" slot twice:
  // original and mirrored.
  auto MaskFull() const {
    uint64_t mask = vget_lane_u64(
        vreinterpret_u64_u8(vcge_s8(vreinterpret_s8_u8(ctrl),
                                    vdup_n_s8(static_cast<int8_t>(0)))),
        0);
    return BitMask<uint64_t, kWidth, /*Shift=*/3,
                   /*NullifyBitsOnIteration=*/true>(mask);
  }

  // Returns a bitmask representing the positions of non full slots.
  // Note: this includes: kEmpty, kDeleted, kSentinel.
  // It is useful in contexts when kSentinel is not present.
  auto MaskNonFull() const {
    uint64_t mask = vget_lane_u64(
        vreinterpret_u64_u8(vclt_s8(vreinterpret_s8_u8(ctrl),
                                    vdup_n_s8(static_cast<int8_t>(0)))),
        0);
    return BitMask<uint64_t, kWidth, /*Shift=*/3,
                   /*NullifyBitsOnIteration=*/true>(mask);
  }

  NonIterableBitMask<uint64_t, kWidth, 3> MaskEmptyOrDeleted() const {
    uint64_t mask =
        vget_lane_u64(vreinterpret_u64_u8(vcgt_s8(
                          vdup_n_s8(static_cast<int8_t>(ctrl_t::kSentinel)),
                          vreinterpret_s8_u8(ctrl))),
                      0);
    return NonIterableBitMask<uint64_t, kWidth, 3>(mask);
  }

  uint32_t CountLeadingEmptyOrDeleted() const {
    uint64_t mask =
        vget_lane_u64(vreinterpret_u64_u8(vcle_s8(
                          vdup_n_s8(static_cast<int8_t>(ctrl_t::kSentinel)),
                          vreinterpret_s8_u8(ctrl))),
                      0);
    // Similar to MaskEmptyorDeleted() but we invert the logic to invert the
    // produced bitfield. We then count number of trailing zeros.
    // Clang and GCC optimize countr_zero to rbit+clz without any check for 0,
    // so we should be fine.
    return static_cast<uint32_t>(countr_zero(mask)) >> 3;
  }

  void ConvertSpecialToEmptyAndFullToDeleted(ctrl_t* dst) const {
    uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(ctrl), 0);
    constexpr uint64_t slsbs = 0x0202020202020202ULL;
    constexpr uint64_t midbs = 0x7e7e7e7e7e7e7e7eULL;
    auto x = slsbs & (mask >> 6);
    auto res = (x + midbs) | kMsbs8Bytes;
    little_endian::Store64(dst, res);
  }

  uint8x8_t ctrl;
};
#endif  // ABSL_INTERNAL_HAVE_ARM_NEON && ABSL_IS_LITTLE_ENDIAN

struct GroupPortableImpl {
  static constexpr size_t kWidth = 8;

  explicit GroupPortableImpl(const ctrl_t* pos)
      : ctrl(little_endian::Load64(pos)) {}

  BitMask<uint64_t, kWidth, 3> Match(h2_t hash) const {
    // For the technique, see:
    // http://graphics.stanford.edu/~seander/bithacks.html##ValueInWord
    // (Determine if a word has a byte equal to n).
    //
    // Caveat: there are false positives but:
    // - they only occur if there is a real match
    // - they never occur on ctrl_t::kEmpty, ctrl_t::kDeleted, ctrl_t::kSentinel
    // - they will be handled gracefully by subsequent checks in code
    //
    // Example:
    //   v = 0x1716151413121110
    //   hash = 0x12
    //   retval = (v - lsbs) & ~v & msbs = 0x0000000080800000
    constexpr uint64_t lsbs = 0x0101010101010101ULL;
    auto x = ctrl ^ (lsbs * hash);
    return BitMask<uint64_t, kWidth, 3>((x - lsbs) & ~x & kMsbs8Bytes);
  }

  NonIterableBitMask<uint64_t, kWidth, 3> MaskEmpty() const {
    return NonIterableBitMask<uint64_t, kWidth, 3>((ctrl & ~(ctrl << 6)) &
                                                   kMsbs8Bytes);
  }

  // Returns a bitmask representing the positions of full slots.
  // Note: for `is_small()` tables group may contain the "same" slot twice:
  // original and mirrored.
  BitMask<uint64_t, kWidth, 3> MaskFull() const {
    return BitMask<uint64_t, kWidth, 3>((ctrl ^ kMsbs8Bytes) & kMsbs8Bytes);
  }

  // Returns a bitmask representing the positions of non full slots.
  // Note: this includes: kEmpty, kDeleted, kSentinel.
  // It is useful in contexts when kSentinel is not present.
  auto MaskNonFull() const {
    return BitMask<uint64_t, kWidth, 3>(ctrl & kMsbs8Bytes);
  }

  NonIterableBitMask<uint64_t, kWidth, 3> MaskEmptyOrDeleted() const {
    return NonIterableBitMask<uint64_t, kWidth, 3>((ctrl & ~(ctrl << 7)) &
                                                   kMsbs8Bytes);
  }

  uint32_t CountLeadingEmptyOrDeleted() const {
    // ctrl | ~(ctrl >> 7) will have the lowest bit set to zero for kEmpty and
    // kDeleted. We lower all other bits and count number of trailing zeros.
    constexpr uint64_t bits = 0x0101010101010101ULL;
    return static_cast<uint32_t>(countr_zero((ctrl | ~(ctrl >> 7)) & bits) >>
                                 3);
  }

  void ConvertSpecialToEmptyAndFullToDeleted(ctrl_t* dst) const {
    constexpr uint64_t lsbs = 0x0101010101010101ULL;
    auto x = ctrl & kMsbs8Bytes;
    auto res = (~x + (x >> 7)) & ~lsbs;
    little_endian::Store64(dst, res);
  }

  uint64_t ctrl;
};

#ifdef ABSL_INTERNAL_HAVE_SSE2
using Group = GroupSse2Impl;
using GroupFullEmptyOrDeleted = GroupSse2Impl;
#elif defined(ABSL_INTERNAL_HAVE_ARM_NEON) && defined(ABSL_IS_LITTLE_ENDIAN)
using Group = GroupAArch64Impl;
// For Aarch64, we use the portable implementation for counting and masking
// full, empty or deleted group elements. This is to avoid the latency of moving
// between data GPRs and Neon registers when it does not provide a benefit.
// Using Neon is profitable when we call Match(), but is not when we don't,
// which is the case when we do *EmptyOrDeleted and MaskFull operations.
// It is difficult to make a similar approach beneficial on other architectures
// such as x86 since they have much lower GPR <-> vector register transfer
// latency and 16-wide Groups.
using GroupFullEmptyOrDeleted = GroupPortableImpl;
#else
using Group = GroupPortableImpl;
using GroupFullEmptyOrDeleted = GroupPortableImpl;
#endif

// When there is an insertion with no reserved growth, we rehash with
// probability `min(1, RehashProbabilityConstant() / capacity())`. Using a
// constant divided by capacity ensures that inserting N elements is still O(N)
// in the average case. Using the constant 16 means that we expect to rehash ~8
// times more often than when generations are disabled. We are adding expected
// rehash_probability * #insertions/capacity_growth = 16/capacity * ((7/8 -
// 7/16) * capacity)/capacity_growth = ~7 extra rehashes per capacity growth.
inline size_t RehashProbabilityConstant() { return 16; }

class CommonFieldsGenerationInfoEnabled {
  // A sentinel value for reserved_growth_ indicating that we just ran out of
  // reserved growth on the last insertion. When reserve is called and then
  // insertions take place, reserved_growth_'s state machine is N, ..., 1,
  // kReservedGrowthJustRanOut, 0.
  static constexpr size_t kReservedGrowthJustRanOut =
      (std::numeric_limits<size_t>::max)();

 public:
  CommonFieldsGenerationInfoEnabled() = default;
  CommonFieldsGenerationInfoEnabled(CommonFieldsGenerationInfoEnabled&& that)
      : reserved_growth_(that.reserved_growth_),
        reservation_size_(that.reservation_size_),
        generation_(that.generation_) {
    that.reserved_growth_ = 0;
    that.reservation_size_ = 0;
    that.generation_ = EmptyGeneration();
  }
  CommonFieldsGenerationInfoEnabled& operator=(
      CommonFieldsGenerationInfoEnabled&&) = default;

  // Whether we should rehash on insert in order to detect bugs of using invalid
  // references. We rehash on the first insertion after reserved_growth_ reaches
  // 0 after a call to reserve. We also do a rehash with low probability
  // whenever reserved_growth_ is zero.
  bool should_rehash_for_bug_detection_on_insert(const ctrl_t* ctrl,
                                                 size_t capacity) const;
  // Similar to above, except that we don't depend on reserved_growth_.
  bool should_rehash_for_bug_detection_on_move(const ctrl_t* ctrl,
                                               size_t capacity) const;
  void maybe_increment_generation_on_insert() {
    if (reserved_growth_ == kReservedGrowthJustRanOut) reserved_growth_ = 0;

    if (reserved_growth_ > 0) {
      if (--reserved_growth_ == 0) reserved_growth_ = kReservedGrowthJustRanOut;
    } else {
      increment_generation();
    }
  }
  void increment_generation() { *generation_ = NextGeneration(*generation_); }
  void reset_reserved_growth(size_t reservation, size_t size) {
    reserved_growth_ = reservation - size;
  }
  size_t reserved_growth() const { return reserved_growth_; }
  void set_reserved_growth(size_t r) { reserved_growth_ = r; }
  size_t reservation_size() const { return reservation_size_; }
  void set_reservation_size(size_t r) { reservation_size_ = r; }
  GenerationType generation() const { return *generation_; }
  void set_generation(GenerationType g) { *generation_ = g; }
  GenerationType* generation_ptr() const { return generation_; }
  void set_generation_ptr(GenerationType* g) { generation_ = g; }

 private:
  // The number of insertions remaining that are guaranteed to not rehash due to
  // a prior call to reserve. Note: we store reserved growth in addition to
  // reservation size because calls to erase() decrease size_ but don't decrease
  // reserved growth.
  size_t reserved_growth_ = 0;
  // The maximum argument to reserve() since the container was cleared. We need
  // to keep track of this, in addition to reserved growth, because we reset
  // reserved growth to this when erase(begin(), end()) is called.
  size_t reservation_size_ = 0;
  // Pointer to the generation counter, which is used to validate iterators and
  // is stored in the backing array between the control bytes and the slots.
  // Note that we can't store the generation inside the container itself and
  // keep a pointer to the container in the iterators because iterators must
  // remain valid when the container is moved.
  // Note: we could derive this pointer from the control pointer, but it makes
  // the code more complicated, and there's a benefit in having the sizes of
  // raw_hash_set in sanitizer mode and non-sanitizer mode a bit more different,
  // which is that tests are less likely to rely on the size remaining the same.
  GenerationType* generation_ = EmptyGeneration();
};

class CommonFieldsGenerationInfoDisabled {
 public:
  CommonFieldsGenerationInfoDisabled() = default;
  CommonFieldsGenerationInfoDisabled(CommonFieldsGenerationInfoDisabled&&) =
      default;
  CommonFieldsGenerationInfoDisabled& operator=(
      CommonFieldsGenerationInfoDisabled&&) = default;

  bool should_rehash_for_bug_detection_on_insert(const ctrl_t*, size_t) const {
    return false;
  }
  bool should_rehash_for_bug_detection_on_move(const ctrl_t*, size_t) const {
    return false;
  }
  void maybe_increment_generation_on_insert() {}
  void increment_generation() {}
  void reset_reserved_growth(size_t, size_t) {}
  size_t reserved_growth() const { return 0; }
  void set_reserved_growth(size_t) {}
  size_t reservation_size() const { return 0; }
  void set_reservation_size(size_t) {}
  GenerationType generation() const { return 0; }
  void set_generation(GenerationType) {}
  GenerationType* generation_ptr() const { return nullptr; }
  void set_generation_ptr(GenerationType*) {}
};

class HashSetIteratorGenerationInfoEnabled {
 public:
  HashSetIteratorGenerationInfoEnabled() = default;
  explicit HashSetIteratorGenerationInfoEnabled(
      const GenerationType* generation_ptr)
      : generation_ptr_(generation_ptr), generation_(*generation_ptr) {}

  GenerationType generation() const { return generation_; }
  void reset_generation() { generation_ = *generation_ptr_; }
  const GenerationType* generation_ptr() const { return generation_ptr_; }
  void set_generation_ptr(const GenerationType* ptr) { generation_ptr_ = ptr; }

 private:
  const GenerationType* generation_ptr_ = EmptyGeneration();
  GenerationType generation_ = *generation_ptr_;
};

class HashSetIteratorGenerationInfoDisabled {
 public:
  HashSetIteratorGenerationInfoDisabled() = default;
  explicit HashSetIteratorGenerationInfoDisabled(const GenerationType*) {}

  GenerationType generation() const { return 0; }
  void reset_generation() {}
  const GenerationType* generation_ptr() const { return nullptr; }
  void set_generation_ptr(const GenerationType*) {}
};

#ifdef ABSL_SWISSTABLE_ENABLE_GENERATIONS
using CommonFieldsGenerationInfo = CommonFieldsGenerationInfoEnabled;
using HashSetIteratorGenerationInfo = HashSetIteratorGenerationInfoEnabled;
#else
using CommonFieldsGenerationInfo = CommonFieldsGenerationInfoDisabled;
using HashSetIteratorGenerationInfo = HashSetIteratorGenerationInfoDisabled;
#endif

// Stored the information regarding number of slots we can still fill
// without needing to rehash.
//
// We want to ensure sufficient number of empty slots in the table in order
// to keep probe sequences relatively short. Empty slot in the probe group
// is required to stop probing.
//
// Tombstones (kDeleted slots) are not included in the growth capacity,
// because we'd like to rehash when the table is filled with tombstones and/or
// full slots.
//
// GrowthInfo also stores a bit that encodes whether table may have any
// deleted slots.
// Most of the tables (>95%) have no deleted slots, so some functions can
// be more efficient with this information.
//
// Callers can also force a rehash via the standard `rehash(0)`,
// which will recompute this value as a side-effect.
//
// See also `CapacityToGrowth()`.
class GrowthInfo {
 public:
  // Leaves data member uninitialized.
  GrowthInfo() = default;

  // Initializes the GrowthInfo assuming we can grow `growth_left` elements
  // and there are no kDeleted slots in the table.
  void InitGrowthLeftNoDeleted(size_t growth_left) {
    growth_left_info_ = growth_left;
  }

  // Overwrites single full slot with an empty slot.
  void OverwriteFullAsEmpty() { ++growth_left_info_; }

  // Overwrites single empty slot with a full slot.
  void OverwriteEmptyAsFull() {
    ABSL_SWISSTABLE_ASSERT(GetGrowthLeft() > 0);
    --growth_left_info_;
  }

  // Overwrites several empty slots with full slots.
  void OverwriteManyEmptyAsFull(size_t cnt) {
    ABSL_SWISSTABLE_ASSERT(GetGrowthLeft() >= cnt);
    growth_left_info_ -= cnt;
  }

  // Overwrites specified control element with full slot.
  void OverwriteControlAsFull(ctrl_t ctrl) {
    ABSL_SWISSTABLE_ASSERT(GetGrowthLeft() >=
                           static_cast<size_t>(IsEmpty(ctrl)));
    growth_left_info_ -= static_cast<size_t>(IsEmpty(ctrl));
  }

  // Overwrites single full slot with a deleted slot.
  void OverwriteFullAsDeleted() { growth_left_info_ |= kDeletedBit; }

  // Returns true if table satisfies two properties:
  // 1. Guaranteed to have no kDeleted slots.
  // 2. There is a place for at least one element to grow.
  bool HasNoDeletedAndGrowthLeft() const {
    return static_cast<std::make_signed_t<size_t>>(growth_left_info_) > 0;
  }

  // Returns true if the table satisfies two properties:
  // 1. Guaranteed to have no kDeleted slots.
  // 2. There is no growth left.
  bool HasNoGrowthLeftAndNoDeleted() const { return growth_left_info_ == 0; }

  // Returns true if table guaranteed to have no k
  bool HasNoDeleted() const {
    return static_cast<std::make_signed_t<size_t>>(growth_left_info_) >= 0;
  }

  // Returns the number of elements left to grow.
  size_t GetGrowthLeft() const { return growth_left_info_ & kGrowthLeftMask; }

 private:
  static constexpr size_t kGrowthLeftMask = ((~size_t{}) >> 1);
  static constexpr size_t kDeletedBit = ~kGrowthLeftMask;
  // Topmost bit signal whenever there are deleted slots.
  size_t growth_left_info_;
};

static_assert(sizeof(GrowthInfo) == sizeof(size_t), "");
static_assert(alignof(GrowthInfo) == alignof(size_t), "");

// Returns whether `n` is a valid capacity (i.e., number of slots).
//
// A valid capacity is a non-zero integer `2^m - 1`.
inline bool IsValidCapacity(size_t n) { return ((n + 1) & n) == 0 && n > 0; }

// Returns the number of "cloned control bytes".
//
// This is the number of control bytes that are present both at the beginning
// of the control byte array and at the end, such that we can create a
// `Group::kWidth`-width probe window starting from any control byte.
constexpr size_t NumClonedBytes() { return Group::kWidth - 1; }

// Returns the number of control bytes including cloned.
constexpr size_t NumControlBytes(size_t capacity) {
  return capacity + 1 + NumClonedBytes();
}

// Computes the offset from the start of the backing allocation of control.
// infoz and growth_info are stored at the beginning of the backing array.
inline static size_t ControlOffset(bool has_infoz) {
  return (has_infoz ? sizeof(HashtablezInfoHandle) : 0) + sizeof(GrowthInfo);
}

// Helper class for computing offsets and allocation size of hash set fields.
class RawHashSetLayout {
 public:
  explicit RawHashSetLayout(size_t capacity, size_t slot_align, bool has_infoz)
      : capacity_(capacity),
        control_offset_(ControlOffset(has_infoz)),
        generation_offset_(control_offset_ + NumControlBytes(capacity)),
        slot_offset_(
            (generation_offset_ + NumGenerationBytes() + slot_align - 1) &
            (~slot_align + 1)) {
    ABSL_SWISSTABLE_ASSERT(IsValidCapacity(capacity));
  }

  // Returns the capacity of a table.
  size_t capacity() const { return capacity_; }

  // Returns precomputed offset from the start of the backing allocation of
  // control.
  size_t control_offset() const { return control_offset_; }

  // Given the capacity of a table, computes the offset (from the start of the
  // backing allocation) of the generation counter (if it exists).
  size_t generation_offset() const { return generation_offset_; }

  // Given the capacity of a table, computes the offset (from the start of the
  // backing allocation) at which the slots begin.
  size_t slot_offset() const { return slot_offset_; }

  // Given the capacity of a table, computes the total size of the backing
  // array.
  size_t alloc_size(size_t slot_size) const {
    ABSL_SWISSTABLE_ASSERT(
        slot_size <=
        ((std::numeric_limits<size_t>::max)() - slot_offset_) / capacity_);
    return slot_offset_ + capacity_ * slot_size;
  }

 private:
  size_t capacity_;
  size_t control_offset_;
  size_t generation_offset_;
  size_t slot_offset_;
};

struct HashtableFreeFunctionsAccess;

// We only allow a maximum of 1 SOO element, which makes the implementation
// much simpler. Complications with multiple SOO elements include:
// - Satisfying the guarantee that erasing one element doesn't invalidate
//   iterators to other elements means we would probably need actual SOO
//   control bytes.
// - In order to prevent user code from depending on iteration order for small
//   tables, we would need to randomize the iteration order somehow.
constexpr size_t SooCapacity() { return 1; }
// Sentinel type to indicate SOO CommonFields construction.
struct soo_tag_t {};
// Sentinel type to indicate SOO CommonFields construction with full size.
struct full_soo_tag_t {};
// Sentinel type to indicate non-SOO CommonFields construction.
struct non_soo_tag_t {};
// Sentinel value to indicate an uninitialized CommonFields for use in swapping.
struct uninitialized_tag_t {};

// Suppress erroneous uninitialized memory errors on GCC. For example, GCC
// thinks that the call to slot_array() in find_or_prepare_insert() is reading
// uninitialized memory, but slot_array is only called there when the table is
// non-empty and this memory is initialized when the table is non-empty.
#if !defined(__clang__) && defined(__GNUC__)
#define ABSL_SWISSTABLE_IGNORE_UNINITIALIZED(x)                    \
  _Pragma("GCC diagnostic push")                                   \
      _Pragma("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")  \
          _Pragma("GCC diagnostic ignored \"-Wuninitialized\"") x; \
  _Pragma("GCC diagnostic pop")
#define ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(x) \
  ABSL_SWISSTABLE_IGNORE_UNINITIALIZED(return x)
#else
#define ABSL_SWISSTABLE_IGNORE_UNINITIALIZED(x) x
#define ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(x) return x
#endif

// This allows us to work around an uninitialized memory warning when
// constructing begin() iterators in empty hashtables.
union MaybeInitializedPtr {
  void* get() const { ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(p); }
  void set(void* ptr) { p = ptr; }

  void* p;
};

struct HeapPtrs {
  HeapPtrs() = default;
  explicit HeapPtrs(ctrl_t* c) : control(c) {}

  // The control bytes (and, also, a pointer near to the base of the backing
  // array).
  //
  // This contains `capacity + 1 + NumClonedBytes()` entries, even
  // when the table is empty (hence EmptyGroup).
  //
  // Note that growth_info is stored immediately before this pointer.
  // May be uninitialized for SOO tables.
  ctrl_t* control;

  // The beginning of the slots, located at `SlotOffset()` bytes after
  // `control`. May be uninitialized for empty tables.
  // Note: we can't use `slots` because Qt defines "slots" as a macro.
  MaybeInitializedPtr slot_array;
};

// Manages the backing array pointers or the SOO slot. When raw_hash_set::is_soo
// is true, the SOO slot is stored in `soo_data`. Otherwise, we use `heap`.
union HeapOrSoo {
  HeapOrSoo() = default;
  explicit HeapOrSoo(ctrl_t* c) : heap(c) {}

  ctrl_t*& control() {
    ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(heap.control);
  }
  ctrl_t* control() const {
    ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(heap.control);
  }
  MaybeInitializedPtr& slot_array() {
    ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(heap.slot_array);
  }
  MaybeInitializedPtr slot_array() const {
    ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(heap.slot_array);
  }
  void* get_soo_data() {
    ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(soo_data);
  }
  const void* get_soo_data() const {
    ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN(soo_data);
  }

  HeapPtrs heap;
  unsigned char soo_data[sizeof(HeapPtrs)];
};

// CommonFields hold the fields in raw_hash_set that do not depend
// on template parameters. This allows us to conveniently pass all
// of this state to helper functions as a single argument.
class CommonFields : public CommonFieldsGenerationInfo {
 public:
  explicit CommonFields(soo_tag_t) : capacity_(SooCapacity()), size_(0) {}
  explicit CommonFields(full_soo_tag_t)
      : capacity_(SooCapacity()), size_(size_t{1} << HasInfozShift()) {}
  explicit CommonFields(non_soo_tag_t)
      : capacity_(0), size_(0), heap_or_soo_(EmptyGroup()) {}
  // For use in swapping.
  explicit CommonFields(uninitialized_tag_t) {}

  // Not copyable
  CommonFields(const CommonFields&) = delete;
  CommonFields& operator=(const CommonFields&) = delete;

  // Movable
  CommonFields(CommonFields&& that) = default;
  CommonFields& operator=(CommonFields&&) = default;

  template <bool kSooEnabled>
  static CommonFields CreateDefault() {
    return kSooEnabled ? CommonFields{soo_tag_t{}}
                       : CommonFields{non_soo_tag_t{}};
  }

  // The inline data for SOO is written on top of control_/slots_.
  const void* soo_data() const { return heap_or_soo_.get_soo_data(); }
  void* soo_data() { return heap_or_soo_.get_soo_data(); }

  HeapOrSoo heap_or_soo() const { return heap_or_soo_; }
  const HeapOrSoo& heap_or_soo_ref() const { return heap_or_soo_; }

  ctrl_t* control() const { return heap_or_soo_.control(); }
  void set_control(ctrl_t* c) { heap_or_soo_.control() = c; }
  void* backing_array_start() const {
    // growth_info (and maybe infoz) is stored before control bytes.
    ABSL_SWISSTABLE_ASSERT(
        reinterpret_cast<uintptr_t>(control()) % alignof(size_t) == 0);
    return control() - ControlOffset(has_infoz());
  }

  // Note: we can't use slots() because Qt defines "slots" as a macro.
  void* slot_array() const { return heap_or_soo_.slot_array().get(); }
  MaybeInitializedPtr slots_union() const { return heap_or_soo_.slot_array(); }
  void set_slots(void* s) { heap_or_soo_.slot_array().set(s); }

  // The number of filled slots.
  size_t size() const { return size_ >> HasInfozShift(); }
  void set_size(size_t s) {
    size_ = (s << HasInfozShift()) | (size_ & HasInfozMask());
  }
  void set_empty_soo() {
    AssertInSooMode();
    size_ = 0;
  }
  void set_full_soo() {
    AssertInSooMode();
    size_ = size_t{1} << HasInfozShift();
  }
  void increment_size() {
    ABSL_SWISSTABLE_ASSERT(size() < capacity());
    size_ += size_t{1} << HasInfozShift();
  }
  void decrement_size() {
    ABSL_SWISSTABLE_ASSERT(size() > 0);
    size_ -= size_t{1} << HasInfozShift();
  }

  // The total number of available slots.
  size_t capacity() const { return capacity_; }
  void set_capacity(size_t c) {
    // We allow setting above the max valid capacity for debugging purposes.
    ABSL_SWISSTABLE_ASSERT(c == 0 || IsValidCapacity(c) ||
                           c > kAboveMaxValidCapacity);
    capacity_ = c;
  }

  // The number of slots we can still fill without needing to rehash.
  // This is stored in the heap allocation before the control bytes.
  // TODO(b/289225379): experiment with moving growth_info back inline to
  // increase room for SOO.
  size_t growth_left() const { return growth_info().GetGrowthLeft(); }

  GrowthInfo& growth_info() {
    auto* gl_ptr = reinterpret_cast<GrowthInfo*>(control()) - 1;
    ABSL_SWISSTABLE_ASSERT(
        reinterpret_cast<uintptr_t>(gl_ptr) % alignof(GrowthInfo) == 0);
    return *gl_ptr;
  }
  GrowthInfo growth_info() const {
    return const_cast<CommonFields*>(this)->growth_info();
  }

  bool has_infoz() const {
    return ABSL_PREDICT_FALSE((size_ & HasInfozMask()) != 0);
  }
  void set_has_infoz(bool has_infoz) {
    size_ = (size() << HasInfozShift()) | static_cast<size_t>(has_infoz);
  }

  HashtablezInfoHandle infoz() {
    return has_infoz()
               ? *reinterpret_cast<HashtablezInfoHandle*>(backing_array_start())
               : HashtablezInfoHandle();
  }
  void set_infoz(HashtablezInfoHandle infoz) {
    ABSL_SWISSTABLE_ASSERT(has_infoz());
    *reinterpret_cast<HashtablezInfoHandle*>(backing_array_start()) = infoz;
  }

  bool should_rehash_for_bug_detection_on_insert() const {
    return CommonFieldsGenerationInfo::
        should_rehash_for_bug_detection_on_insert(control(), capacity());
  }
  bool should_rehash_for_bug_detection_on_move() const {
    return CommonFieldsGenerationInfo::should_rehash_for_bug_detection_on_move(
        control(), capacity());
  }
  void reset_reserved_growth(size_t reservation) {
    CommonFieldsGenerationInfo::reset_reserved_growth(reservation, size());
  }

  // The size of the backing array allocation.
  size_t alloc_size(size_t slot_size, size_t slot_align) const {
    return RawHashSetLayout(capacity(), slot_align, has_infoz())
        .alloc_size(slot_size);
  }

  // Move fields other than heap_or_soo_.
  void move_non_heap_or_soo_fields(CommonFields& that) {
    static_cast<CommonFieldsGenerationInfo&>(*this) =
        std::move(static_cast<CommonFieldsGenerationInfo&>(that));
    capacity_ = that.capacity_;
    size_ = that.size_;
  }

  // Returns the number of control bytes set to kDeleted. For testing only.
  size_t TombstonesCount() const {
    return static_cast<size_t>(
        std::count(control(), control() + capacity(), ctrl_t::kDeleted));
  }

  // Helper to enable sanitizer mode validation to protect against reentrant
  // calls during element constructor/destructor.
  template <typename F>
  void RunWithReentrancyGuard(F f) {
#ifdef NDEBUG
    f();
    return;
#endif
    const size_t cap = capacity();
    set_capacity(InvalidCapacity::kReentrance);
    f();
    set_capacity(cap);
  }

 private:
  // We store the has_infoz bit in the lowest bit of size_.
  static constexpr size_t HasInfozShift() { return 1; }
  static constexpr size_t HasInfozMask() {
    return (size_t{1} << HasInfozShift()) - 1;
  }

  // We can't assert that SOO is enabled because we don't have SooEnabled(), but
  // we assert what we can.
  void AssertInSooMode() const {
    ABSL_SWISSTABLE_ASSERT(capacity() == SooCapacity());
    ABSL_SWISSTABLE_ASSERT(!has_infoz());
  }

  // The number of slots in the backing array. This is always 2^N-1 for an
  // integer N. NOTE: we tried experimenting with compressing the capacity and
  // storing it together with size_: (a) using 6 bits to store the corresponding
  // power (N in 2^N-1), and (b) storing 2^N as the most significant bit of
  // size_ and storing size in the low bits. Both of these experiments were
  // regressions, presumably because we need capacity to do find operations.
  size_t capacity_;

  // The size and also has one bit that stores whether we have infoz.
  // TODO(b/289225379): we could put size_ into HeapOrSoo and make capacity_
  // encode the size in SOO case. We would be making size()/capacity() more
  // expensive in order to have more SOO space.
  size_t size_;

  // Either the control/slots pointers or the SOO slot.
  HeapOrSoo heap_or_soo_;
};

template <class Policy, class Hash, class Eq, class Alloc>
class raw_hash_set;

// Returns the next valid capacity after `n`.
inline size_t NextCapacity(size_t n) {
  ABSL_SWISSTABLE_ASSERT(IsValidCapacity(n) || n == 0);
  return n * 2 + 1;
}

// Applies the following mapping to every byte in the control array:
//   * kDeleted -> kEmpty
//   * kEmpty -> kEmpty
//   * _ -> kDeleted
// PRECONDITION:
//   IsValidCapacity(capacity)
//   ctrl[capacity] == ctrl_t::kSentinel
//   ctrl[i] != ctrl_t::kSentinel for all i < capacity
void ConvertDeletedToEmptyAndFullToDeleted(ctrl_t* ctrl, size_t capacity);

// Converts `n` into the next valid capacity, per `IsValidCapacity`.
inline size_t NormalizeCapacity(size_t n) {
  return n ? ~size_t{} >> countl_zero(n) : 1;
}

template <size_t kSlotSize>
size_t MaxValidCapacity() {
  return NormalizeCapacity((std::numeric_limits<size_t>::max)() / 4 /
                           kSlotSize);
}

// Use a non-inlined function to avoid code bloat.
[[noreturn]] void HashTableSizeOverflow();

// General notes on capacity/growth methods below:
// - We use 7/8th as maximum load factor. For 16-wide groups, that gives an
//   average of two empty slots per group.
// - For (capacity+1) >= Group::kWidth, growth is 7/8*capacity.
// - For (capacity+1) < Group::kWidth, growth == capacity. In this case, we
//   never need to probe (the whole table fits in one group) so we don't need a
//   load factor less than 1.

// Given `capacity`, applies the load factor; i.e., it returns the maximum
// number of values we should put into the table before a resizing rehash.
inline size_t CapacityToGrowth(size_t capacity) {
  ABSL_SWISSTABLE_ASSERT(IsValidCapacity(capacity));
  // `capacity*7/8`
  if (Group::kWidth == 8 && capacity == 7) {
    // x-x/8 does not work when x==7.
    return 6;
  }
  return capacity - capacity / 8;
}

// Given `growth`, "unapplies" the load factor to find how large the capacity
// should be to stay within the load factor.
//
// This might not be a valid capacity and `NormalizeCapacity()` should be
// called on this.
inline size_t GrowthToLowerboundCapacity(size_t growth) {
  // `growth*8/7`
  if (Group::kWidth == 8 && growth == 7) {
    // x+(x-1)/7 does not work when x==7.
    return 8;
  }
  return growth + static_cast<size_t>((static_cast<int64_t>(growth) - 1) / 7);
}

template <class InputIter>
size_t SelectBucketCountForIterRange(InputIter first, InputIter last,
                                     size_t bucket_count) {
  if (bucket_count != 0) {
    return bucket_count;
  }
  using InputIterCategory =
      typename std::iterator_traits<InputIter>::iterator_category;
  if (std::is_base_of<std::random_access_iterator_tag,
                      InputIterCategory>::value) {
    return GrowthToLowerboundCapacity(
        static_cast<size_t>(std::distance(first, last)));
  }
  return 0;
}

constexpr bool SwisstableDebugEnabled() {
#if defined(ABSL_SWISSTABLE_ENABLE_GENERATIONS) || \
    ABSL_OPTION_HARDENED == 1 || !defined(NDEBUG)
  return true;
#else
  return false;
#endif
}

inline void AssertIsFull(const ctrl_t* ctrl, GenerationType generation,
                         const GenerationType* generation_ptr,
                         const char* operation) {
  if (!SwisstableDebugEnabled()) return;
  // `SwisstableDebugEnabled()` is also true for release builds with hardening
  // enabled. To minimize their impact in those builds:
  // - use `ABSL_PREDICT_FALSE()` to provide a compiler hint for code layout
  // - use `ABSL_RAW_LOG()` with a format string to reduce code size and improve
  //   the chances that the hot paths will be inlined.
  if (ABSL_PREDICT_FALSE(ctrl == nullptr)) {
    ABSL_RAW_LOG(FATAL, "%s called on end() iterator.", operation);
  }
  if (ABSL_PREDICT_FALSE(ctrl == EmptyGroup())) {
    ABSL_RAW_LOG(FATAL, "%s called on default-constructed iterator.",
                 operation);
  }
  if (SwisstableGenerationsEnabled()) {
    if (ABSL_PREDICT_FALSE(generation != *generation_ptr)) {
      ABSL_RAW_LOG(FATAL,
                   "%s called on invalid iterator. The table could have "
                   "rehashed or moved since this iterator was initialized.",
                   operation);
    }
    if (ABSL_PREDICT_FALSE(!IsFull(*ctrl))) {
      ABSL_RAW_LOG(
          FATAL,
          "%s called on invalid iterator. The element was likely erased.",
          operation);
    }
  } else {
    if (ABSL_PREDICT_FALSE(!IsFull(*ctrl))) {
      ABSL_RAW_LOG(
          FATAL,
          "%s called on invalid iterator. The element might have been erased "
          "or the table might have rehashed. Consider running with "
          "--config=asan to diagnose rehashing issues.",
          operation);
    }
  }
}

// Note that for comparisons, null/end iterators are valid.
inline void AssertIsValidForComparison(const ctrl_t* ctrl,
                                       GenerationType generation,
                                       const GenerationType* generation_ptr) {
  if (!SwisstableDebugEnabled()) return;
  const bool ctrl_is_valid_for_comparison =
      ctrl == nullptr || ctrl == EmptyGroup() || IsFull(*ctrl);
  if (SwisstableGenerationsEnabled()) {
    if (ABSL_PREDICT_FALSE(generation != *generation_ptr)) {
      ABSL_RAW_LOG(FATAL,
                   "Invalid iterator comparison. The table could have rehashed "
                   "or moved since this iterator was initialized.");
    }
    if (ABSL_PREDICT_FALSE(!ctrl_is_valid_for_comparison)) {
      ABSL_RAW_LOG(
          FATAL, "Invalid iterator comparison. The element was likely erased.");
    }
  } else {
    ABSL_HARDENING_ASSERT(
        ctrl_is_valid_for_comparison &&
        "Invalid iterator comparison. The element might have been erased or "
        "the table might have rehashed. Consider running with --config=asan to "
        "diagnose rehashing issues.");
  }
}

// If the two iterators come from the same container, then their pointers will
// interleave such that ctrl_a <= ctrl_b < slot_a <= slot_b or vice/versa.
// Note: we take slots by reference so that it's not UB if they're uninitialized
// as long as we don't read them (when ctrl is null).
inline bool AreItersFromSameContainer(const ctrl_t* ctrl_a,
                                      const ctrl_t* ctrl_b,
                                      const void* const& slot_a,
                                      const void* const& slot_b) {
  // If either control byte is null, then we can't tell.
  if (ctrl_a == nullptr || ctrl_b == nullptr) return true;
  const bool a_is_soo = IsSooControl(ctrl_a);
  if (a_is_soo != IsSooControl(ctrl_b)) return false;
  if (a_is_soo) return slot_a == slot_b;

  const void* low_slot = slot_a;
  const void* hi_slot = slot_b;
  if (ctrl_a > ctrl_b) {
    std::swap(ctrl_a, ctrl_b);
    std::swap(low_slot, hi_slot);
  }
  return ctrl_b < low_slot && low_slot <= hi_slot;
}

// Asserts that two iterators come from the same container.
// Note: we take slots by reference so that it's not UB if they're uninitialized
// as long as we don't read them (when ctrl is null).
inline void AssertSameContainer(const ctrl_t* ctrl_a, const ctrl_t* ctrl_b,
                                const void* const& slot_a,
                                const void* const& slot_b,
                                const GenerationType* generation_ptr_a,
                                const GenerationType* generation_ptr_b) {
  if (!SwisstableDebugEnabled()) return;
  // `SwisstableDebugEnabled()` is also true for release builds with hardening
  // enabled. To minimize their impact in those builds:
  // - use `ABSL_PREDICT_FALSE()` to provide a compiler hint for code layout
  // - use `ABSL_RAW_LOG()` with a format string to reduce code size and improve
  //   the chances that the hot paths will be inlined.

  // fail_if(is_invalid, message) crashes when is_invalid is true and provides
  // an error message based on `message`.
  const auto fail_if = [](bool is_invalid, const char* message) {
    if (ABSL_PREDICT_FALSE(is_invalid)) {
      ABSL_RAW_LOG(FATAL, "Invalid iterator comparison. %s", message);
    }
  };

  const bool a_is_default = ctrl_a == EmptyGroup();
  const bool b_is_default = ctrl_b == EmptyGroup();
  if (a_is_default && b_is_default) return;
  fail_if(a_is_default != b_is_default,
          "Comparing default-constructed hashtable iterator with a "
          "non-default-constructed hashtable iterator.");

  if (SwisstableGenerationsEnabled()) {
    if (ABSL_PREDICT_TRUE(generation_ptr_a == generation_ptr_b)) return;
    // Users don't need to know whether the tables are SOO so don't mention SOO
    // in the debug message.
    const bool a_is_soo = IsSooControl(ctrl_a);
    const bool b_is_soo = IsSooControl(ctrl_b);
    fail_if(a_is_soo != b_is_soo || (a_is_soo && b_is_soo),
            "Comparing iterators from different hashtables.");

    const bool a_is_empty = IsEmptyGeneration(generation_ptr_a);
    const bool b_is_empty = IsEmptyGeneration(generation_ptr_b);
    fail_if(a_is_empty != b_is_empty,
            "Comparing an iterator from an empty hashtable with an iterator "
            "from a non-empty hashtable.");
    fail_if(a_is_empty && b_is_empty,
            "Comparing iterators from different empty hashtables.");

    const bool a_is_end = ctrl_a == nullptr;
    const bool b_is_end = ctrl_b == nullptr;
    fail_if(a_is_end || b_is_end,
            "Comparing iterator with an end() iterator from a different "
            "hashtable.");
    fail_if(true, "Comparing non-end() iterators from different hashtables.");
  } else {
    ABSL_HARDENING_ASSERT_SLOW(
        AreItersFromSameContainer(ctrl_a, ctrl_b, slot_a, slot_b) &&
        "Invalid iterator comparison. The iterators may be from different "
        "containers or the container might have rehashed or moved. Consider "
        "running with --config=asan to diagnose issues.");
  }
}

struct FindInfo {
  size_t offset;
  size_t probe_length;
};

// Whether a table is "small". A small table fits entirely into a probing
// group, i.e., has a capacity < `Group::kWidth`.
//
// In small mode we are able to use the whole capacity. The extra control
// bytes give us at least one "empty" control byte to stop the iteration.
// This is important to make 1 a valid capacity.
//
// In small mode only the first `capacity` control bytes after the sentinel
// are valid. The rest contain dummy ctrl_t::kEmpty values that do not
// represent a real slot. This is important to take into account on
// `find_first_non_full()`, where we never try
// `ShouldInsertBackwards()` for small tables.
inline bool is_small(size_t capacity) { return capacity < Group::kWidth - 1; }

// Whether a table fits entirely into a probing group.
// Arbitrary order of elements in such tables is correct.
inline bool is_single_group(size_t capacity) {
  return capacity <= Group::kWidth;
}

// Begins a probing operation on `common.control`, using `hash`.
inline probe_seq<Group::kWidth> probe(const ctrl_t* ctrl, const size_t capacity,
                                      size_t hash) {
  return probe_seq<Group::kWidth>(H1(hash, ctrl), capacity);
}
inline probe_seq<Group::kWidth> probe(const CommonFields& common, size_t hash) {
  return probe(common.control(), common.capacity(), hash);
}

// Probes an array of control bits using a probe sequence derived from `hash`,
// and returns the offset corresponding to the first deleted or empty slot.
//
// Behavior when the entire table is full is undefined.
//
// NOTE: this function must work with tables having both empty and deleted
// slots in the same group. Such tables appear during `erase()`.
template <typename = void>
inline FindInfo find_first_non_full(const CommonFields& common, size_t hash) {
  auto seq = probe(common, hash);
  const ctrl_t* ctrl = common.control();
  if (IsEmptyOrDeleted(ctrl[seq.offset()]) &&
      !ShouldInsertBackwards(common.capacity(), hash, ctrl)) {
    return {seq.offset(), /*probe_length=*/0};
  }
  while (true) {
    GroupFullEmptyOrDeleted g{ctrl + seq.offset()};
    auto mask = g.MaskEmptyOrDeleted();
    if (mask) {
      return {
          seq.offset(GetInsertionOffset(mask, common.capacity(), hash, ctrl)),
          seq.index()};
    }
    seq.next();
    ABSL_SWISSTABLE_ASSERT(seq.index() <= common.capacity() && "full table!");
  }
}

// Extern template for inline function keep possibility of inlining.
// When compiler decided to not inline, no symbols will be added to the
// corresponding translation unit.
extern template FindInfo find_first_non_full(const CommonFields&, size_t);

// Non-inlined version of find_first_non_full for use in less
// performance critical routines.
FindInfo find_first_non_full_outofline(const CommonFields&, size_t);

inline void ResetGrowthLeft(CommonFields& common) {
  common.growth_info().InitGrowthLeftNoDeleted(
      CapacityToGrowth(common.capacity()) - common.size());
}

// Sets `ctrl` to `{kEmpty, kSentinel, ..., kEmpty}`, marking the entire
// array as marked as empty.
inline void ResetCtrl(CommonFields& common, size_t slot_size) {
  const size_t capacity = common.capacity();
  ctrl_t* ctrl = common.control();
  std::memset(ctrl, static_cast<int8_t>(ctrl_t::kEmpty),
              capacity + 1 + NumClonedBytes());
  ctrl[capacity] = ctrl_t::kSentinel;
  SanitizerPoisonMemoryRegion(common.slot_array(), slot_size * capacity);
}

// Sets sanitizer poisoning for slot corresponding to control byte being set.
inline void DoSanitizeOnSetCtrl(const CommonFields& c, size_t i, ctrl_t h,
                                size_t slot_size) {
  ABSL_SWISSTABLE_ASSERT(i < c.capacity());
  auto* slot_i = static_cast<const char*>(c.slot_array()) + i * slot_size;
  if (IsFull(h)) {
    SanitizerUnpoisonMemoryRegion(slot_i, slot_size);
  } else {
    SanitizerPoisonMemoryRegion(slot_i, slot_size);
  }
}

// Sets `ctrl[i]` to `h`.
//
// Unlike setting it directly, this function will perform bounds checks and
// mirror the value to the cloned tail if necessary.
inline void SetCtrl(const CommonFields& c, size_t i, ctrl_t h,
                    size_t slot_size) {
  DoSanitizeOnSetCtrl(c, i, h, slot_size);
  ctrl_t* ctrl = c.control();
  ctrl[i] = h;
  ctrl[((i - NumClonedBytes()) & c.capacity()) +
       (NumClonedBytes() & c.capacity())] = h;
}
// Overload for setting to an occupied `h2_t` rather than a special `ctrl_t`.
inline void SetCtrl(const CommonFields& c, size_t i, h2_t h, size_t slot_size) {
  SetCtrl(c, i, static_cast<ctrl_t>(h), slot_size);
}

// Like SetCtrl, but in a single group table, we can save some operations when
// setting the cloned control byte.
inline void SetCtrlInSingleGroupTable(const CommonFields& c, size_t i, ctrl_t h,
                                      size_t slot_size) {
  ABSL_SWISSTABLE_ASSERT(is_single_group(c.capacity()));
  DoSanitizeOnSetCtrl(c, i, h, slot_size);
  ctrl_t* ctrl = c.control();
  ctrl[i] = h;
  ctrl[i + c.capacity() + 1] = h;
}
// Overload for setting to an occupied `h2_t` rather than a special `ctrl_t`.
inline void SetCtrlInSingleGroupTable(const CommonFields& c, size_t i, h2_t h,
                                      size_t slot_size) {
  SetCtrlInSingleGroupTable(c, i, static_cast<ctrl_t>(h), slot_size);
}

// growth_info (which is a size_t) is stored with the backing array.
constexpr size_t BackingArrayAlignment(size_t align_of_slot) {
  return (std::max)(align_of_slot, alignof(GrowthInfo));
}

// Returns the address of the ith slot in slots where each slot occupies
// slot_size.
inline void* SlotAddress(void* slot_array, size_t slot, size_t slot_size) {
  return static_cast<void*>(static_cast<char*>(slot_array) +
                            (slot * slot_size));
}

// Iterates over all full slots and calls `cb(const ctrl_t*, SlotType*)`.
// No insertion to the table allowed during Callback call.
// Erasure is allowed only for the element passed to the callback.
template <class SlotType, class Callback>
ABSL_ATTRIBUTE_ALWAYS_INLINE inline void IterateOverFullSlots(
    const CommonFields& c, SlotType* slot, Callback cb) {
  const size_t cap = c.capacity();
  const ctrl_t* ctrl = c.control();
  if (is_small(cap)) {
    // Mirrored/cloned control bytes in small table are also located in the
    // first group (starting from position 0). We are taking group from position
    // `capacity` in order to avoid duplicates.

    // Small tables capacity fits into portable group, where
    // GroupPortableImpl::MaskFull is more efficient for the
    // capacity <= GroupPortableImpl::kWidth.
    ABSL_SWISSTABLE_ASSERT(cap <= GroupPortableImpl::kWidth &&
                           "unexpectedly large small capacity");
    static_assert(Group::kWidth >= GroupPortableImpl::kWidth,
                  "unexpected group width");
    // Group starts from kSentinel slot, so indices in the mask will
    // be increased by 1.
    const auto mask = GroupPortableImpl(ctrl + cap).MaskFull();
    --ctrl;
    --slot;
    for (uint32_t i : mask) {
      cb(ctrl + i, slot + i);
    }
    return;
  }
  size_t remaining = c.size();
  ABSL_ATTRIBUTE_UNUSED const size_t original_size_for_assert = remaining;
  while (remaining != 0) {
    for (uint32_t i : GroupFullEmptyOrDeleted(ctrl).MaskFull()) {
      ABSL_SWISSTABLE_ASSERT(IsFull(ctrl[i]) &&
                             "hash table was modified unexpectedly");
      cb(ctrl + i, slot + i);
      --remaining;
    }
    ctrl += Group::kWidth;
    slot += Group::kWidth;
    ABSL_SWISSTABLE_ASSERT(
        (remaining == 0 || *(ctrl - 1) != ctrl_t::kSentinel) &&
        "hash table was modified unexpectedly");
  }
  // NOTE: erasure of the current element is allowed in callback for
  // absl::erase_if specialization. So we use `>=`.
  ABSL_SWISSTABLE_ASSERT(original_size_for_assert >= c.size() &&
                         "hash table was modified unexpectedly");
}

template <typename CharAlloc>
constexpr bool ShouldSampleHashtablezInfo() {
  // Folks with custom allocators often make unwarranted assumptions about the
  // behavior of their classes vis-a-vis trivial destructability and what
  // calls they will or won't make.  Avoid sampling for people with custom
  // allocators to get us out of this mess.  This is not a hard guarantee but
  // a workaround while we plan the exact guarantee we want to provide.
  return std::is_same<CharAlloc, std::allocator<char>>::value;
}

template <bool kSooEnabled>
HashtablezInfoHandle SampleHashtablezInfo(size_t sizeof_slot, size_t sizeof_key,
                                          size_t sizeof_value,
                                          size_t old_capacity, bool was_soo,
                                          HashtablezInfoHandle forced_infoz,
                                          CommonFields& c) {
  if (forced_infoz.IsSampled()) return forced_infoz;
  // In SOO, we sample on the first insertion so if this is an empty SOO case
  // (e.g. when reserve is called), then we still need to sample.
  if (kSooEnabled && was_soo && c.size() == 0) {
    return Sample(sizeof_slot, sizeof_key, sizeof_value, SooCapacity());
  }
  // For non-SOO cases, we sample whenever the capacity is increasing from zero
  // to non-zero.
  if (!kSooEnabled && old_capacity == 0) {
    return Sample(sizeof_slot, sizeof_key, sizeof_value, 0);
  }
  return c.infoz();
}

// Helper class to perform resize of the hash set.
//
// It contains special optimizations for small group resizes.
// See GrowIntoSingleGroupShuffleControlBytes for details.
class HashSetResizeHelper {
 public:
  explicit HashSetResizeHelper(CommonFields& c, bool was_soo, bool had_soo_slot,
                               HashtablezInfoHandle forced_infoz)
      : old_capacity_(c.capacity()),
        had_infoz_(c.has_infoz()),
        was_soo_(was_soo),
        had_soo_slot_(had_soo_slot),
        forced_infoz_(forced_infoz) {}

  // Optimized for small groups version of `find_first_non_full`.
  // Beneficial only right after calling `raw_hash_set::resize`.
  // It is safe to call in case capacity is big or was not changed, but there
  // will be no performance benefit.
  // It has implicit assumption that `resize` will call
  // `GrowSizeIntoSingleGroup*` in case `IsGrowingIntoSingleGroupApplicable`.
  // Falls back to `find_first_non_full` in case of big groups.
  static FindInfo FindFirstNonFullAfterResize(const CommonFields& c,
                                              size_t old_capacity, size_t hash);

  HeapOrSoo& old_heap_or_soo() { return old_heap_or_soo_; }
  void* old_soo_data() { return old_heap_or_soo_.get_soo_data(); }
  ctrl_t* old_ctrl() const {
    ABSL_SWISSTABLE_ASSERT(!was_soo_);
    return old_heap_or_soo_.control();
  }
  void* old_slots() const {
    ABSL_SWISSTABLE_ASSERT(!was_soo_);
    return old_heap_or_soo_.slot_array().get();
  }
  size_t old_capacity() const { return old_capacity_; }

  // Returns the index of the SOO slot when growing from SOO to non-SOO in a
  // single group. See also InitControlBytesAfterSoo(). It's important to use
  // index 1 so that when resizing from capacity 1 to 3, we can still have
  // random iteration order between the first two inserted elements.
  // I.e. it allows inserting the second element at either index 0 or 2.
  static size_t SooSlotIndex() { return 1; }

  // Allocates a backing array for the hashtable.
  // Reads `capacity` and updates all other fields based on the result of
  // the allocation.
  //
  // It also may do the following actions:
  // 1. initialize control bytes
  // 2. initialize slots
  // 3. deallocate old slots.
  //
  // We are bundling a lot of functionality
  // in one ABSL_ATTRIBUTE_NOINLINE function in order to minimize binary code
  // duplication in raw_hash_set<>::resize.
  //
  // `c.capacity()` must be nonzero.
  // POSTCONDITIONS:
  //  1. CommonFields is initialized.
  //
  //  if IsGrowingIntoSingleGroupApplicable && TransferUsesMemcpy
  //    Both control bytes and slots are fully initialized.
  //    old_slots are deallocated.
  //    infoz.RecordRehash is called.
  //
  //  if IsGrowingIntoSingleGroupApplicable && !TransferUsesMemcpy
  //    Control bytes are fully initialized.
  //    infoz.RecordRehash is called.
  //    GrowSizeIntoSingleGroup must be called to finish slots initialization.
  //
  //  if !IsGrowingIntoSingleGroupApplicable
  //    Control bytes are initialized to empty table via ResetCtrl.
  //    raw_hash_set<>::resize must insert elements regularly.
  //    infoz.RecordRehash is called if old_capacity == 0.
  //
  //  Returns IsGrowingIntoSingleGroupApplicable result to avoid recomputation.
  template <typename Alloc, size_t SizeOfSlot, bool TransferUsesMemcpy,
            bool SooEnabled, size_t AlignOfSlot>
  ABSL_ATTRIBUTE_NOINLINE bool InitializeSlots(CommonFields& c, Alloc alloc,
                                               ctrl_t soo_slot_h2,
                                               size_t key_size,
                                               size_t value_size) {
    ABSL_SWISSTABLE_ASSERT(c.capacity());
    HashtablezInfoHandle infoz =
        ShouldSampleHashtablezInfo<Alloc>()
            ? SampleHashtablezInfo<SooEnabled>(SizeOfSlot, key_size, value_size,
                                               old_capacity_, was_soo_,
                                               forced_infoz_, c)
            : HashtablezInfoHandle{};

    const bool has_infoz = infoz.IsSampled();
    RawHashSetLayout layout(c.capacity(), AlignOfSlot, has_infoz);
    char* mem = static_cast<char*>(Allocate<BackingArrayAlignment(AlignOfSlot)>(
        &alloc, layout.alloc_size(SizeOfSlot)));
    const GenerationType old_generation = c.generation();
    c.set_generation_ptr(
        reinterpret_cast<GenerationType*>(mem + layout.generation_offset()));
    c.set_generation(NextGeneration(old_generation));
    c.set_control(reinterpret_cast<ctrl_t*>(mem + layout.control_offset()));
    c.set_slots(mem + layout.slot_offset());
    ResetGrowthLeft(c);

    const bool grow_single_group =
        IsGrowingIntoSingleGroupApplicable(old_capacity_, layout.capacity());
    if (SooEnabled && was_soo_ && grow_single_group) {
      InitControlBytesAfterSoo(c.control(), soo_slot_h2, layout.capacity());
      if (TransferUsesMemcpy && had_soo_slot_) {
        TransferSlotAfterSoo(c, SizeOfSlot);
      }
      // SooEnabled implies that old_capacity_ != 0.
    } else if ((SooEnabled || old_capacity_ != 0) && grow_single_group) {
      if (TransferUsesMemcpy) {
        GrowSizeIntoSingleGroupTransferable(c, SizeOfSlot);
        DeallocateOld<AlignOfSlot>(alloc, SizeOfSlot);
      } else {
        GrowIntoSingleGroupShuffleControlBytes(c.control(), layout.capacity());
      }
    } else {
      ResetCtrl(c, SizeOfSlot);
    }

    c.set_has_infoz(has_infoz);
    if (has_infoz) {
      infoz.RecordStorageChanged(c.size(), layout.capacity());
      if ((SooEnabled && was_soo_) || grow_single_group || old_capacity_ == 0) {
        infoz.RecordRehash(0);
      }
      c.set_infoz(infoz);
    }
    return grow_single_group;
  }

  // Relocates slots into new single group consistent with
  // GrowIntoSingleGroupShuffleControlBytes.
  //
  // PRECONDITIONS:
  // 1. GrowIntoSingleGroupShuffleControlBytes was already called.
  template <class PolicyTraits, class Alloc>
  void GrowSizeIntoSingleGroup(CommonFields& c, Alloc& alloc_ref) {
    ABSL_SWISSTABLE_ASSERT(old_capacity_ < Group::kWidth / 2);
    ABSL_SWISSTABLE_ASSERT(
        IsGrowingIntoSingleGroupApplicable(old_capacity_, c.capacity()));
    using slot_type = typename PolicyTraits::slot_type;
    ABSL_SWISSTABLE_ASSERT(is_single_group(c.capacity()));

    auto* new_slots = static_cast<slot_type*>(c.slot_array()) + 1;
    auto* old_slots_ptr = static_cast<slot_type*>(old_slots());
    auto* old_ctrl_ptr = old_ctrl();

    for (size_t i = 0; i < old_capacity_; ++i, ++new_slots) {
      if (IsFull(old_ctrl_ptr[i])) {
        SanitizerUnpoisonMemoryRegion(new_slots, sizeof(slot_type));
        PolicyTraits::transfer(&alloc_ref, new_slots, old_slots_ptr + i);
      }
    }
    PoisonSingleGroupEmptySlots(c, sizeof(slot_type));
  }

  // Deallocates old backing array.
  template <size_t AlignOfSlot, class CharAlloc>
  void DeallocateOld(CharAlloc alloc_ref, size_t slot_size) {
    SanitizerUnpoisonMemoryRegion(old_slots(), slot_size * old_capacity_);
    auto layout = RawHashSetLayout(old_capacity_, AlignOfSlot, had_infoz_);
    Deallocate<BackingArrayAlignment(AlignOfSlot)>(
        &alloc_ref, old_ctrl() - layout.control_offset(),
        layout.alloc_size(slot_size));
  }

 private:
  // Returns true if `GrowSizeIntoSingleGroup` can be used for resizing.
  static bool IsGrowingIntoSingleGroupApplicable(size_t old_capacity,
                                                 size_t new_capacity) {
    // NOTE that `old_capacity < new_capacity` in order to have
    // `old_capacity < Group::kWidth / 2` to make faster copies of 8 bytes.
    return is_single_group(new_capacity) && old_capacity < new_capacity;
  }

  // Relocates control bytes and slots into new single group for
  // transferable objects.
  // Must be called only if IsGrowingIntoSingleGroupApplicable returned true.
  void GrowSizeIntoSingleGroupTransferable(CommonFields& c, size_t slot_size);

  // If there was an SOO slot and slots are transferable, transfers the SOO slot
  // into the new heap allocation. Must be called only if
  // IsGrowingIntoSingleGroupApplicable returned true.
  void TransferSlotAfterSoo(CommonFields& c, size_t slot_size);

  // Shuffle control bits deterministically to the next capacity.
  // Returns offset for newly added element with given hash.
  //
  // PRECONDITIONs:
  // 1. new_ctrl is allocated for new_capacity,
  //    but not initialized.
  // 2. new_capacity is a single group.
  // 3. old_capacity > 0.
  //
  // All elements are transferred into the first `old_capacity + 1` positions
  // of the new_ctrl. Elements are shifted by 1 in order to keep a space at the
  // beginning for the new element.
  // Position of the new added element will be based on `H1` and is not
  // deterministic.
  //
  // Examples:
  // S = kSentinel, E = kEmpty
  //
  // old_ctrl = 0SEEEEEEE...
  // new_ctrl = E0ESE0EEE...
  //
  // old_ctrl = 012S012EEEEEEEEE...
  // new_ctrl = E012EEESE012EEE...
  //
  // old_ctrl = 0123456S0123456EEEEEEEEEEE...
  // new_ctrl = E0123456EEEEEESE0123456EEE...
  void GrowIntoSingleGroupShuffleControlBytes(ctrl_t* new_ctrl,
                                              size_t new_capacity) const;

  // If the table was SOO, initializes new control bytes. `h2` is the control
  // byte corresponding to the full slot. Must be called only if
  // IsGrowingIntoSingleGroupApplicable returned true.
  // Requires: `had_soo_slot_ || h2 == ctrl_t::kEmpty`.
  void InitControlBytesAfterSoo(ctrl_t* new_ctrl, ctrl_t h2,
                                size_t new_capacity);

  // Shuffle trivially transferable slots in the way consistent with
  // GrowIntoSingleGroupShuffleControlBytes.
  //
  // PRECONDITIONs:
  // 1. old_capacity must be non-zero.
  // 2. new_ctrl is fully initialized using
  //    GrowIntoSingleGroupShuffleControlBytes.
  // 3. new_slots is allocated and *not* poisoned.
  //
  // POSTCONDITIONS:
  // 1. new_slots are transferred from old_slots_ consistent with
  //    GrowIntoSingleGroupShuffleControlBytes.
  // 2. Empty new_slots are *not* poisoned.
  void GrowIntoSingleGroupShuffleTransferableSlots(void* new_slots,
                                                   size_t slot_size) const;

  // Poison empty slots that were transferred using the deterministic algorithm
  // described above.
  // PRECONDITIONs:
  // 1. new_ctrl is fully initialized using
  //    GrowIntoSingleGroupShuffleControlBytes.
  // 2. new_slots is fully initialized consistent with
  //    GrowIntoSingleGroupShuffleControlBytes.
  void PoisonSingleGroupEmptySlots(CommonFields& c, size_t slot_size) const {
    // poison non full items
    for (size_t i = 0; i < c.capacity(); ++i) {
      if (!IsFull(c.control()[i])) {
        SanitizerPoisonMemoryRegion(SlotAddress(c.slot_array(), i, slot_size),
                                    slot_size);
      }
    }
  }

  HeapOrSoo old_heap_or_soo_;
  size_t old_capacity_;
  bool had_infoz_;
  bool was_soo_;
  bool had_soo_slot_;
  // Either null infoz or a pre-sampled forced infoz for SOO tables.
  HashtablezInfoHandle forced_infoz_;
};

inline void PrepareInsertCommon(CommonFields& common) {
  common.increment_size();
  common.maybe_increment_generation_on_insert();
}

// Like prepare_insert, but for the case of inserting into a full SOO table.
size_t PrepareInsertAfterSoo(size_t hash, size_t slot_size,
                             CommonFields& common);

// PolicyFunctions bundles together some information for a particular
// raw_hash_set<T, ...> instantiation. This information is passed to
// type-erased functions that want to do small amounts of type-specific
// work.
struct PolicyFunctions {
  size_t slot_size;

  // Returns the pointer to the hash function stored in the set.
  const void* (*hash_fn)(const CommonFields& common);

  // Returns the hash of the pointed-to slot.
  size_t (*hash_slot)(const void* hash_fn, void* slot);

  // Transfers the contents of src_slot to dst_slot.
  void (*transfer)(void* set, void* dst_slot, void* src_slot);

  // Deallocates the backing store from common.
  void (*dealloc)(CommonFields& common, const PolicyFunctions& policy);

  // Resizes set to the new capacity.
  // Arguments are used as in raw_hash_set::resize_impl.
  void (*resize)(CommonFields& common, size_t new_capacity,
                 HashtablezInfoHandle forced_infoz);
};

// ClearBackingArray clears the backing array, either modifying it in place,
// or creating a new one based on the value of "reuse".
// REQUIRES: c.capacity > 0
void ClearBackingArray(CommonFields& c, const PolicyFunctions& policy,
                       bool reuse, bool soo_enabled);

// Type-erased version of raw_hash_set::erase_meta_only.
void EraseMetaOnly(CommonFields& c, size_t index, size_t slot_size);

// Function to place in PolicyFunctions::dealloc for raw_hash_sets
// that are using std::allocator. This allows us to share the same
// function body for raw_hash_set instantiations that have the
// same slot alignment.
template <size_t AlignOfSlot>
ABSL_ATTRIBUTE_NOINLINE void DeallocateStandard(CommonFields& common,
                                                const PolicyFunctions& policy) {
  // Unpoison before returning the memory to the allocator.
  SanitizerUnpoisonMemoryRegion(common.slot_array(),
                                policy.slot_size * common.capacity());

  std::allocator<char> alloc;
  common.infoz().Unregister();
  Deallocate<BackingArrayAlignment(AlignOfSlot)>(
      &alloc, common.backing_array_start(),
      common.alloc_size(policy.slot_size, AlignOfSlot));
}

// For trivially relocatable types we use memcpy directly. This allows us to
// share the same function body for raw_hash_set instantiations that have the
// same slot size as long as they are relocatable.
template <size_t SizeOfSlot>
ABSL_ATTRIBUTE_NOINLINE void TransferRelocatable(void*, void* dst, void* src) {
  memcpy(dst, src, SizeOfSlot);
}

// Type erased raw_hash_set::get_hash_ref_fn for the empty hash function case.
const void* GetHashRefForEmptyHasher(const CommonFields& common);

// Given the hash of a value not currently in the table and the first empty
// slot in the probe sequence, finds a viable slot index to insert it at.
//
// In case there's no space left, the table can be resized or rehashed
// (for tables with deleted slots, see FindInsertPositionWithGrowthOrRehash).
//
// In the case of absence of deleted slots and positive growth_left, the element
// can be inserted in the provided `target` position.
//
// When the table has deleted slots (according to GrowthInfo), the target
// position will be searched one more time using `find_first_non_full`.
//
// REQUIRES: Table is not SOO.
// REQUIRES: At least one non-full slot available.
// REQUIRES: `target` is a valid empty position to insert.
size_t PrepareInsertNonSoo(CommonFields& common, size_t hash, FindInfo target,
                           const PolicyFunctions& policy);

// A SwissTable.
//
// Policy: a policy defines how to perform different operations on
// the slots of the hashtable (see hash_policy_traits.h for the full interface
// of policy).
//
// Hash: a (possibly polymorphic) functor that hashes keys of the hashtable. The
// functor should accept a key and return size_t as hash. For best performance
// it is important that the hash function provides high entropy across all bits
// of the hash.
//
// Eq: a (possibly polymorphic) functor that compares two keys for equality. It
// should accept two (of possibly different type) keys and return a bool: true
// if they are equal, false if they are not. If two keys compare equal, then
// their hash values as defined by Hash MUST be equal.
//
// Allocator: an Allocator
// [https://en.cppreference.com/w/cpp/named_req/Allocator] with which
// the storage of the hashtable will be allocated and the elements will be
// constructed and destroyed.
template <class Policy, class Hash, class Eq, class Alloc>
class raw_hash_set {
  using PolicyTraits = hash_policy_traits<Policy>;
  using KeyArgImpl =
      KeyArg<IsTransparent<Eq>::value && IsTransparent<Hash>::value>;

 public:
  using init_type = typename PolicyTraits::init_type;
  using key_type = typename PolicyTraits::key_type;
  // TODO(sbenza): Hide slot_type as it is an implementation detail. Needs user
  // code fixes!
  using slot_type = typename PolicyTraits::slot_type;
  using allocator_type = Alloc;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using hasher = Hash;
  using key_equal = Eq;
  using policy_type = Policy;
  using value_type = typename PolicyTraits::value_type;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = typename absl::allocator_traits<
      allocator_type>::template rebind_traits<value_type>::pointer;
  using const_pointer = typename absl::allocator_traits<
      allocator_type>::template rebind_traits<value_type>::const_pointer;

  // Alias used for heterogeneous lookup functions.
  // `key_arg<K>` evaluates to `K` when the functors are transparent and to
  // `key_type` otherwise. It permits template argument deduction on `K` for the
  // transparent case.
  template <class K>
  using key_arg = typename KeyArgImpl::template type<K, key_type>;

 private:
  // TODO(b/289225379): we could add extra SOO space inside raw_hash_set
  // after CommonFields to allow inlining larger slot_types (e.g. std::string),
  // but it's a bit complicated if we want to support incomplete mapped_type in
  // flat_hash_map. We could potentially do this for flat_hash_set and for an
  // allowlist of `mapped_type`s of flat_hash_map that includes e.g. arithmetic
  // types, strings, cords, and pairs/tuples of allowlisted types.
  constexpr static bool SooEnabled() {
    return PolicyTraits::soo_enabled() &&
           sizeof(slot_type) <= sizeof(HeapOrSoo) &&
           alignof(slot_type) <= alignof(HeapOrSoo);
  }

  constexpr static size_t DefaultCapacity() {
    return SooEnabled() ? SooCapacity() : 0;
  }

  // Whether `size` fits in the SOO capacity of this table.
  bool fits_in_soo(size_t size) const {
    return SooEnabled() && size <= SooCapacity();
  }
  // Whether this table is in SOO mode or non-SOO mode.
  bool is_soo() const { return fits_in_soo(capacity()); }
  bool is_full_soo() const { return is_soo() && !empty(); }

  // Give an early error when key_type is not hashable/eq.
  auto KeyTypeCanBeHashed(const Hash& h, const key_type& k) -> decltype(h(k));
  auto KeyTypeCanBeEq(const Eq& eq, const key_type& k) -> decltype(eq(k, k));

  using AllocTraits = absl::allocator_traits<allocator_type>;
  using SlotAlloc = typename absl::allocator_traits<
      allocator_type>::template rebind_alloc<slot_type>;
  // People are often sloppy with the exact type of their allocator (sometimes
  // it has an extra const or is missing the pair, but rebinds made it work
  // anyway).
  using CharAlloc =
      typename absl::allocator_traits<Alloc>::template rebind_alloc<char>;
  using SlotAllocTraits = typename absl::allocator_traits<
      allocator_type>::template rebind_traits<slot_type>;

  static_assert(std::is_lvalue_reference<reference>::value,
                "Policy::element() must return a reference");

  // An enabler for insert(T&&): T must be convertible to init_type or be the
  // same as [cv] value_type [ref].
  template <class T>
  using Insertable = absl::disjunction<
      std::is_same<absl::remove_cvref_t<reference>, absl::remove_cvref_t<T>>,
      std::is_convertible<T, init_type>>;
  template <class T>
  using IsNotBitField = std::is_pointer<T*>;

  // RequiresNotInit is a workaround for gcc prior to 7.1.
  // See https://godbolt.org/g/Y4xsUh.
  template <class T>
  using RequiresNotInit =
      typename std::enable_if<!std::is_same<T, init_type>::value, int>::type;

  template <class... Ts>
  using IsDecomposable = IsDecomposable<void, PolicyTraits, Hash, Eq, Ts...>;

  template <class T>
  using IsDecomposableAndInsertable =
      IsDecomposable<std::enable_if_t<Insertable<T>::value, T>>;

  // Evaluates to true if an assignment from the given type would require the
  // source object to remain alive for the life of the element.
  template <class U>
  using IsLifetimeBoundAssignmentFrom = std::conditional_t<
      policy_trait_element_is_owner<Policy>::value, std::false_type,
      type_traits_internal::IsLifetimeBoundAssignment<init_type, U>>;

 public:
  static_assert(std::is_same<pointer, value_type*>::value,
                "Allocators with custom pointer types are not supported");
  static_assert(std::is_same<const_pointer, const value_type*>::value,
                "Allocators with custom pointer types are not supported");

  class iterator : private HashSetIteratorGenerationInfo {
    friend class raw_hash_set;
    friend struct HashtableFreeFunctionsAccess;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = typename raw_hash_set::value_type;
    using reference =
        absl::conditional_t<PolicyTraits::constant_iterators::value,
                            const value_type&, value_type&>;
    using pointer = absl::remove_reference_t<reference>*;
    using difference_type = typename raw_hash_set::difference_type;

    iterator() {}

    // PRECONDITION: not an end() iterator.
    reference operator*() const {
      AssertIsFull(ctrl_, generation(), generation_ptr(), "operator*()");
      return unchecked_deref();
    }

    // PRECONDITION: not an end() iterator.
    pointer operator->() const {
      AssertIsFull(ctrl_, generation(), generation_ptr(), "operator->");
      return &operator*();
    }

    // PRECONDITION: not an end() iterator.
    iterator& operator++() {
      AssertIsFull(ctrl_, generation(), generation_ptr(), "operator++");
      ++ctrl_;
      ++slot_;
      skip_empty_or_deleted();
      if (ABSL_PREDICT_FALSE(*ctrl_ == ctrl_t::kSentinel)) ctrl_ = nullptr;
      return *this;
    }
    // PRECONDITION: not an end() iterator.
    iterator operator++(int) {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    friend bool operator==(const iterator& a, const iterator& b) {
      AssertIsValidForComparison(a.ctrl_, a.generation(), a.generation_ptr());
      AssertIsValidForComparison(b.ctrl_, b.generation(), b.generation_ptr());
      AssertSameContainer(a.ctrl_, b.ctrl_, a.slot_, b.slot_,
                          a.generation_ptr(), b.generation_ptr());
      return a.ctrl_ == b.ctrl_;
    }
    friend bool operator!=(const iterator& a, const iterator& b) {
      return !(a == b);
    }

   private:
    iterator(ctrl_t* ctrl, slot_type* slot,
             const GenerationType* generation_ptr)
        : HashSetIteratorGenerationInfo(generation_ptr),
          ctrl_(ctrl),
          slot_(slot) {
      // This assumption helps the compiler know that any non-end iterator is
      // not equal to any end iterator.
      ABSL_ASSUME(ctrl != nullptr);
    }
    // This constructor is used in begin() to avoid an MSan
    // use-of-uninitialized-value error. Delegating from this constructor to
    // the previous one doesn't avoid the error.
    iterator(ctrl_t* ctrl, MaybeInitializedPtr slot,
             const GenerationType* generation_ptr)
        : HashSetIteratorGenerationInfo(generation_ptr),
          ctrl_(ctrl),
          slot_(to_slot(slot.get())) {
      // This assumption helps the compiler know that any non-end iterator is
      // not equal to any end iterator.
      ABSL_ASSUME(ctrl != nullptr);
    }
    // For end() iterators.
    explicit iterator(const GenerationType* generation_ptr)
        : HashSetIteratorGenerationInfo(generation_ptr), ctrl_(nullptr) {}

    // Fixes up `ctrl_` to point to a full or sentinel by advancing `ctrl_` and
    // `slot_` until they reach one.
    void skip_empty_or_deleted() {
      while (IsEmptyOrDeleted(*ctrl_)) {
        uint32_t shift =
            GroupFullEmptyOrDeleted{ctrl_}.CountLeadingEmptyOrDeleted();
        ctrl_ += shift;
        slot_ += shift;
      }
    }

    ctrl_t* control() const { return ctrl_; }
    slot_type* slot() const { return slot_; }

    // We use EmptyGroup() for default-constructed iterators so that they can
    // be distinguished from end iterators, which have nullptr ctrl_.
    ctrl_t* ctrl_ = EmptyGroup();
    // To avoid uninitialized member warnings, put slot_ in an anonymous union.
    // The member is not initialized on singleton and end iterators.
    union {
      slot_type* slot_;
    };

    // An equality check which skips ABSL Hardening iterator invalidation
    // checks.
    // Should be used when the lifetimes of the iterators are well-enough
    // understood to prove that they cannot be invalid.
    bool unchecked_equals(const iterator& b) { return ctrl_ == b.control(); }

    // Dereferences the iterator without ABSL Hardening iterator invalidation
    // checks.
    reference unchecked_deref() const { return PolicyTraits::element(slot_); }
  };

  class const_iterator {
    friend class raw_hash_set;
    template <class Container, typename Enabler>
    friend struct absl::container_internal::hashtable_debug_internal::
        HashtableDebugAccess;

   public:
    using iterator_category = typename iterator::iterator_category;
    using value_type = typename raw_hash_set::value_type;
    using reference = typename raw_hash_set::const_reference;
    using pointer = typename raw_hash_set::const_pointer;
    using difference_type = typename raw_hash_set::difference_type;

    const_iterator() = default;
    // Implicit construction from iterator.
    const_iterator(iterator i) : inner_(std::move(i)) {}  // NOLINT

    reference operator*() const { return *inner_; }
    pointer operator->() const { return inner_.operator->(); }

    const_iterator& operator++() {
      ++inner_;
      return *this;
    }
    const_iterator operator++(int) { return inner_++; }

    friend bool operator==(const const_iterator& a, const const_iterator& b) {
      return a.inner_ == b.inner_;
    }
    friend bool operator!=(const const_iterator& a, const const_iterator& b) {
      return !(a == b);
    }

   private:
    const_iterator(const ctrl_t* ctrl, const slot_type* slot,
                   const GenerationType* gen)
        : inner_(const_cast<ctrl_t*>(ctrl), const_cast<slot_type*>(slot), gen) {
    }
    ctrl_t* control() const { return inner_.control(); }
    slot_type* slot() const { return inner_.slot(); }

    iterator inner_;

    bool unchecked_equals(const const_iterator& b) {
      return inner_.unchecked_equals(b.inner_);
    }
  };

  using node_type = node_handle<Policy, hash_policy_traits<Policy>, Alloc>;
  using insert_return_type = InsertReturnType<iterator, node_type>;

  // Note: can't use `= default` due to non-default noexcept (causes
  // problems for some compilers). NOLINTNEXTLINE
  raw_hash_set() noexcept(
      std::is_nothrow_default_constructible<hasher>::value &&
      std::is_nothrow_default_constructible<key_equal>::value &&
      std::is_nothrow_default_constructible<allocator_type>::value) {}

  ABSL_ATTRIBUTE_NOINLINE explicit raw_hash_set(
      size_t bucket_count, const hasher& hash = hasher(),
      const key_equal& eq = key_equal(),
      const allocator_type& alloc = allocator_type())
      : settings_(CommonFields::CreateDefault<SooEnabled()>(), hash, eq,
                  alloc) {
    if (bucket_count > DefaultCapacity()) {
      if (ABSL_PREDICT_FALSE(bucket_count >
                             MaxValidCapacity<sizeof(slot_type)>())) {
        HashTableSizeOverflow();
      }
      resize(NormalizeCapacity(bucket_count));
    }
  }

  raw_hash_set(size_t bucket_count, const hasher& hash,
               const allocator_type& alloc)
      : raw_hash_set(bucket_count, hash, key_equal(), alloc) {}

  raw_hash_set(size_t bucket_count, const allocator_type& alloc)
      : raw_hash_set(bucket_count, hasher(), key_equal(), alloc) {}

  explicit raw_hash_set(const allocator_type& alloc)
      : raw_hash_set(0, hasher(), key_equal(), alloc) {}

  template <class InputIter>
  raw_hash_set(InputIter first, InputIter last, size_t bucket_count = 0,
               const hasher& hash = hasher(), const key_equal& eq = key_equal(),
               const allocator_type& alloc = allocator_type())
      : raw_hash_set(SelectBucketCountForIterRange(first, last, bucket_count),
                     hash, eq, alloc) {
    insert(first, last);
  }

  template <class InputIter>
  raw_hash_set(InputIter first, InputIter last, size_t bucket_count,
               const hasher& hash, const allocator_type& alloc)
      : raw_hash_set(first, last, bucket_count, hash, key_equal(), alloc) {}

  template <class InputIter>
  raw_hash_set(InputIter first, InputIter last, size_t bucket_count,
               const allocator_type& alloc)
      : raw_hash_set(first, last, bucket_count, hasher(), key_equal(), alloc) {}

  template <class InputIter>
  raw_hash_set(InputIter first, InputIter last, const allocator_type& alloc)
      : raw_hash_set(first, last, 0, hasher(), key_equal(), alloc) {}

  // Instead of accepting std::initializer_list<value_type> as the first
  // argument like std::unordered_set<value_type> does, we have two overloads
  // that accept std::initializer_list<T> and std::initializer_list<init_type>.
  // This is advantageous for performance.
  //
  //   // Turns {"abc", "def"} into std::initializer_list<std::string>, then
  //   // copies the strings into the set.
  //   std::unordered_set<std::string> s = {"abc", "def"};
  //
  //   // Turns {"abc", "def"} into std::initializer_list<const char*>, then
  //   // copies the strings into the set.
  //   absl::flat_hash_set<std::string> s = {"abc", "def"};
  //
  // The same trick is used in insert().
  //
  // The enabler is necessary to prevent this constructor from triggering where
  // the copy constructor is meant to be called.
  //
  //   absl::flat_hash_set<int> a, b{a};
  //
  // RequiresNotInit<T> is a workaround for gcc prior to 7.1.
  template <class T, RequiresNotInit<T> = 0,
            std::enable_if_t<Insertable<T>::value, int> = 0>
  raw_hash_set(std::initializer_list<T> init, size_t bucket_count = 0,
               const hasher& hash = hasher(), const key_equal& eq = key_equal(),
               const allocator_type& alloc = allocator_type())
      : raw_hash_set(init.begin(), init.end(), bucket_count, hash, eq, alloc) {}

  raw_hash_set(std::initializer_list<init_type> init, size_t bucket_count = 0,
               const hasher& hash = hasher(), const key_equal& eq = key_equal(),
               const allocator_type& alloc = allocator_type())
      : raw_hash_set(init.begin(), init.end(), bucket_count, hash, eq, alloc) {}

  template <class T, RequiresNotInit<T> = 0,
            std::enable_if_t<Insertable<T>::value, int> = 0>
  raw_hash_set(std::initializer_list<T> init, size_t bucket_count,
               const hasher& hash, const allocator_type& alloc)
      : raw_hash_set(init, bucket_count, hash, key_equal(), alloc) {}

  raw_hash_set(std::initializer_list<init_type> init, size_t bucket_count,
               const hasher& hash, const allocator_type& alloc)
      : raw_hash_set(init, bucket_count, hash, key_equal(), alloc) {}

  template <class T, RequiresNotInit<T> = 0,
            std::enable_if_t<Insertable<T>::value, int> = 0>
  raw_hash_set(std::initializer_list<T> init, size_t bucket_count,
               const allocator_type& alloc)
      : raw_hash_set(init, bucket_count, hasher(), key_equal(), alloc) {}

  raw_hash_set(std::initializer_list<init_type> init, size_t bucket_count,
               const allocator_type& alloc)
      : raw_hash_set(init, bucket_count, hasher(), key_equal(), alloc) {}

  template <class T, RequiresNotInit<T> = 0,
            std::enable_if_t<Insertable<T>::value, int> = 0>
  raw_hash_set(std::initializer_list<T> init, const allocator_type& alloc)
      : raw_hash_set(init, 0, hasher(), key_equal(), alloc) {}

  raw_hash_set(std::initializer_list<init_type> init,
               const allocator_type& alloc)
      : raw_hash_set(init, 0, hasher(), key_equal(), alloc) {}

  raw_hash_set(const raw_hash_set& that)
      : raw_hash_set(that, AllocTraits::select_on_container_copy_construction(
                               that.alloc_ref())) {}

  raw_hash_set(const raw_hash_set& that, const allocator_type& a)
      : raw_hash_set(GrowthToLowerboundCapacity(that.size()), that.hash_ref(),
                     that.eq_ref(), a) {
    that.AssertNotDebugCapacity();
    const size_t size = that.size();
    if (size == 0) {
      return;
    }
    // We don't use `that.is_soo()` here because `that` can have non-SOO
    // capacity but have a size that fits into SOO capacity.
    if (fits_in_soo(size)) {
      ABSL_SWISSTABLE_ASSERT(size == 1);
      common().set_full_soo();
      emplace_at(soo_iterator(), *that.begin());
      const HashtablezInfoHandle infoz = try_sample_soo();
      if (infoz.IsSampled()) resize_with_soo_infoz(infoz);
      return;
    }
    ABSL_SWISSTABLE_ASSERT(!that.is_soo());
    const size_t cap = capacity();
    // Note about single group tables:
    // 1. It is correct to have any order of elements.
    // 2. Order has to be non deterministic.
    // 3. We are assigning elements with arbitrary `shift` starting from
    //    `capacity + shift` position.
    // 4. `shift` must be coprime with `capacity + 1` in order to be able to use
    //     modular arithmetic to traverse all positions, instead if cycling
    //     through a subset of positions. Odd numbers are coprime with any
    //     `capacity + 1` (2^N).
    size_t offset = cap;
    const size_t shift =
        is_single_group(cap) ? (PerTableSalt(control()) | 1) : 0;
    IterateOverFullSlots(
        that.common(), that.slot_array(),
        [&](const ctrl_t* that_ctrl,
            slot_type* that_slot) ABSL_ATTRIBUTE_ALWAYS_INLINE {
          if (shift == 0) {
            // Big tables case. Position must be searched via probing.
            // The table is guaranteed to be empty, so we can do faster than
            // a full `insert`.
            const size_t hash = PolicyTraits::apply(
                HashElement{hash_ref()}, PolicyTraits::element(that_slot));
            FindInfo target = find_first_non_full_outofline(common(), hash);
            infoz().RecordInsert(hash, target.probe_length);
            offset = target.offset;
          } else {
            // Small tables case. Next position is computed via shift.
            offset = (offset + shift) & cap;
          }
          const h2_t h2 = static_cast<h2_t>(*that_ctrl);
          ABSL_SWISSTABLE_ASSERT(  // We rely that hash is not changed for small
                                   // tables.
              H2(PolicyTraits::apply(HashElement{hash_ref()},
                                     PolicyTraits::element(that_slot))) == h2 &&
              "hash function value changed unexpectedly during the copy");
          SetCtrl(common(), offset, h2, sizeof(slot_type));
          emplace_at(iterator_at(offset), PolicyTraits::element(that_slot));
          common().maybe_increment_generation_on_insert();
        });
    if (shift != 0) {
      // On small table copy we do not record individual inserts.
      // RecordInsert requires hash, but it is unknown for small tables.
      infoz().RecordStorageChanged(size, cap);
    }
    common().set_size(size);
    growth_info().OverwriteManyEmptyAsFull(size);
  }

  ABSL_ATTRIBUTE_NOINLINE raw_hash_set(raw_hash_set&& that) noexcept(
      std::is_nothrow_copy_constructible<hasher>::value &&
      std::is_nothrow_copy_constructible<key_equal>::value &&
      std::is_nothrow_copy_constructible<allocator_type>::value)
      :  // Hash, equality and allocator are copied instead of moved because
         // `that` must be left valid. If Hash is std::function<Key>, moving it
         // would create a nullptr functor that cannot be called.
         // Note: we avoid using exchange for better generated code.
        settings_(PolicyTraits::transfer_uses_memcpy() || !that.is_full_soo()
                      ? std::move(that.common())
                      : CommonFields{full_soo_tag_t{}},
                  that.hash_ref(), that.eq_ref(), that.alloc_ref()) {
    if (!PolicyTraits::transfer_uses_memcpy() && that.is_full_soo()) {
      transfer(soo_slot(), that.soo_slot());
    }
    that.common() = CommonFields::CreateDefault<SooEnabled()>();
    annotate_for_bug_detection_on_move(that);
  }

  raw_hash_set(raw_hash_set&& that, const allocator_type& a)
      : settings_(CommonFields::CreateDefault<SooEnabled()>(), that.hash_ref(),
                  that.eq_ref(), a) {
    if (a == that.alloc_ref()) {
      swap_common(that);
      annotate_for_bug_detection_on_move(that);
    } else {
      move_elements_allocs_unequal(std::move(that));
    }
  }

  raw_hash_set& operator=(const raw_hash_set& that) {
    that.AssertNotDebugCapacity();
    if (ABSL_PREDICT_FALSE(this == &that)) return *this;
    constexpr bool propagate_alloc =
        AllocTraits::propagate_on_container_copy_assignment::value;
    // TODO(ezb): maybe avoid allocating a new backing array if this->capacity()
    // is an exact match for that.size(). If this->capacity() is too big, then
    // it would make iteration very slow to reuse the allocation. Maybe we can
    // do the same heuristic as clear() and reuse if it's small enough.
    raw_hash_set tmp(that, propagate_alloc ? that.alloc_ref() : alloc_ref());
    // NOLINTNEXTLINE: not returning *this for performance.
    return assign_impl<propagate_alloc>(std::move(tmp));
  }

  raw_hash_set& operator=(raw_hash_set&& that) noexcept(
      absl::allocator_traits<allocator_type>::is_always_equal::value &&
      std::is_nothrow_move_assignable<hasher>::value &&
      std::is_nothrow_move_assignable<key_equal>::value) {
    // TODO(sbenza): We should only use the operations from the noexcept clause
    // to make sure we actually adhere to that contract.
    // NOLINTNEXTLINE: not returning *this for performance.
    return move_assign(
        std::move(that),
        typename AllocTraits::propagate_on_container_move_assignment());
  }

  ~raw_hash_set() {
    destructor_impl();
#ifndef NDEBUG
    common().set_capacity(InvalidCapacity::kDestroyed);
#endif
  }

  iterator begin() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(empty())) return end();
    if (is_soo()) return soo_iterator();
    iterator it = {control(), common().slots_union(),
                   common().generation_ptr()};
    it.skip_empty_or_deleted();
    ABSL_SWISSTABLE_ASSERT(IsFull(*it.control()));
    return it;
  }
  iterator end() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    AssertNotDebugCapacity();
    return iterator(common().generation_ptr());
  }

  const_iterator begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_cast<raw_hash_set*>(this)->begin();
  }
  const_iterator end() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_cast<raw_hash_set*>(this)->end();
  }
  const_iterator cbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return begin();
  }
  const_iterator cend() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return end(); }

  bool empty() const { return !size(); }
  size_t size() const {
    AssertNotDebugCapacity();
    return common().size();
  }
  size_t capacity() const {
    const size_t cap = common().capacity();
    // Compiler complains when using functions in ASSUME so use local variable.
    ABSL_ATTRIBUTE_UNUSED static constexpr size_t kDefaultCapacity =
        DefaultCapacity();
    ABSL_ASSUME(cap >= kDefaultCapacity);
    return cap;
  }
  size_t max_size() const {
    return CapacityToGrowth(MaxValidCapacity<sizeof(slot_type)>());
  }

  ABSL_ATTRIBUTE_REINITIALIZES void clear() {
    if (SwisstableGenerationsEnabled() &&
        capacity() >= InvalidCapacity::kMovedFrom) {
      common().set_capacity(DefaultCapacity());
    }
    AssertNotDebugCapacity();
    // Iterating over this container is O(bucket_count()). When bucket_count()
    // is much greater than size(), iteration becomes prohibitively expensive.
    // For clear() it is more important to reuse the allocated array when the
    // container is small because allocation takes comparatively long time
    // compared to destruction of the elements of the container. So we pick the
    // largest bucket_count() threshold for which iteration is still fast and
    // past that we simply deallocate the array.
    const size_t cap = capacity();
    if (cap == 0) {
      // Already guaranteed to be empty; so nothing to do.
    } else if (is_soo()) {
      if (!empty()) destroy(soo_slot());
      common().set_empty_soo();
    } else {
      destroy_slots();
      ClearBackingArray(common(), GetPolicyFunctions(), /*reuse=*/cap < 128,
                        SooEnabled());
    }
    common().set_reserved_growth(0);
    common().set_reservation_size(0);
  }

  // This overload kicks in when the argument is an rvalue of insertable and
  // decomposable type other than init_type.
  //
  //   flat_hash_map<std::string, int> m;
  //   m.insert(std::make_pair("abc", 42));
  template <class T,
            std::enable_if_t<IsDecomposableAndInsertable<T>::value &&
                                 IsNotBitField<T>::value &&
                                 !IsLifetimeBoundAssignmentFrom<T>::value,
                             int> = 0>
  std::pair<iterator, bool> insert(T&& value) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return emplace(std::forward<T>(value));
  }

  template <class T,
            std::enable_if_t<IsDecomposableAndInsertable<T>::value &&
                                 IsNotBitField<T>::value &&
                                 IsLifetimeBoundAssignmentFrom<T>::value,
                             int> = 0>
  std::pair<iterator, bool> insert(
      T&& value ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY(this))
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return emplace(std::forward<T>(value));
  }

  // This overload kicks in when the argument is a bitfield or an lvalue of
  // insertable and decomposable type.
  //
  //   union { int n : 1; };
  //   flat_hash_set<int> s;
  //   s.insert(n);
  //
  //   flat_hash_set<std::string> s;
  //   const char* p = "hello";
  //   s.insert(p);
  //
  template <class T, std::enable_if_t<
                         IsDecomposableAndInsertable<const T&>::value &&
                             !IsLifetimeBoundAssignmentFrom<const T&>::value,
                         int> = 0>
  std::pair<iterator, bool> insert(const T& value)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return emplace(value);
  }
  template <class T,
            std::enable_if_t<IsDecomposableAndInsertable<const T&>::value &&
                                 IsLifetimeBoundAssignmentFrom<const T&>::value,
                             int> = 0>
  std::pair<iterator, bool> insert(
      const T& value ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY(this))
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return emplace(value);
  }

  // This overload kicks in when the argument is an rvalue of init_type. Its
  // purpose is to handle brace-init-list arguments.
  //
  //   flat_hash_map<std::string, int> s;
  //   s.insert({"abc", 42});
  std::pair<iterator, bool> insert(init_type&& value)
      ABSL_ATTRIBUTE_LIFETIME_BOUND
#if __cplusplus >= 202002L
    requires(!IsLifetimeBoundAssignmentFrom<init_type>::value)
#endif
  {
    return emplace(std::move(value));
  }
#if __cplusplus >= 202002L
  std::pair<iterator, bool> insert(
      init_type&& value ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY(this))
      ABSL_ATTRIBUTE_LIFETIME_BOUND
    requires(IsLifetimeBoundAssignmentFrom<init_type>::value)
  {
    return emplace(std::move(value));
  }
#endif

  template <class T,
            std::enable_if_t<IsDecomposableAndInsertable<T>::value &&
                                 IsNotBitField<T>::value &&
                                 !IsLifetimeBoundAssignmentFrom<T>::value,
                             int> = 0>
  iterator insert(const_iterator, T&& value) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert(std::forward<T>(value)).first;
  }
  template <class T,
            std::enable_if_t<IsDecomposableAndInsertable<T>::value &&
                                 IsNotBitField<T>::value &&
                                 IsLifetimeBoundAssignmentFrom<T>::value,
                             int> = 0>
  iterator insert(const_iterator, T&& value ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY(
                                      this)) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert(std::forward<T>(value)).first;
  }

  template <class T, std::enable_if_t<
                         IsDecomposableAndInsertable<const T&>::value, int> = 0>
  iterator insert(const_iterator,
                  const T& value) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert(value).first;
  }

  iterator insert(const_iterator,
                  init_type&& value) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert(std::move(value)).first;
  }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    for (; first != last; ++first) emplace(*first);
  }

  template <class T, RequiresNotInit<T> = 0,
            std::enable_if_t<Insertable<const T&>::value, int> = 0>
  void insert(std::initializer_list<T> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  void insert(std::initializer_list<init_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  insert_return_type insert(node_type&& node) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (!node) return {end(), false, node_type()};
    const auto& elem = PolicyTraits::element(CommonAccess::GetSlot(node));
    auto res = PolicyTraits::apply(
        InsertSlot<false>{*this, std::move(*CommonAccess::GetSlot(node))},
        elem);
    if (res.second) {
      CommonAccess::Reset(&node);
      return {res.first, true, node_type()};
    } else {
      return {res.first, false, std::move(node)};
    }
  }

  iterator insert(const_iterator,
                  node_type&& node) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto res = insert(std::move(node));
    node = std::move(res.node);
    return res.position;
  }

  // This overload kicks in if we can deduce the key from args. This enables us
  // to avoid constructing value_type if an entry with the same key already
  // exists.
  //
  // For example:
  //
  //   flat_hash_map<std::string, std::string> m = {{"abc", "def"}};
  //   // Creates no std::string copies and makes no heap allocations.
  //   m.emplace("abc", "xyz");
  template <class... Args,
            std::enable_if_t<IsDecomposable<Args...>::value, int> = 0>
  std::pair<iterator, bool> emplace(Args&&... args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return PolicyTraits::apply(EmplaceDecomposable{*this},
                               std::forward<Args>(args)...);
  }

  // This overload kicks in if we cannot deduce the key from args. It constructs
  // value_type unconditionally and then either moves it into the table or
  // destroys.
  template <class... Args,
            std::enable_if_t<!IsDecomposable<Args...>::value, int> = 0>
  std::pair<iterator, bool> emplace(Args&&... args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    alignas(slot_type) unsigned char raw[sizeof(slot_type)];
    slot_type* slot = to_slot(&raw);

    construct(slot, std::forward<Args>(args)...);
    const auto& elem = PolicyTraits::element(slot);
    return PolicyTraits::apply(InsertSlot<true>{*this, std::move(*slot)}, elem);
  }

  template <class... Args>
  iterator emplace_hint(const_iterator,
                        Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return emplace(std::forward<Args>(args)...).first;
  }

  // Extension API: support for lazy emplace.
  //
  // Looks up key in the table. If found, returns the iterator to the element.
  // Otherwise calls `f` with one argument of type `raw_hash_set::constructor`,
  // and returns an iterator to the new element.
  //
  // `f` must abide by several restrictions:
  //  - it MUST call `raw_hash_set::constructor` with arguments as if a
  //    `raw_hash_set::value_type` is constructed,
  //  - it MUST NOT access the container before the call to
  //    `raw_hash_set::constructor`, and
  //  - it MUST NOT erase the lazily emplaced element.
  // Doing any of these is undefined behavior.
  //
  // For example:
  //
  //   std::unordered_set<ArenaString> s;
  //   // Makes ArenaStr even if "abc" is in the map.
  //   s.insert(ArenaString(&arena, "abc"));
  //
  //   flat_hash_set<ArenaStr> s;
  //   // Makes ArenaStr only if "abc" is not in the map.
  //   s.lazy_emplace("abc", [&](const constructor& ctor) {
  //     ctor(&arena, "abc");
  //   });
  //
  // WARNING: This API is currently experimental. If there is a way to implement
  // the same thing with the rest of the API, prefer that.
  class constructor {
    friend class raw_hash_set;

   public:
    template <class... Args>
    void operator()(Args&&... args) const {
      ABSL_SWISSTABLE_ASSERT(*slot_);
      PolicyTraits::construct(alloc_, *slot_, std::forward<Args>(args)...);
      *slot_ = nullptr;
    }

   private:
    constructor(allocator_type* a, slot_type** slot) : alloc_(a), slot_(slot) {}

    allocator_type* alloc_;
    slot_type** slot_;
  };

  template <class K = key_type, class F>
  iterator lazy_emplace(const key_arg<K>& key,
                        F&& f) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto res = find_or_prepare_insert(key);
    if (res.second) {
      slot_type* slot = res.first.slot();
      std::forward<F>(f)(constructor(&alloc_ref(), &slot));
      ABSL_SWISSTABLE_ASSERT(!slot);
    }
    return res.first;
  }

  // Extension API: support for heterogeneous keys.
  //
  //   std::unordered_set<std::string> s;
  //   // Turns "abc" into std::string.
  //   s.erase("abc");
  //
  //   flat_hash_set<std::string> s;
  //   // Uses "abc" directly without copying it into std::string.
  //   s.erase("abc");
  template <class K = key_type>
  size_type erase(const key_arg<K>& key) {
    auto it = find(key);
    if (it == end()) return 0;
    erase(it);
    return 1;
  }

  // Erases the element pointed to by `it`. Unlike `std::unordered_set::erase`,
  // this method returns void to reduce algorithmic complexity to O(1). The
  // iterator is invalidated so any increment should be done before calling
  // erase (e.g. `erase(it++)`).
  void erase(const_iterator cit) { erase(cit.inner_); }

  // This overload is necessary because otherwise erase<K>(const K&) would be
  // a better match if non-const iterator is passed as an argument.
  void erase(iterator it) {
    AssertNotDebugCapacity();
    AssertIsFull(it.control(), it.generation(), it.generation_ptr(), "erase()");
    destroy(it.slot());
    if (is_soo()) {
      common().set_empty_soo();
    } else {
      erase_meta_only(it);
    }
  }

  iterator erase(const_iterator first,
                 const_iterator last) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    AssertNotDebugCapacity();
    // We check for empty first because ClearBackingArray requires that
    // capacity() > 0 as a precondition.
    if (empty()) return end();
    if (first == last) return last.inner_;
    if (is_soo()) {
      destroy(soo_slot());
      common().set_empty_soo();
      return end();
    }
    if (first == begin() && last == end()) {
      // TODO(ezb): we access control bytes in destroy_slots so it could make
      // sense to combine destroy_slots and ClearBackingArray to avoid cache
      // misses when the table is large. Note that we also do this in clear().
      destroy_slots();
      ClearBackingArray(common(), GetPolicyFunctions(), /*reuse=*/true,
                        SooEnabled());
      common().set_reserved_growth(common().reservation_size());
      return end();
    }
    while (first != last) {
      erase(first++);
    }
    return last.inner_;
  }

  // Moves elements from `src` into `this`.
  // If the element already exists in `this`, it is left unmodified in `src`.
  template <typename H, typename E>
  void merge(raw_hash_set<Policy, H, E, Alloc>& src) {  // NOLINT
    AssertNotDebugCapacity();
    src.AssertNotDebugCapacity();
    assert(this != &src);
    // Returns whether insertion took place.
    const auto insert_slot = [this](slot_type* src_slot) {
      return PolicyTraits::apply(InsertSlot<false>{*this, std::move(*src_slot)},
                                 PolicyTraits::element(src_slot))
          .second;
    };

    if (src.is_soo()) {
      if (src.empty()) return;
      if (insert_slot(src.soo_slot())) src.common().set_empty_soo();
      return;
    }
    for (auto it = src.begin(), e = src.end(); it != e;) {
      auto next = std::next(it);
      if (insert_slot(it.slot())) src.erase_meta_only(it);
      it = next;
    }
  }

  template <typename H, typename E>
  void merge(raw_hash_set<Policy, H, E, Alloc>&& src) {
    merge(src);
  }

  node_type extract(const_iterator position) {
    AssertNotDebugCapacity();
    AssertIsFull(position.control(), position.inner_.generation(),
                 position.inner_.generation_ptr(), "extract()");
    auto node = CommonAccess::Transfer<node_type>(alloc_ref(), position.slot());
    if (is_soo()) {
      common().set_empty_soo();
    } else {
      erase_meta_only(position);
    }
    return node;
  }

  template <class K = key_type,
            std::enable_if_t<!std::is_same<K, iterator>::value, int> = 0>
  node_type extract(const key_arg<K>& key) {
    auto it = find(key);
    return it == end() ? node_type() : extract(const_iterator{it});
  }

  void swap(raw_hash_set& that) noexcept(
      IsNoThrowSwappable<hasher>() && IsNoThrowSwappable<key_equal>() &&
      IsNoThrowSwappable<allocator_type>(
          typename AllocTraits::propagate_on_container_swap{})) {
    AssertNotDebugCapacity();
    that.AssertNotDebugCapacity();
    using std::swap;
    swap_common(that);
    swap(hash_ref(), that.hash_ref());
    swap(eq_ref(), that.eq_ref());
    SwapAlloc(alloc_ref(), that.alloc_ref(),
              typename AllocTraits::propagate_on_container_swap{});
  }

  void rehash(size_t n) {
    const size_t cap = capacity();
    if (n == 0) {
      if (cap == 0 || is_soo()) return;
      if (empty()) {
        ClearBackingArray(common(), GetPolicyFunctions(), /*reuse=*/false,
                          SooEnabled());
        return;
      }
      if (fits_in_soo(size())) {
        // When the table is already sampled, we keep it sampled.
        if (infoz().IsSampled()) {
          const size_t kInitialSampledCapacity = NextCapacity(SooCapacity());
          if (capacity() > kInitialSampledCapacity) {
            resize(kInitialSampledCapacity);
          }
          // This asserts that we didn't lose sampling coverage in `resize`.
          ABSL_SWISSTABLE_ASSERT(infoz().IsSampled());
          return;
        }
        alignas(slot_type) unsigned char slot_space[sizeof(slot_type)];
        slot_type* tmp_slot = to_slot(slot_space);
        transfer(tmp_slot, begin().slot());
        ClearBackingArray(common(), GetPolicyFunctions(), /*reuse=*/false,
                          SooEnabled());
        transfer(soo_slot(), tmp_slot);
        common().set_full_soo();
        return;
      }
    }

    // bitor is a faster way of doing `max` here. We will round up to the next
    // power-of-2-minus-1, so bitor is good enough.
    auto m = NormalizeCapacity(n | GrowthToLowerboundCapacity(size()));
    // n == 0 unconditionally rehashes as per the standard.
    if (n == 0 || m > cap) {
      if (ABSL_PREDICT_FALSE(m > MaxValidCapacity<sizeof(slot_type)>())) {
        HashTableSizeOverflow();
      }
      resize(m);

      // This is after resize, to ensure that we have completed the allocation
      // and have potentially sampled the hashtable.
      infoz().RecordReservation(n);
    }
  }

  void reserve(size_t n) {
    const size_t max_size_before_growth =
        is_soo() ? SooCapacity() : size() + growth_left();
    if (n > max_size_before_growth) {
      if (ABSL_PREDICT_FALSE(n > max_size())) {
        HashTableSizeOverflow();
      }
      size_t m = GrowthToLowerboundCapacity(n);
      resize(NormalizeCapacity(m));

      // This is after resize, to ensure that we have completed the allocation
      // and have potentially sampled the hashtable.
      infoz().RecordReservation(n);
    }
    common().reset_reserved_growth(n);
    common().set_reservation_size(n);
  }

  // Extension API: support for heterogeneous keys.
  //
  //   std::unordered_set<std::string> s;
  //   // Turns "abc" into std::string.
  //   s.count("abc");
  //
  //   ch_set<std::string> s;
  //   // Uses "abc" directly without copying it into std::string.
  //   s.count("abc");
  template <class K = key_type>
  size_t count(const key_arg<K>& key) const {
    return find(key) == end() ? 0 : 1;
  }

  // Issues CPU prefetch instructions for the memory needed to find or insert
  // a key.  Like all lookup functions, this support heterogeneous keys.
  //
  // NOTE: This is a very low level operation and should not be used without
  // specific benchmarks indicating its importance.
  template <class K = key_type>
  void prefetch(const key_arg<K>& key) const {
    if (capacity() == DefaultCapacity()) return;
    (void)key;
    // Avoid probing if we won't be able to prefetch the addresses received.
#ifdef ABSL_HAVE_PREFETCH
    prefetch_heap_block();
    auto seq = probe(common(), hash_ref()(key));
    PrefetchToLocalCache(control() + seq.offset());
    PrefetchToLocalCache(slot_array() + seq.offset());
#endif  // ABSL_HAVE_PREFETCH
  }

  template <class K = key_type>
  ABSL_DEPRECATE_AND_INLINE()
  iterator find(const key_arg<K>& key,
                size_t) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return find(key);
  }
  // The API of find() has one extension: the type of the key argument doesn't
  // have to be key_type. This is so called heterogeneous key support.
  template <class K = key_type>
  iterator find(const key_arg<K>& key) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    AssertOnFind(key);
    if (is_soo()) return find_soo(key);
    prefetch_heap_block();
    return find_non_soo(key, hash_ref()(key));
  }

  template <class K = key_type>
  ABSL_DEPRECATE_AND_INLINE()
  const_iterator find(const key_arg<K>& key,
                      size_t) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return find(key);
  }
  template <class K = key_type>
  const_iterator find(const key_arg<K>& key) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_cast<raw_hash_set*>(this)->find(key);
  }

  template <class K = key_type>
  bool contains(const key_arg<K>& key) const {
    // Here neither the iterator returned by `find()` nor `end()` can be invalid
    // outside of potential thread-safety issues.
    // `find()`'s return value is constructed, used, and then destructed
    // all in this context.
    return !find(key).unchecked_equals(end());
  }

  template <class K = key_type>
  std::pair<iterator, iterator> equal_range(const key_arg<K>& key)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = find(key);
    if (it != end()) return {it, std::next(it)};
    return {it, it};
  }
  template <class K = key_type>
  std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K>& key) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = find(key);
    if (it != end()) return {it, std::next(it)};
    return {it, it};
  }

  size_t bucket_count() const { return capacity(); }
  float load_factor() const {
    return capacity() ? static_cast<double>(size()) / capacity() : 0.0;
  }
  float max_load_factor() const { return 1.0f; }
  void max_load_factor(float) {
    // Does nothing.
  }

  hasher hash_function() const { return hash_ref(); }
  key_equal key_eq() const { return eq_ref(); }
  allocator_type get_allocator() const { return alloc_ref(); }

  friend bool operator==(const raw_hash_set& a, const raw_hash_set& b) {
    if (a.size() != b.size()) return false;
    const raw_hash_set* outer = &a;
    const raw_hash_set* inner = &b;
    if (outer->capacity() > inner->capacity()) std::swap(outer, inner);
    for (const value_type& elem : *outer) {
      auto it = PolicyTraits::apply(FindElement{*inner}, elem);
      if (it == inner->end()) return false;
      // Note: we used key_equal to check for key equality in FindElement, but
      // we may need to do an additional comparison using
      // value_type::operator==. E.g. the keys could be equal and the
      // mapped_types could be unequal in a map or even in a set, key_equal
      // could ignore some fields that aren't ignored by operator==.
      static constexpr bool kKeyEqIsValueEq =
          std::is_same<key_type, value_type>::value &&
          std::is_same<key_equal, hash_default_eq<key_type>>::value;
      if (!kKeyEqIsValueEq && !(*it == elem)) return false;
    }
    return true;
  }

  friend bool operator!=(const raw_hash_set& a, const raw_hash_set& b) {
    return !(a == b);
  }

  template <typename H>
  friend typename std::enable_if<H::template is_hashable<value_type>::value,
                                 H>::type
  AbslHashValue(H h, const raw_hash_set& s) {
    return H::combine(H::combine_unordered(std::move(h), s.begin(), s.end()),
                      s.size());
  }

  friend void swap(raw_hash_set& a,
                   raw_hash_set& b) noexcept(noexcept(a.swap(b))) {
    a.swap(b);
  }

 private:
  template <class Container, typename Enabler>
  friend struct absl::container_internal::hashtable_debug_internal::
      HashtableDebugAccess;

  friend struct absl::container_internal::HashtableFreeFunctionsAccess;

  struct FindElement {
    template <class K, class... Args>
    const_iterator operator()(const K& key, Args&&...) const {
      return s.find(key);
    }
    const raw_hash_set& s;
  };

  struct HashElement {
    template <class K, class... Args>
    size_t operator()(const K& key, Args&&...) const {
      return h(key);
    }
    const hasher& h;
  };

  template <class K1>
  struct EqualElement {
    template <class K2, class... Args>
    bool operator()(const K2& lhs, Args&&...) const {
      return eq(lhs, rhs);
    }
    const K1& rhs;
    const key_equal& eq;
  };

  struct EmplaceDecomposable {
    template <class K, class... Args>
    std::pair<iterator, bool> operator()(const K& key, Args&&... args) const {
      auto res = s.find_or_prepare_insert(key);
      if (res.second) {
        s.emplace_at(res.first, std::forward<Args>(args)...);
      }
      return res;
    }
    raw_hash_set& s;
  };

  template <bool do_destroy>
  struct InsertSlot {
    template <class K, class... Args>
    std::pair<iterator, bool> operator()(const K& key, Args&&...) && {
      auto res = s.find_or_prepare_insert(key);
      if (res.second) {
        s.transfer(res.first.slot(), &slot);
      } else if (do_destroy) {
        s.destroy(&slot);
      }
      return res;
    }
    raw_hash_set& s;
    // Constructed slot. Either moved into place or destroyed.
    slot_type&& slot;
  };

  template <typename... Args>
  inline void construct(slot_type* slot, Args&&... args) {
    common().RunWithReentrancyGuard([&] {
      PolicyTraits::construct(&alloc_ref(), slot, std::forward<Args>(args)...);
    });
  }
  inline void destroy(slot_type* slot) {
    common().RunWithReentrancyGuard(
        [&] { PolicyTraits::destroy(&alloc_ref(), slot); });
  }
  inline void transfer(slot_type* to, slot_type* from) {
    common().RunWithReentrancyGuard(
        [&] { PolicyTraits::transfer(&alloc_ref(), to, from); });
  }

  // TODO(b/289225379): consider having a helper class that has the impls for
  // SOO functionality.
  template <class K = key_type>
  iterator find_soo(const key_arg<K>& key) {
    ABSL_SWISSTABLE_ASSERT(is_soo());
    return empty() || !PolicyTraits::apply(EqualElement<K>{key, eq_ref()},
                                           PolicyTraits::element(soo_slot()))
               ? end()
               : soo_iterator();
  }

  template <class K = key_type>
  iterator find_non_soo(const key_arg<K>& key, size_t hash) {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    auto seq = probe(common(), hash);
    const ctrl_t* ctrl = control();
    while (true) {
      Group g{ctrl + seq.offset()};
      for (uint32_t i : g.Match(H2(hash))) {
        if (ABSL_PREDICT_TRUE(PolicyTraits::apply(
                EqualElement<K>{key, eq_ref()},
                PolicyTraits::element(slot_array() + seq.offset(i)))))
          return iterator_at(seq.offset(i));
      }
      if (ABSL_PREDICT_TRUE(g.MaskEmpty())) return end();
      seq.next();
      ABSL_SWISSTABLE_ASSERT(seq.index() <= capacity() && "full table!");
    }
  }

  // Conditionally samples hashtablez for SOO tables. This should be called on
  // insertion into an empty SOO table and in copy construction when the size
  // can fit in SOO capacity.
  inline HashtablezInfoHandle try_sample_soo() {
    ABSL_SWISSTABLE_ASSERT(is_soo());
    if (!ShouldSampleHashtablezInfo<CharAlloc>()) return HashtablezInfoHandle{};
    return Sample(sizeof(slot_type), sizeof(key_type), sizeof(value_type),
                  SooCapacity());
  }

  inline void destroy_slots() {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    if (PolicyTraits::template destroy_is_trivial<Alloc>()) return;
    IterateOverFullSlots(
        common(), slot_array(),
        [&](const ctrl_t*, slot_type* slot)
            ABSL_ATTRIBUTE_ALWAYS_INLINE { this->destroy(slot); });
  }

  inline void dealloc() {
    ABSL_SWISSTABLE_ASSERT(capacity() != 0);
    // Unpoison before returning the memory to the allocator.
    SanitizerUnpoisonMemoryRegion(slot_array(), sizeof(slot_type) * capacity());
    infoz().Unregister();
    Deallocate<BackingArrayAlignment(alignof(slot_type))>(
        &alloc_ref(), common().backing_array_start(),
        common().alloc_size(sizeof(slot_type), alignof(slot_type)));
  }

  inline void destructor_impl() {
    if (SwisstableGenerationsEnabled() &&
        capacity() >= InvalidCapacity::kMovedFrom) {
      return;
    }
    if (capacity() == 0) return;
    if (is_soo()) {
      if (!empty()) {
        ABSL_SWISSTABLE_IGNORE_UNINITIALIZED(destroy(soo_slot()));
      }
      return;
    }
    destroy_slots();
    dealloc();
  }

  // Erases, but does not destroy, the value pointed to by `it`.
  //
  // This merely updates the pertinent control byte. This can be used in
  // conjunction with Policy::transfer to move the object to another place.
  void erase_meta_only(const_iterator it) {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    EraseMetaOnly(common(), static_cast<size_t>(it.control() - control()),
                  sizeof(slot_type));
  }

  size_t hash_of(slot_type* slot) const {
    return PolicyTraits::apply(HashElement{hash_ref()},
                               PolicyTraits::element(slot));
  }

  // Resizes table to the new capacity and move all elements to the new
  // positions accordingly.
  //
  // Note that for better performance instead of
  // find_first_non_full(common(), hash),
  // HashSetResizeHelper::FindFirstNonFullAfterResize(
  //    common(), old_capacity, hash)
  // can be called right after `resize`.
  void resize(size_t new_capacity) {
    raw_hash_set::resize_impl(common(), new_capacity, HashtablezInfoHandle{});
  }

  // As above, except that we also accept a pre-sampled, forced infoz for
  // SOO tables, since they need to switch from SOO to heap in order to
  // store the infoz.
  void resize_with_soo_infoz(HashtablezInfoHandle forced_infoz) {
    ABSL_SWISSTABLE_ASSERT(forced_infoz.IsSampled());
    raw_hash_set::resize_impl(common(), NextCapacity(SooCapacity()),
                              forced_infoz);
  }

  // Resizes set to the new capacity.
  // It is a static function in order to use its pointer in GetPolicyFunctions.
  ABSL_ATTRIBUTE_NOINLINE static void resize_impl(
      CommonFields& common, size_t new_capacity,
      HashtablezInfoHandle forced_infoz) {
    raw_hash_set* set = reinterpret_cast<raw_hash_set*>(&common);
    ABSL_SWISSTABLE_ASSERT(IsValidCapacity(new_capacity));
    ABSL_SWISSTABLE_ASSERT(!set->fits_in_soo(new_capacity));
    const bool was_soo = set->is_soo();
    const bool had_soo_slot = was_soo && !set->empty();
    const ctrl_t soo_slot_h2 =
        had_soo_slot ? static_cast<ctrl_t>(H2(set->hash_of(set->soo_slot())))
                     : ctrl_t::kEmpty;
    HashSetResizeHelper resize_helper(common, was_soo, had_soo_slot,
                                      forced_infoz);
    // Initialize HashSetResizeHelper::old_heap_or_soo_. We can't do this in
    // HashSetResizeHelper constructor because it can't transfer slots when
    // transfer_uses_memcpy is false.
    // TODO(b/289225379): try to handle more of the SOO cases inside
    // InitializeSlots. See comment on cl/555990034 snapshot #63.
    if (PolicyTraits::transfer_uses_memcpy() || !had_soo_slot) {
      resize_helper.old_heap_or_soo() = common.heap_or_soo();
    } else {
      set->transfer(set->to_slot(resize_helper.old_soo_data()),
                    set->soo_slot());
    }
    common.set_capacity(new_capacity);
    // Note that `InitializeSlots` does different number initialization steps
    // depending on the values of `transfer_uses_memcpy` and capacities.
    // Refer to the comment in `InitializeSlots` for more details.
    const bool grow_single_group =
        resize_helper.InitializeSlots<CharAlloc, sizeof(slot_type),
                                      PolicyTraits::transfer_uses_memcpy(),
                                      SooEnabled(), alignof(slot_type)>(
            common, CharAlloc(set->alloc_ref()), soo_slot_h2, sizeof(key_type),
            sizeof(value_type));

    // In the SooEnabled() case, capacity is never 0 so we don't check.
    if (!SooEnabled() && resize_helper.old_capacity() == 0) {
      // InitializeSlots did all the work including infoz().RecordRehash().
      return;
    }
    ABSL_SWISSTABLE_ASSERT(resize_helper.old_capacity() > 0);
    // Nothing more to do in this case.
    if (was_soo && !had_soo_slot) return;

    slot_type* new_slots = set->slot_array();
    if (grow_single_group) {
      if (PolicyTraits::transfer_uses_memcpy()) {
        // InitializeSlots did all the work.
        return;
      }
      if (was_soo) {
        set->transfer(new_slots + resize_helper.SooSlotIndex(),
                      to_slot(resize_helper.old_soo_data()));
        return;
      } else {
        // We want GrowSizeIntoSingleGroup to be called here in order to make
        // InitializeSlots not depend on PolicyTraits.
        resize_helper.GrowSizeIntoSingleGroup<PolicyTraits>(common,
                                                            set->alloc_ref());
      }
    } else {
      // InitializeSlots prepares control bytes to correspond to empty table.
      const auto insert_slot = [&](slot_type* slot) {
        size_t hash = PolicyTraits::apply(HashElement{set->hash_ref()},
                                          PolicyTraits::element(slot));
        auto target = find_first_non_full(common, hash);
        SetCtrl(common, target.offset, H2(hash), sizeof(slot_type));
        set->transfer(new_slots + target.offset, slot);
        return target.probe_length;
      };
      if (was_soo) {
        insert_slot(to_slot(resize_helper.old_soo_data()));
        return;
      } else {
        auto* old_slots = static_cast<slot_type*>(resize_helper.old_slots());
        size_t total_probe_length = 0;
        for (size_t i = 0; i != resize_helper.old_capacity(); ++i) {
          if (IsFull(resize_helper.old_ctrl()[i])) {
            total_probe_length += insert_slot(old_slots + i);
          }
        }
        common.infoz().RecordRehash(total_probe_length);
      }
    }
    resize_helper.DeallocateOld<alignof(slot_type)>(CharAlloc(set->alloc_ref()),
                                                    sizeof(slot_type));
  }

  // Casting directly from e.g. char* to slot_type* can cause compilation errors
  // on objective-C. This function converts to void* first, avoiding the issue.
  static slot_type* to_slot(void* buf) { return static_cast<slot_type*>(buf); }

  // Requires that lhs does not have a full SOO slot.
  static void move_common(bool rhs_is_full_soo, allocator_type& rhs_alloc,
                          CommonFields& lhs, CommonFields&& rhs) {
    if (PolicyTraits::transfer_uses_memcpy() || !rhs_is_full_soo) {
      lhs = std::move(rhs);
    } else {
      lhs.move_non_heap_or_soo_fields(rhs);
      rhs.RunWithReentrancyGuard([&] {
        lhs.RunWithReentrancyGuard([&] {
          PolicyTraits::transfer(&rhs_alloc, to_slot(lhs.soo_data()),
                                 to_slot(rhs.soo_data()));
        });
      });
    }
  }

  // Swaps common fields making sure to avoid memcpy'ing a full SOO slot if we
  // aren't allowed to do so.
  void swap_common(raw_hash_set& that) {
    using std::swap;
    if (PolicyTraits::transfer_uses_memcpy()) {
      swap(common(), that.common());
      return;
    }
    CommonFields tmp = CommonFields(uninitialized_tag_t{});
    const bool that_is_full_soo = that.is_full_soo();
    move_common(that_is_full_soo, that.alloc_ref(), tmp,
                std::move(that.common()));
    move_common(is_full_soo(), alloc_ref(), that.common(), std::move(common()));
    move_common(that_is_full_soo, that.alloc_ref(), common(), std::move(tmp));
  }

  void annotate_for_bug_detection_on_move(
      ABSL_ATTRIBUTE_UNUSED raw_hash_set& that) {
    // We only enable moved-from validation when generations are enabled (rather
    // than using NDEBUG) to avoid issues in which NDEBUG is enabled in some
    // translation units but not in others.
    if (SwisstableGenerationsEnabled()) {
      that.common().set_capacity(this == &that ? InvalidCapacity::kSelfMovedFrom
                                               : InvalidCapacity::kMovedFrom);
    }
    if (!SwisstableGenerationsEnabled() || capacity() == DefaultCapacity() ||
        capacity() > kAboveMaxValidCapacity) {
      return;
    }
    common().increment_generation();
    if (!empty() && common().should_rehash_for_bug_detection_on_move()) {
      resize(capacity());
    }
  }

  template <bool propagate_alloc>
  raw_hash_set& assign_impl(raw_hash_set&& that) {
    // We don't bother checking for this/that aliasing. We just need to avoid
    // breaking the invariants in that case.
    destructor_impl();
    move_common(that.is_full_soo(), that.alloc_ref(), common(),
                std::move(that.common()));
    hash_ref() = that.hash_ref();
    eq_ref() = that.eq_ref();
    CopyAlloc(alloc_ref(), that.alloc_ref(),
              std::integral_constant<bool, propagate_alloc>());
    that.common() = CommonFields::CreateDefault<SooEnabled()>();
    annotate_for_bug_detection_on_move(that);
    return *this;
  }

  raw_hash_set& move_elements_allocs_unequal(raw_hash_set&& that) {
    const size_t size = that.size();
    if (size == 0) return *this;
    reserve(size);
    for (iterator it = that.begin(); it != that.end(); ++it) {
      insert(std::move(PolicyTraits::element(it.slot())));
      that.destroy(it.slot());
    }
    if (!that.is_soo()) that.dealloc();
    that.common() = CommonFields::CreateDefault<SooEnabled()>();
    annotate_for_bug_detection_on_move(that);
    return *this;
  }

  raw_hash_set& move_assign(raw_hash_set&& that,
                            std::true_type /*propagate_alloc*/) {
    return assign_impl<true>(std::move(that));
  }
  raw_hash_set& move_assign(raw_hash_set&& that,
                            std::false_type /*propagate_alloc*/) {
    if (alloc_ref() == that.alloc_ref()) {
      return assign_impl<false>(std::move(that));
    }
    // Aliasing can't happen here because allocs would compare equal above.
    assert(this != &that);
    destructor_impl();
    // We can't take over that's memory so we need to move each element.
    // While moving elements, this should have that's hash/eq so copy hash/eq
    // before moving elements.
    hash_ref() = that.hash_ref();
    eq_ref() = that.eq_ref();
    return move_elements_allocs_unequal(std::move(that));
  }

  template <class K>
  std::pair<iterator, bool> find_or_prepare_insert_soo(const K& key) {
    if (empty()) {
      const HashtablezInfoHandle infoz = try_sample_soo();
      if (infoz.IsSampled()) {
        resize_with_soo_infoz(infoz);
      } else {
        common().set_full_soo();
        return {soo_iterator(), true};
      }
    } else if (PolicyTraits::apply(EqualElement<K>{key, eq_ref()},
                                   PolicyTraits::element(soo_slot()))) {
      return {soo_iterator(), false};
    } else {
      resize(NextCapacity(SooCapacity()));
    }
    const size_t index =
        PrepareInsertAfterSoo(hash_ref()(key), sizeof(slot_type), common());
    return {iterator_at(index), true};
  }

  template <class K>
  std::pair<iterator, bool> find_or_prepare_insert_non_soo(const K& key) {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    prefetch_heap_block();
    auto hash = hash_ref()(key);
    auto seq = probe(common(), hash);
    const ctrl_t* ctrl = control();
    while (true) {
      Group g{ctrl + seq.offset()};
      for (uint32_t i : g.Match(H2(hash))) {
        if (ABSL_PREDICT_TRUE(PolicyTraits::apply(
                EqualElement<K>{key, eq_ref()},
                PolicyTraits::element(slot_array() + seq.offset(i)))))
          return {iterator_at(seq.offset(i)), false};
      }
      auto mask_empty = g.MaskEmpty();
      if (ABSL_PREDICT_TRUE(mask_empty)) {
        size_t target = seq.offset(
            GetInsertionOffset(mask_empty, capacity(), hash, control()));
        return {iterator_at(PrepareInsertNonSoo(common(), hash,
                                                FindInfo{target, seq.index()},
                                                GetPolicyFunctions())),
                true};
      }
      seq.next();
      ABSL_SWISSTABLE_ASSERT(seq.index() <= capacity() && "full table!");
    }
  }

 protected:
  // Asserts for correctness that we run on find/find_or_prepare_insert.
  template <class K>
  void AssertOnFind(ABSL_ATTRIBUTE_UNUSED const K& key) {
    AssertHashEqConsistent(key);
    AssertNotDebugCapacity();
  }

  // Asserts that the capacity is not a sentinel invalid value.
  void AssertNotDebugCapacity() const {
    if (ABSL_PREDICT_TRUE(capacity() <
                          InvalidCapacity::kAboveMaxValidCapacity)) {
      return;
    }
    assert(capacity() != InvalidCapacity::kReentrance &&
           "Reentrant container access during element construction/destruction "
           "is not allowed.");
    assert(capacity() != InvalidCapacity::kDestroyed &&
           "Use of destroyed hash table.");
    if (SwisstableGenerationsEnabled() &&
        ABSL_PREDICT_FALSE(capacity() >= InvalidCapacity::kMovedFrom)) {
      if (capacity() == InvalidCapacity::kSelfMovedFrom) {
        // If this log triggers, then a hash table was move-assigned to itself
        // and then used again later without being reinitialized.
        ABSL_RAW_LOG(FATAL, "Use of self-move-assigned hash table.");
      }
      ABSL_RAW_LOG(FATAL, "Use of moved-from hash table.");
    }
  }

  // Asserts that hash and equal functors provided by the user are consistent,
  // meaning that `eq(k1, k2)` implies `hash(k1)==hash(k2)`.
  template <class K>
  void AssertHashEqConsistent(const K& key) {
#ifdef NDEBUG
    return;
#endif
    // If the hash/eq functors are known to be consistent, then skip validation.
    if (std::is_same<hasher, absl::container_internal::StringHash>::value &&
        std::is_same<key_equal, absl::container_internal::StringEq>::value) {
      return;
    }
    if (std::is_scalar<key_type>::value &&
        std::is_same<hasher, absl::Hash<key_type>>::value &&
        std::is_same<key_equal, std::equal_to<key_type>>::value) {
      return;
    }
    if (empty()) return;

    const size_t hash_of_arg = hash_ref()(key);
    const auto assert_consistent = [&](const ctrl_t*, slot_type* slot) {
      const value_type& element = PolicyTraits::element(slot);
      const bool is_key_equal =
          PolicyTraits::apply(EqualElement<K>{key, eq_ref()}, element);
      if (!is_key_equal) return;

      const size_t hash_of_slot =
          PolicyTraits::apply(HashElement{hash_ref()}, element);
      ABSL_ATTRIBUTE_UNUSED const bool is_hash_equal =
          hash_of_arg == hash_of_slot;
      assert((!is_key_equal || is_hash_equal) &&
             "eq(k1, k2) must imply that hash(k1) == hash(k2). "
             "hash/eq functors are inconsistent.");
    };

    if (is_soo()) {
      assert_consistent(/*unused*/ nullptr, soo_slot());
      return;
    }
    // We only do validation for small tables so that it's constant time.
    if (capacity() > 16) return;
    IterateOverFullSlots(common(), slot_array(), assert_consistent);
  }

  // Attempts to find `key` in the table; if it isn't found, returns an iterator
  // where the value can be inserted into, with the control byte already set to
  // `key`'s H2. Returns a bool indicating whether an insertion can take place.
  template <class K>
  std::pair<iterator, bool> find_or_prepare_insert(const K& key) {
    AssertOnFind(key);
    if (is_soo()) return find_or_prepare_insert_soo(key);
    return find_or_prepare_insert_non_soo(key);
  }

  // Constructs the value in the space pointed by the iterator. This only works
  // after an unsuccessful find_or_prepare_insert() and before any other
  // modifications happen in the raw_hash_set.
  //
  // PRECONDITION: iter was returned from find_or_prepare_insert(k), where k is
  // the key decomposed from `forward<Args>(args)...`, and the bool returned by
  // find_or_prepare_insert(k) was true.
  // POSTCONDITION: *m.iterator_at(i) == value_type(forward<Args>(args)...).
  template <class... Args>
  void emplace_at(iterator iter, Args&&... args) {
    construct(iter.slot(), std::forward<Args>(args)...);

    assert(PolicyTraits::apply(FindElement{*this}, *iter) == iter &&
           "constructed value does not match the lookup key");
  }

  iterator iterator_at(size_t i) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return {control() + i, slot_array() + i, common().generation_ptr()};
  }
  const_iterator iterator_at(size_t i) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_cast<raw_hash_set*>(this)->iterator_at(i);
  }

  reference unchecked_deref(iterator it) { return it.unchecked_deref(); }

 private:
  friend struct RawHashSetTestOnlyAccess;

  // The number of slots we can still fill without needing to rehash.
  //
  // This is stored separately due to tombstones: we do not include tombstones
  // in the growth capacity, because we'd like to rehash when the table is
  // otherwise filled with tombstones: otherwise, probe sequences might get
  // unacceptably long without triggering a rehash. Callers can also force a
  // rehash via the standard `rehash(0)`, which will recompute this value as a
  // side-effect.
  //
  // See `CapacityToGrowth()`.
  size_t growth_left() const {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    return common().growth_left();
  }

  GrowthInfo& growth_info() {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    return common().growth_info();
  }
  GrowthInfo growth_info() const {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    return common().growth_info();
  }

  // Prefetch the heap-allocated memory region to resolve potential TLB and
  // cache misses. This is intended to overlap with execution of calculating the
  // hash for a key.
  void prefetch_heap_block() const {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
#if ABSL_HAVE_BUILTIN(__builtin_prefetch) || defined(__GNUC__)
    __builtin_prefetch(control(), 0, 1);
#endif
  }

  CommonFields& common() { return settings_.template get<0>(); }
  const CommonFields& common() const { return settings_.template get<0>(); }

  ctrl_t* control() const {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    return common().control();
  }
  slot_type* slot_array() const {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    return static_cast<slot_type*>(common().slot_array());
  }
  slot_type* soo_slot() {
    ABSL_SWISSTABLE_ASSERT(is_soo());
    return static_cast<slot_type*>(common().soo_data());
  }
  const slot_type* soo_slot() const {
    return const_cast<raw_hash_set*>(this)->soo_slot();
  }
  iterator soo_iterator() {
    return {SooControl(), soo_slot(), common().generation_ptr()};
  }
  const_iterator soo_iterator() const {
    return const_cast<raw_hash_set*>(this)->soo_iterator();
  }
  HashtablezInfoHandle infoz() {
    ABSL_SWISSTABLE_ASSERT(!is_soo());
    return common().infoz();
  }

  hasher& hash_ref() { return settings_.template get<1>(); }
  const hasher& hash_ref() const { return settings_.template get<1>(); }
  key_equal& eq_ref() { return settings_.template get<2>(); }
  const key_equal& eq_ref() const { return settings_.template get<2>(); }
  allocator_type& alloc_ref() { return settings_.template get<3>(); }
  const allocator_type& alloc_ref() const {
    return settings_.template get<3>();
  }

  static const void* get_hash_ref_fn(const CommonFields& common) {
    auto* h = reinterpret_cast<const raw_hash_set*>(&common);
    return &h->hash_ref();
  }
  static void transfer_slot_fn(void* set, void* dst, void* src) {
    auto* h = static_cast<raw_hash_set*>(set);
    h->transfer(static_cast<slot_type*>(dst), static_cast<slot_type*>(src));
  }
  // Note: dealloc_fn will only be used if we have a non-standard allocator.
  static void dealloc_fn(CommonFields& common, const PolicyFunctions&) {
    auto* set = reinterpret_cast<raw_hash_set*>(&common);

    // Unpoison before returning the memory to the allocator.
    SanitizerUnpoisonMemoryRegion(common.slot_array(),
                                  sizeof(slot_type) * common.capacity());

    common.infoz().Unregister();
    Deallocate<BackingArrayAlignment(alignof(slot_type))>(
        &set->alloc_ref(), common.backing_array_start(),
        common.alloc_size(sizeof(slot_type), alignof(slot_type)));
  }

  static const PolicyFunctions& GetPolicyFunctions() {
    static constexpr PolicyFunctions value = {
        sizeof(slot_type),
        // TODO(b/328722020): try to type erase
        // for standard layout and alignof(Hash) <= alignof(CommonFields).
        std::is_empty<hasher>::value ? &GetHashRefForEmptyHasher
                                     : &raw_hash_set::get_hash_ref_fn,
        PolicyTraits::template get_hash_slot_fn<hasher>(),
        PolicyTraits::transfer_uses_memcpy()
            ? TransferRelocatable<sizeof(slot_type)>
            : &raw_hash_set::transfer_slot_fn,
        (std::is_same<SlotAlloc, std::allocator<slot_type>>::value
             ? &DeallocateStandard<alignof(slot_type)>
             : &raw_hash_set::dealloc_fn),
        &raw_hash_set::resize_impl
    };
    return value;
  }

  // Bundle together CommonFields plus other objects which might be empty.
  // CompressedTuple will ensure that sizeof is not affected by any of the empty
  // fields that occur after CommonFields.
  absl::container_internal::CompressedTuple<CommonFields, hasher, key_equal,
                                            allocator_type>
      settings_{CommonFields::CreateDefault<SooEnabled()>(), hasher{},
                key_equal{}, allocator_type{}};
};

// Friend access for free functions in raw_hash_set.h.
struct HashtableFreeFunctionsAccess {
  template <class Predicate, typename Set>
  static typename Set::size_type EraseIf(Predicate& pred, Set* c) {
    if (c->empty()) {
      return 0;
    }
    if (c->is_soo()) {
      auto it = c->soo_iterator();
      if (!pred(*it)) {
        ABSL_SWISSTABLE_ASSERT(c->size() == 1 &&
                               "hash table was modified unexpectedly");
        return 0;
      }
      c->destroy(it.slot());
      c->common().set_empty_soo();
      return 1;
    }
    ABSL_ATTRIBUTE_UNUSED const size_t original_size_for_assert = c->size();
    size_t num_deleted = 0;
    IterateOverFullSlots(
        c->common(), c->slot_array(), [&](const ctrl_t* ctrl, auto* slot) {
          if (pred(Set::PolicyTraits::element(slot))) {
            c->destroy(slot);
            EraseMetaOnly(c->common(), static_cast<size_t>(ctrl - c->control()),
                          sizeof(*slot));
            ++num_deleted;
          }
        });
    // NOTE: IterateOverFullSlots allow removal of the current element, so we
    // verify the size additionally here.
    ABSL_SWISSTABLE_ASSERT(original_size_for_assert - num_deleted ==
                               c->size() &&
                           "hash table was modified unexpectedly");
    return num_deleted;
  }

  template <class Callback, typename Set>
  static void ForEach(Callback& cb, Set* c) {
    if (c->empty()) {
      return;
    }
    if (c->is_soo()) {
      cb(*c->soo_iterator());
      return;
    }
    using ElementTypeWithConstness = decltype(*c->begin());
    IterateOverFullSlots(
        c->common(), c->slot_array(), [&cb](const ctrl_t*, auto* slot) {
          ElementTypeWithConstness& element = Set::PolicyTraits::element(slot);
          cb(element);
        });
  }
};

// Erases all elements that satisfy the predicate `pred` from the container `c`.
template <typename P, typename H, typename E, typename A, typename Predicate>
typename raw_hash_set<P, H, E, A>::size_type EraseIf(
    Predicate& pred, raw_hash_set<P, H, E, A>* c) {
  return HashtableFreeFunctionsAccess::EraseIf(pred, c);
}

// Calls `cb` for all elements in the container `c`.
template <typename P, typename H, typename E, typename A, typename Callback>
void ForEach(Callback& cb, raw_hash_set<P, H, E, A>* c) {
  return HashtableFreeFunctionsAccess::ForEach(cb, c);
}
template <typename P, typename H, typename E, typename A, typename Callback>
void ForEach(Callback& cb, const raw_hash_set<P, H, E, A>* c) {
  return HashtableFreeFunctionsAccess::ForEach(cb, c);
}

namespace hashtable_debug_internal {
template <typename Set>
struct HashtableDebugAccess<Set, absl::void_t<typename Set::raw_hash_set>> {
  using Traits = typename Set::PolicyTraits;
  using Slot = typename Traits::slot_type;

  static size_t GetNumProbes(const Set& set,
                             const typename Set::key_type& key) {
    if (set.is_soo()) return 0;
    size_t num_probes = 0;
    size_t hash = set.hash_ref()(key);
    auto seq = probe(set.common(), hash);
    const ctrl_t* ctrl = set.control();
    while (true) {
      container_internal::Group g{ctrl + seq.offset()};
      for (uint32_t i : g.Match(container_internal::H2(hash))) {
        if (Traits::apply(
                typename Set::template EqualElement<typename Set::key_type>{
                    key, set.eq_ref()},
                Traits::element(set.slot_array() + seq.offset(i))))
          return num_probes;
        ++num_probes;
      }
      if (g.MaskEmpty()) return num_probes;
      seq.next();
      ++num_probes;
    }
  }

  static size_t AllocatedByteSize(const Set& c) {
    size_t capacity = c.capacity();
    if (capacity == 0) return 0;
    size_t m =
        c.is_soo() ? 0 : c.common().alloc_size(sizeof(Slot), alignof(Slot));

    size_t per_slot = Traits::space_used(static_cast<const Slot*>(nullptr));
    if (per_slot != ~size_t{}) {
      m += per_slot * c.size();
    } else {
      for (auto it = c.begin(); it != c.end(); ++it) {
        m += Traits::space_used(it.slot());
      }
    }
    return m;
  }
};

}  // namespace hashtable_debug_internal
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#undef ABSL_SWISSTABLE_ENABLE_GENERATIONS
#undef ABSL_SWISSTABLE_IGNORE_UNINITIALIZED
#undef ABSL_SWISSTABLE_IGNORE_UNINITIALIZED_RETURN
#undef ABSL_SWISSTABLE_ASSERT

#endif  // ABSL_CONTAINER_INTERNAL_RAW_HASH_SET_H_
