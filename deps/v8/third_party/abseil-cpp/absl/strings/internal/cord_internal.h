// Copyright 2021 The Abseil Authors.
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

#ifndef ABSL_STRINGS_INTERNAL_CORD_INTERNAL_H_
#define ABSL_STRINGS_INTERNAL_CORD_INTERNAL_H_

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/internal/invoke.h"
#include "absl/base/optimization.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/container/internal/container_memory.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"

// We can only add poisoning if we can detect consteval executions.
#if defined(ABSL_HAVE_CONSTANT_EVALUATED) && \
    (defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
     defined(ABSL_HAVE_MEMORY_SANITIZER))
#define ABSL_INTERNAL_CORD_HAVE_SANITIZER 1
#endif

#define ABSL_CORD_INTERNAL_NO_SANITIZE \
  ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// The overhead of a vtable is too much for Cord, so we roll our own subclasses
// using only a single byte to differentiate classes from each other - the "tag"
// byte.  Define the subclasses first so we can provide downcasting helper
// functions in the base class.
struct CordRep;
struct CordRepConcat;
struct CordRepExternal;
struct CordRepFlat;
struct CordRepSubstring;
struct CordRepCrc;
class CordRepBtree;

class CordzInfo;

// Default feature enable states for cord ring buffers
enum CordFeatureDefaults { kCordShallowSubcordsDefault = false };

extern std::atomic<bool> shallow_subcords_enabled;

inline void enable_shallow_subcords(bool enable) {
  shallow_subcords_enabled.store(enable, std::memory_order_relaxed);
}

enum Constants {
  // The inlined size to use with absl::InlinedVector.
  //
  // Note: The InlinedVectors in this file (and in cord.h) do not need to use
  // the same value for their inlined size. The fact that they do is historical.
  // It may be desirable for each to use a different inlined size optimized for
  // that InlinedVector's usage.
  //
  // TODO(jgm): Benchmark to see if there's a more optimal value than 47 for
  // the inlined vector size (47 exists for backward compatibility).
  kInlinedVectorSize = 47,

  // Prefer copying blocks of at most this size, otherwise reference count.
  kMaxBytesToCopy = 511
};

// Emits a fatal error "Unexpected node type: xyz" and aborts the program.
ABSL_ATTRIBUTE_NORETURN void LogFatalNodeType(CordRep* rep);

// Fast implementation of memmove for up to 15 bytes. This implementation is
// safe for overlapping regions. If nullify_tail is true, the destination is
// padded with '\0' up to 15 bytes.
template <bool nullify_tail = false>
inline void SmallMemmove(char* dst, const char* src, size_t n) {
  if (n >= 8) {
    assert(n <= 15);
    uint64_t buf1;
    uint64_t buf2;
    memcpy(&buf1, src, 8);
    memcpy(&buf2, src + n - 8, 8);
    if (nullify_tail) {
      memset(dst + 7, 0, 8);
    }
    // GCC 12 has a false-positive -Wstringop-overflow warning here.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    memcpy(dst, &buf1, 8);
    memcpy(dst + n - 8, &buf2, 8);
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif
  } else if (n >= 4) {
    uint32_t buf1;
    uint32_t buf2;
    memcpy(&buf1, src, 4);
    memcpy(&buf2, src + n - 4, 4);
    if (nullify_tail) {
      memset(dst + 4, 0, 4);
      memset(dst + 7, 0, 8);
    }
    memcpy(dst, &buf1, 4);
    memcpy(dst + n - 4, &buf2, 4);
  } else {
    if (n != 0) {
      dst[0] = src[0];
      dst[n / 2] = src[n / 2];
      dst[n - 1] = src[n - 1];
    }
    if (nullify_tail) {
      memset(dst + 7, 0, 8);
      memset(dst + n, 0, 8);
    }
  }
}

// Compact class for tracking the reference count and state flags for CordRep
// instances.  Data is stored in an atomic int32_t for compactness and speed.
class RefcountAndFlags {
 public:
  constexpr RefcountAndFlags() : count_{kRefIncrement} {}
  struct Immortal {};
  explicit constexpr RefcountAndFlags(Immortal) : count_(kImmortalFlag) {}

  // Increments the reference count. Imposes no memory ordering.
  inline void Increment() {
    count_.fetch_add(kRefIncrement, std::memory_order_relaxed);
  }

  // Asserts that the current refcount is greater than 0. If the refcount is
  // greater than 1, decrements the reference count.
  //
  // Returns false if there are no references outstanding; true otherwise.
  // Inserts barriers to ensure that state written before this method returns
  // false will be visible to a thread that just observed this method returning
  // false.  Always returns false when the immortal bit is set.
  inline bool Decrement() {
    int32_t refcount = count_.load(std::memory_order_acquire);
    assert(refcount > 0 || refcount & kImmortalFlag);
    return refcount != kRefIncrement &&
           count_.fetch_sub(kRefIncrement, std::memory_order_acq_rel) !=
               kRefIncrement;
  }

  // Same as Decrement but expect that refcount is greater than 1.
  inline bool DecrementExpectHighRefcount() {
    int32_t refcount =
        count_.fetch_sub(kRefIncrement, std::memory_order_acq_rel);
    assert(refcount > 0 || refcount & kImmortalFlag);
    return refcount != kRefIncrement;
  }

  // Returns the current reference count using acquire semantics.
  inline size_t Get() const {
    return static_cast<size_t>(count_.load(std::memory_order_acquire) >>
                               kNumFlags);
  }

  // Returns whether the atomic integer is 1.
  // If the reference count is used in the conventional way, a
  // reference count of 1 implies that the current thread owns the
  // reference and no other thread shares it.
  // This call performs the test for a reference count of one, and
  // performs the memory barrier needed for the owning thread
  // to act on the object, knowing that it has exclusive access to the
  // object. Always returns false when the immortal bit is set.
  inline bool IsOne() {
    return count_.load(std::memory_order_acquire) == kRefIncrement;
  }

  bool IsImmortal() const {
    return (count_.load(std::memory_order_relaxed) & kImmortalFlag) != 0;
  }

 private:
  // We reserve the bottom bit for flag.
  // kImmortalBit indicates that this entity should never be collected; it is
  // used for the StringConstant constructor to avoid collecting immutable
  // constant cords.
  enum Flags {
    kNumFlags = 1,

    kImmortalFlag = 0x1,
    kRefIncrement = (1 << kNumFlags),
  };

  std::atomic<int32_t> count_;
};

// Various representations that we allow
enum CordRepKind {
  UNUSED_0 = 0,
  SUBSTRING = 1,
  CRC = 2,
  BTREE = 3,
  UNUSED_4 = 4,
  EXTERNAL = 5,

  // We have different tags for different sized flat arrays,
  // starting with FLAT, and limited to MAX_FLAT_TAG. The below values map to an
  // allocated range of 32 bytes to 256 KB. The current granularity is:
  // - 8 byte granularity for flat sizes in [32 - 512]
  // - 64 byte granularity for flat sizes in (512 - 8KiB]
  // - 4KiB byte granularity for flat sizes in (8KiB, 256 KiB]
  // If a new tag is needed in the future, then 'FLAT' and 'MAX_FLAT_TAG' should
  // be adjusted as well as the Tag <---> Size mapping logic so that FLAT still
  // represents the minimum flat allocation size. (32 bytes as of now).
  FLAT = 6,
  MAX_FLAT_TAG = 248
};

// There are various locations where we want to check if some rep is a 'plain'
// data edge, i.e. an external or flat rep. By having FLAT == EXTERNAL + 1, we
// can perform this check in a single branch as 'tag >= EXTERNAL'
// Note that we can leave this optimization to the compiler. The compiler will
// DTRT when it sees a condition like `tag == EXTERNAL || tag >= FLAT`.
static_assert(FLAT == EXTERNAL + 1, "EXTERNAL and FLAT not consecutive");

struct CordRep {
  // Result from an `extract edge` operation. Contains the (possibly changed)
  // tree node as well as the extracted edge, or {tree, nullptr} if no edge
  // could be extracted.
  // On success, the returned `tree` value is null if `extracted` was the only
  // data edge inside the tree, a data edge if there were only two data edges in
  // the tree, or the (possibly new / smaller) remaining tree with the extracted
  // data edge removed.
  struct ExtractResult {
    CordRep* tree;
    CordRep* extracted;
  };

  CordRep() = default;
  constexpr CordRep(RefcountAndFlags::Immortal immortal, size_t l)
      : length(l), refcount(immortal), tag(EXTERNAL), storage{} {}

  // The following three fields have to be less than 32 bytes since
  // that is the smallest supported flat node size. Some code optimizations rely
  // on the specific layout of these fields. Notably: the non-trivial field
  // `refcount` being preceded by `length`, and being tailed by POD data
  // members only.
  // # LINT.IfChange
  size_t length;
  RefcountAndFlags refcount;
  // If tag < FLAT, it represents CordRepKind and indicates the type of node.
  // Otherwise, the node type is CordRepFlat and the tag is the encoded size.
  uint8_t tag;

  // `storage` provides two main purposes:
  // - the starting point for FlatCordRep.Data() [flexible-array-member]
  // - 3 bytes of additional storage for use by derived classes.
  // The latter is used by CordrepConcat and CordRepBtree. CordRepConcat stores
  // a 'depth' value in storage[0], and the (future) CordRepBtree class stores
  // `height`, `begin` and `end` in the 3 entries. Otherwise we would need to
  // allocate room for these in the derived class, as not all compilers reuse
  // padding space from the base class (clang and gcc do, MSVC does not, etc)
  uint8_t storage[3];
  // # LINT.ThenChange(cord_rep_btree.h:copy_raw)

  // Returns true if this instance's tag matches the requested type.
  constexpr bool IsSubstring() const { return tag == SUBSTRING; }
  constexpr bool IsCrc() const { return tag == CRC; }
  constexpr bool IsExternal() const { return tag == EXTERNAL; }
  constexpr bool IsFlat() const { return tag >= FLAT; }
  constexpr bool IsBtree() const { return tag == BTREE; }

  inline CordRepSubstring* substring();
  inline const CordRepSubstring* substring() const;
  inline CordRepCrc* crc();
  inline const CordRepCrc* crc() const;
  inline CordRepExternal* external();
  inline const CordRepExternal* external() const;
  inline CordRepFlat* flat();
  inline const CordRepFlat* flat() const;
  inline CordRepBtree* btree();
  inline const CordRepBtree* btree() const;

  // --------------------------------------------------------------------
  // Memory management

  // Destroys the provided `rep`.
  static void Destroy(CordRep* rep);

  // Increments the reference count of `rep`.
  // Requires `rep` to be a non-null pointer value.
  static inline CordRep* Ref(CordRep* rep);

  // Decrements the reference count of `rep`. Destroys rep if count reaches
  // zero. Requires `rep` to be a non-null pointer value.
  static inline void Unref(CordRep* rep);
};

struct CordRepSubstring : public CordRep {
  size_t start;  // Starting offset of substring in child
  CordRep* child;

  // Creates a substring on `child`, adopting a reference on `child`.
  // Requires `child` to be either a flat or external node, and `pos` and `n` to
  // form a non-empty partial sub range of `'child`, i.e.:
  // `n > 0 && n < length && n + pos <= length`
  static inline CordRepSubstring* Create(CordRep* child, size_t pos, size_t n);

  // Creates a substring of `rep`. Does not adopt a reference on `rep`.
  // Requires `IsDataEdge(rep) && n > 0 && pos + n <= rep->length`.
  // If `n == rep->length` then this method returns `CordRep::Ref(rep)`
  // If `rep` is a substring of a flat or external node, then this method will
  // return a new substring of that flat or external node with `pos` adjusted
  // with the original `start` position.
  static inline CordRep* Substring(CordRep* rep, size_t pos, size_t n);
};

// Type for function pointer that will invoke the releaser function and also
// delete the `CordRepExternalImpl` corresponding to the passed in
// `CordRepExternal`.
using ExternalReleaserInvoker = void (*)(CordRepExternal*);

// External CordReps are allocated together with a type erased releaser. The
// releaser is stored in the memory directly following the CordRepExternal.
struct CordRepExternal : public CordRep {
  CordRepExternal() = default;
  explicit constexpr CordRepExternal(absl::string_view str)
      : CordRep(RefcountAndFlags::Immortal{}, str.size()),
        base(str.data()),
        releaser_invoker(nullptr) {}

  const char* base;
  // Pointer to function that knows how to call and destroy the releaser.
  ExternalReleaserInvoker releaser_invoker;

  // Deletes (releases) the external rep.
  // Requires rep != nullptr and rep->IsExternal()
  static void Delete(CordRep* rep);
};

// Use go/ranked-overloads for dispatching.
struct Rank0 {};
struct Rank1 : Rank0 {};

template <typename Releaser, typename = ::absl::base_internal::invoke_result_t<
                                 Releaser, absl::string_view>>
void InvokeReleaser(Rank1, Releaser&& releaser, absl::string_view data) {
  ::absl::base_internal::invoke(std::forward<Releaser>(releaser), data);
}

template <typename Releaser,
          typename = ::absl::base_internal::invoke_result_t<Releaser>>
void InvokeReleaser(Rank0, Releaser&& releaser, absl::string_view) {
  ::absl::base_internal::invoke(std::forward<Releaser>(releaser));
}

// We use CompressedTuple so that we can benefit from EBCO.
template <typename Releaser>
struct CordRepExternalImpl
    : public CordRepExternal,
      public ::absl::container_internal::CompressedTuple<Releaser> {
  // The extra int arg is so that we can avoid interfering with copy/move
  // constructors while still benefitting from perfect forwarding.
  template <typename T>
  CordRepExternalImpl(T&& releaser, int)
      : CordRepExternalImpl::CompressedTuple(std::forward<T>(releaser)) {
    this->releaser_invoker = &Release;
  }

  ~CordRepExternalImpl() {
    InvokeReleaser(Rank1{}, std::move(this->template get<0>()),
                   absl::string_view(base, length));
  }

  static void Release(CordRepExternal* rep) {
    delete static_cast<CordRepExternalImpl*>(rep);
  }
};

inline CordRepSubstring* CordRepSubstring::Create(CordRep* child, size_t pos,
                                                  size_t n) {
  assert(child != nullptr);
  assert(n > 0);
  assert(n < child->length);
  assert(pos < child->length);
  assert(n <= child->length - pos);

  // TODO(b/217376272): Harden internal logic.
  // Move to strategical places inside the Cord logic and make this an assert.
  if (ABSL_PREDICT_FALSE(!(child->IsExternal() || child->IsFlat()))) {
    LogFatalNodeType(child);
  }

  CordRepSubstring* rep = new CordRepSubstring();
  rep->length = n;
  rep->tag = SUBSTRING;
  rep->start = pos;
  rep->child = child;
  return rep;
}

inline CordRep* CordRepSubstring::Substring(CordRep* rep, size_t pos,
                                            size_t n) {
  assert(rep != nullptr);
  assert(n != 0);
  assert(pos < rep->length);
  assert(n <= rep->length - pos);
  if (n == rep->length) return CordRep::Ref(rep);
  if (rep->IsSubstring()) {
    pos += rep->substring()->start;
    rep = rep->substring()->child;
  }
  CordRepSubstring* substr = new CordRepSubstring();
  substr->length = n;
  substr->tag = SUBSTRING;
  substr->start = pos;
  substr->child = CordRep::Ref(rep);
  return substr;
}

inline void CordRepExternal::Delete(CordRep* rep) {
  assert(rep != nullptr && rep->IsExternal());
  auto* rep_external = static_cast<CordRepExternal*>(rep);
  assert(rep_external->releaser_invoker != nullptr);
  rep_external->releaser_invoker(rep_external);
}

template <typename Str>
struct ConstInitExternalStorage {
  ABSL_CONST_INIT static CordRepExternal value;
};

template <typename Str>
ABSL_CONST_INIT CordRepExternal
    ConstInitExternalStorage<Str>::value(Str::value);

enum {
  kMaxInline = 15,
};

constexpr char GetOrNull(absl::string_view data, size_t pos) {
  return pos < data.size() ? data[pos] : '\0';
}

// We store cordz_info as 64 bit pointer value in little endian format. This
// guarantees that the least significant byte of cordz_info matches the first
// byte of the inline data representation in `data`, which holds the inlined
// size or the 'is_tree' bit.
using cordz_info_t = int64_t;

// Assert that the `cordz_info` pointer value perfectly overlaps the last half
// of `data` and can hold a pointer value.
static_assert(sizeof(cordz_info_t) * 2 == kMaxInline + 1, "");
static_assert(sizeof(cordz_info_t) >= sizeof(intptr_t), "");

// LittleEndianByte() creates a little endian representation of 'value', i.e.:
// a little endian value where the first byte in the host's representation
// holds 'value`, with all other bytes being 0.
static constexpr cordz_info_t LittleEndianByte(unsigned char value) {
#if defined(ABSL_IS_BIG_ENDIAN)
  return static_cast<cordz_info_t>(value) << ((sizeof(cordz_info_t) - 1) * 8);
#else
  return value;
#endif
}

class InlineData {
 public:
  // DefaultInitType forces the use of the default initialization constructor.
  enum DefaultInitType { kDefaultInit };

  // kNullCordzInfo holds the little endian representation of intptr_t(1)
  // This is the 'null' / initial value of 'cordz_info'. The null value
  // is specifically big endian 1 as with 64-bit pointers, the last
  // byte of cordz_info overlaps with the last byte holding the tag.
  static constexpr cordz_info_t kNullCordzInfo = LittleEndianByte(1);

  // kTagOffset contains the offset of the control byte / tag. This constant is
  // intended mostly for debugging purposes: do not remove this constant as it
  // is actively inspected and used by gdb pretty printing code.
  static constexpr size_t kTagOffset = 0;

  // Implement `~InlineData()` conditionally: we only need this destructor to
  // unpoison poisoned instances under *SAN, and it will only compile correctly
  // if the current compiler supports `absl::is_constant_evaluated()`.
#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER
  ~InlineData() noexcept { unpoison(); }
#endif

  constexpr InlineData() noexcept { poison_this(); }

  explicit InlineData(DefaultInitType) noexcept : rep_(kDefaultInit) {
    poison_this();
  }

  explicit InlineData(CordRep* rep) noexcept : rep_(rep) {
    ABSL_ASSERT(rep != nullptr);
  }

  // Explicit constexpr constructor to create a constexpr InlineData
  // value. Creates an inlined SSO value if `rep` is null, otherwise
  // creates a tree instance value.
  constexpr InlineData(absl::string_view sv, CordRep* rep) noexcept
      : rep_(rep ? Rep(rep) : Rep(sv)) {
    poison();
  }

  constexpr InlineData(const InlineData& rhs) noexcept;
  InlineData& operator=(const InlineData& rhs) noexcept;
  friend void swap(InlineData& lhs, InlineData& rhs) noexcept;

  friend bool operator==(const InlineData& lhs, const InlineData& rhs) {
#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER
    const Rep l = lhs.rep_.SanitizerSafeCopy();
    const Rep r = rhs.rep_.SanitizerSafeCopy();
    return memcmp(&l, &r, sizeof(l)) == 0;
#else
    return memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
#endif
  }
  friend bool operator!=(const InlineData& lhs, const InlineData& rhs) {
    return !operator==(lhs, rhs);
  }

  // Poisons the unused inlined SSO data if the current instance
  // is inlined, else un-poisons the entire instance.
  constexpr void poison();

  // Un-poisons this instance.
  constexpr void unpoison();

  // Poisons the current instance. This is used on default initialization.
  constexpr void poison_this();

  // Returns true if the current instance is empty.
  // The 'empty value' is an inlined data value of zero length.
  bool is_empty() const { return rep_.tag() == 0; }

  // Returns true if the current instance holds a tree value.
  bool is_tree() const { return (rep_.tag() & 1) != 0; }

  // Returns true if the current instance holds a cordz_info value.
  // Requires the current instance to hold a tree value.
  bool is_profiled() const {
    assert(is_tree());
    return rep_.cordz_info() != kNullCordzInfo;
  }

  // Returns true if either of the provided instances hold a cordz_info value.
  // This method is more efficient than the equivalent `data1.is_profiled() ||
  // data2.is_profiled()`. Requires both arguments to hold a tree.
  static bool is_either_profiled(const InlineData& data1,
                                 const InlineData& data2) {
    assert(data1.is_tree() && data2.is_tree());
    return (data1.rep_.cordz_info() | data2.rep_.cordz_info()) !=
           kNullCordzInfo;
  }

  // Returns the cordz_info sampling instance for this instance, or nullptr
  // if the current instance is not sampled and does not have CordzInfo data.
  // Requires the current instance to hold a tree value.
  CordzInfo* cordz_info() const {
    assert(is_tree());
    intptr_t info = static_cast<intptr_t>(absl::little_endian::ToHost64(
        static_cast<uint64_t>(rep_.cordz_info())));
    assert(info & 1);
    return reinterpret_cast<CordzInfo*>(info - 1);
  }

  // Sets the current cordz_info sampling instance for this instance, or nullptr
  // if the current instance is not sampled and does not have CordzInfo data.
  // Requires the current instance to hold a tree value.
  void set_cordz_info(CordzInfo* cordz_info) {
    assert(is_tree());
    uintptr_t info = reinterpret_cast<uintptr_t>(cordz_info) | 1;
    rep_.set_cordz_info(
        static_cast<cordz_info_t>(absl::little_endian::FromHost64(info)));
  }

  // Resets the current cordz_info to null / empty.
  void clear_cordz_info() {
    assert(is_tree());
    rep_.set_cordz_info(kNullCordzInfo);
  }

  // Returns a read only pointer to the character data inside this instance.
  // Requires the current instance to hold inline data.
  const char* as_chars() const {
    assert(!is_tree());
    return rep_.as_chars();
  }

  // Returns a mutable pointer to the character data inside this instance.
  // Should be used for 'write only' operations setting an inlined value.
  // Applications can set the value of inlined data either before or after
  // setting the inlined size, i.e., both of the below are valid:
  //
  //   // Set inlined data and inline size
  //   memcpy(data_.as_chars(), data, size);
  //   data_.set_inline_size(size);
  //
  //   // Set inlined size and inline data
  //   data_.set_inline_size(size);
  //   memcpy(data_.as_chars(), data, size);
  //
  // It's an error to read from the returned pointer without a preceding write
  // if the current instance does not hold inline data, i.e.: is_tree() == true.
  char* as_chars() { return rep_.as_chars(); }

  // Returns the tree value of this value.
  // Requires the current instance to hold a tree value.
  CordRep* as_tree() const {
    assert(is_tree());
    return rep_.tree();
  }

  void set_inline_data(const char* data, size_t n) {
    ABSL_ASSERT(n <= kMaxInline);
    unpoison();
    rep_.set_tag(static_cast<int8_t>(n << 1));
    SmallMemmove<true>(rep_.as_chars(), data, n);
    poison();
  }

  void copy_max_inline_to(char* dst) const {
    assert(!is_tree());
    memcpy(dst, rep_.SanitizerSafeCopy().as_chars(), kMaxInline);
  }

  // Initialize this instance to holding the tree value `rep`,
  // initializing the cordz_info to null, i.e.: 'not profiled'.
  void make_tree(CordRep* rep) {
    unpoison();
    rep_.make_tree(rep);
  }

  // Set the tree value of this instance to 'rep`.
  // Requires the current instance to already hold a tree value.
  // Does not affect the value of cordz_info.
  void set_tree(CordRep* rep) {
    assert(is_tree());
    rep_.set_tree(rep);
  }

  // Returns the size of the inlined character data inside this instance.
  // Requires the current instance to hold inline data.
  size_t inline_size() const { return rep_.inline_size(); }

  // Sets the size of the inlined character data inside this instance.
  // Requires `size` to be <= kMaxInline.
  // See the documentation on 'as_chars()' for more information and examples.
  void set_inline_size(size_t size) {
    unpoison();
    rep_.set_inline_size(size);
    poison();
  }

  // Compares 'this' inlined data  with rhs. The comparison is a straightforward
  // lexicographic comparison. `Compare()` returns values as follows:
  //
  //   -1  'this' InlineData instance is smaller
  //    0  the InlineData instances are equal
  //    1  'this' InlineData instance larger
  int Compare(const InlineData& rhs) const {
    return Compare(rep_.SanitizerSafeCopy(), rhs.rep_.SanitizerSafeCopy());
  }

 private:
  struct Rep {
    // See cordz_info_t for forced alignment and size of `cordz_info` details.
    struct AsTree {
      explicit constexpr AsTree(absl::cord_internal::CordRep* tree)
          : rep(tree) {}
      cordz_info_t cordz_info = kNullCordzInfo;
      absl::cord_internal::CordRep* rep;
    };

    explicit Rep(DefaultInitType) {}
    constexpr Rep() : data{0} {}
    constexpr Rep(const Rep&) = default;
    constexpr Rep& operator=(const Rep&) = default;

    explicit constexpr Rep(CordRep* rep) : as_tree(rep) {}

    explicit constexpr Rep(absl::string_view chars)
        : data{static_cast<char>((chars.size() << 1)),
               GetOrNull(chars, 0),
               GetOrNull(chars, 1),
               GetOrNull(chars, 2),
               GetOrNull(chars, 3),
               GetOrNull(chars, 4),
               GetOrNull(chars, 5),
               GetOrNull(chars, 6),
               GetOrNull(chars, 7),
               GetOrNull(chars, 8),
               GetOrNull(chars, 9),
               GetOrNull(chars, 10),
               GetOrNull(chars, 11),
               GetOrNull(chars, 12),
               GetOrNull(chars, 13),
               GetOrNull(chars, 14)} {}

    // Disable sanitizer as we must always be able to read `tag`.
    ABSL_CORD_INTERNAL_NO_SANITIZE
    int8_t tag() const { return reinterpret_cast<const int8_t*>(this)[0]; }
    void set_tag(int8_t rhs) { reinterpret_cast<int8_t*>(this)[0] = rhs; }

    char* as_chars() { return data + 1; }
    const char* as_chars() const { return data + 1; }

    bool is_tree() const { return (tag() & 1) != 0; }

    size_t inline_size() const {
      ABSL_ASSERT(!is_tree());
      return static_cast<size_t>(tag()) >> 1;
    }

    void set_inline_size(size_t size) {
      ABSL_ASSERT(size <= kMaxInline);
      set_tag(static_cast<int8_t>(size << 1));
    }

    CordRep* tree() const { return as_tree.rep; }
    void set_tree(CordRep* rhs) { as_tree.rep = rhs; }

    cordz_info_t cordz_info() const { return as_tree.cordz_info; }
    void set_cordz_info(cordz_info_t rhs) { as_tree.cordz_info = rhs; }

    void make_tree(CordRep* tree) {
      as_tree.rep = tree;
      as_tree.cordz_info = kNullCordzInfo;
    }

#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER
    constexpr Rep SanitizerSafeCopy() const {
      if (!absl::is_constant_evaluated()) {
        Rep res;
        if (is_tree()) {
          res = *this;
        } else {
          res.set_tag(tag());
          memcpy(res.as_chars(), as_chars(), inline_size());
        }
        return res;
      } else {
        return *this;
      }
    }
#else
    constexpr const Rep& SanitizerSafeCopy() const { return *this; }
#endif

    // If the data has length <= kMaxInline, we store it in `data`, and
    // store the size in the first char of `data` shifted left + 1.
    // Else we store it in a tree and store a pointer to that tree in
    // `as_tree.rep` with a tagged pointer to make `tag() & 1` non zero.
    union {
      char data[kMaxInline + 1];
      AsTree as_tree;
    };

    // TODO(b/145829486): see swap(InlineData, InlineData) for more info.
    inline void SwapValue(Rep rhs, Rep& refrhs) {
      memcpy(&refrhs, this, sizeof(*this));
      memcpy(this, &rhs, sizeof(*this));
    }
  };

  // Private implementation of `Compare()`
  static inline int Compare(const Rep& lhs, const Rep& rhs) {
    uint64_t x, y;
    memcpy(&x, lhs.as_chars(), sizeof(x));
    memcpy(&y, rhs.as_chars(), sizeof(y));
    if (x == y) {
      memcpy(&x, lhs.as_chars() + 7, sizeof(x));
      memcpy(&y, rhs.as_chars() + 7, sizeof(y));
      if (x == y) {
        if (lhs.inline_size() == rhs.inline_size()) return 0;
        return lhs.inline_size() < rhs.inline_size() ? -1 : 1;
      }
    }
    x = absl::big_endian::FromHost64(x);
    y = absl::big_endian::FromHost64(y);
    return x < y ? -1 : 1;
  }

  Rep rep_;
};

static_assert(sizeof(InlineData) == kMaxInline + 1, "");

#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER

constexpr InlineData::InlineData(const InlineData& rhs) noexcept
    : rep_(rhs.rep_.SanitizerSafeCopy()) {
  poison();
}

inline InlineData& InlineData::operator=(const InlineData& rhs) noexcept {
  unpoison();
  rep_ = rhs.rep_.SanitizerSafeCopy();
  poison();
  return *this;
}

constexpr void InlineData::poison_this() {
  if (!absl::is_constant_evaluated()) {
    container_internal::SanitizerPoisonObject(this);
  }
}

constexpr void InlineData::unpoison() {
  if (!absl::is_constant_evaluated()) {
    container_internal::SanitizerUnpoisonObject(this);
  }
}

constexpr void InlineData::poison() {
  if (!absl::is_constant_evaluated()) {
    if (is_tree()) {
      container_internal::SanitizerUnpoisonObject(this);
    } else if (const size_t size = inline_size()) {
      if (size < kMaxInline) {
        const char* end = rep_.as_chars() + size;
        container_internal::SanitizerPoisonMemoryRegion(end, kMaxInline - size);
      }
    } else {
      container_internal::SanitizerPoisonObject(this);
    }
  }
}

#else  // ABSL_INTERNAL_CORD_HAVE_SANITIZER

constexpr InlineData::InlineData(const InlineData&) noexcept = default;
inline InlineData& InlineData::operator=(const InlineData&) noexcept = default;

constexpr void InlineData::poison_this() {}
constexpr void InlineData::unpoison() {}
constexpr void InlineData::poison() {}

#endif  // ABSL_INTERNAL_CORD_HAVE_SANITIZER

inline CordRepSubstring* CordRep::substring() {
  assert(IsSubstring());
  return static_cast<CordRepSubstring*>(this);
}

inline const CordRepSubstring* CordRep::substring() const {
  assert(IsSubstring());
  return static_cast<const CordRepSubstring*>(this);
}

inline CordRepExternal* CordRep::external() {
  assert(IsExternal());
  return static_cast<CordRepExternal*>(this);
}

inline const CordRepExternal* CordRep::external() const {
  assert(IsExternal());
  return static_cast<const CordRepExternal*>(this);
}

inline CordRep* CordRep::Ref(CordRep* rep) {
  // ABSL_ASSUME is a workaround for
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105585
  ABSL_ASSUME(rep != nullptr);
  rep->refcount.Increment();
  return rep;
}

inline void CordRep::Unref(CordRep* rep) {
  assert(rep != nullptr);
  // Expect refcount to be 0. Avoiding the cost of an atomic decrement should
  // typically outweigh the cost of an extra branch checking for ref == 1.
  if (ABSL_PREDICT_FALSE(!rep->refcount.DecrementExpectHighRefcount())) {
    Destroy(rep);
  }
}

inline void swap(InlineData& lhs, InlineData& rhs) noexcept {
  lhs.unpoison();
  rhs.unpoison();
  // TODO(b/145829486): `std::swap(lhs.rep_, rhs.rep_)` results in bad codegen
  // on clang, spilling the temporary swap value on the stack. Since `Rep` is
  // trivial, we can make clang DTRT by calling a hand-rolled `SwapValue` where
  // we pass `rhs` both by value (register allocated) and by reference. The IR
  // then folds and inlines correctly into an optimized swap without spill.
  lhs.rep_.SwapValue(rhs.rep_, rhs.rep_);
  rhs.poison();
  lhs.poison();
}

}  // namespace cord_internal

ABSL_NAMESPACE_END
}  // namespace absl
#endif  // ABSL_STRINGS_INTERNAL_CORD_INTERNAL_H_
