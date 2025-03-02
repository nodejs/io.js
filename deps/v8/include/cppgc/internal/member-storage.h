// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_MEMBER_STORAGE_H_
#define INCLUDE_CPPGC_INTERNAL_MEMBER_STORAGE_H_

#include <atomic>
#include <cstddef>
#include <type_traits>

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/caged-heap.h"
#include "cppgc/internal/logging.h"
#include "cppgc/sentinel-pointer.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {
namespace internal {

enum class WriteBarrierSlotType {
  kCompressed,
  kUncompressed,
};

#if defined(CPPGC_POINTER_COMPRESSION)

#if defined(__clang__)
// Attribute const allows the compiler to assume that CageBaseGlobal::g_base_
// doesn't change (e.g. across calls) and thereby avoid redundant loads.
#define CPPGC_CONST __attribute__((const))
#define CPPGC_REQUIRE_CONSTANT_INIT \
  __attribute__((require_constant_initialization))
#else  // defined(__clang__)
#define CPPGC_CONST
#define CPPGC_REQUIRE_CONSTANT_INIT
#endif  // defined(__clang__)

class V8_EXPORT CageBaseGlobal final {
 public:
  V8_INLINE CPPGC_CONST static uintptr_t Get() {
    CPPGC_DCHECK(IsBaseConsistent());
    return g_base_.base;
  }

  V8_INLINE CPPGC_CONST static bool IsSet() {
    CPPGC_DCHECK(IsBaseConsistent());
    return (g_base_.base & ~kLowerHalfWordMask) != 0;
  }

 private:
  // We keep the lower halfword as ones to speed up decompression.
  static constexpr uintptr_t kLowerHalfWordMask =
      (api_constants::kCagedHeapReservationAlignment - 1);

  static union alignas(api_constants::kCachelineSize) Base {
    uintptr_t base;
    char cache_line[api_constants::kCachelineSize];
  } g_base_ CPPGC_REQUIRE_CONSTANT_INIT;

  CageBaseGlobal() = delete;

  V8_INLINE static bool IsBaseConsistent() {
    return kLowerHalfWordMask == (g_base_.base & kLowerHalfWordMask);
  }

  friend class CageBaseGlobalUpdater;
};

#undef CPPGC_REQUIRE_CONSTANT_INIT
#undef CPPGC_CONST

class V8_TRIVIAL_ABI CompressedPointer final {
 public:
  struct AtomicInitializerTag {};

  using IntegralType = uint32_t;
  static constexpr auto kWriteBarrierSlotType =
      WriteBarrierSlotType::kCompressed;

  V8_INLINE CompressedPointer() : value_(0u) {}
  V8_INLINE explicit CompressedPointer(const void* value,
                                       AtomicInitializerTag) {
    StoreAtomic(value);
  }
  V8_INLINE explicit CompressedPointer(const void* ptr)
      : value_(Compress(ptr)) {}
  V8_INLINE explicit CompressedPointer(std::nullptr_t) : value_(0u) {}
  V8_INLINE explicit CompressedPointer(SentinelPointer)
      : value_(kCompressedSentinel) {}

  V8_INLINE const void* Load() const { return Decompress(value_); }
  V8_INLINE const void* LoadAtomic() const {
    return Decompress(
        reinterpret_cast<const std::atomic<IntegralType>&>(value_).load(
            std::memory_order_relaxed));
  }

  V8_INLINE void Store(const void* ptr) { value_ = Compress(ptr); }
  V8_INLINE void StoreAtomic(const void* value) {
    reinterpret_cast<std::atomic<IntegralType>&>(value_).store(
        Compress(value), std::memory_order_relaxed);
  }

  V8_INLINE void Clear() { value_ = 0u; }
  V8_INLINE bool IsCleared() const { return !value_; }

  V8_INLINE bool IsSentinel() const { return value_ == kCompressedSentinel; }

  V8_INLINE uint32_t GetAsInteger() const { return value_; }

  V8_INLINE friend bool operator==(CompressedPointer a, CompressedPointer b) {
    return a.value_ == b.value_;
  }
  V8_INLINE friend bool operator!=(CompressedPointer a, CompressedPointer b) {
    return a.value_ != b.value_;
  }
  V8_INLINE friend bool operator<(CompressedPointer a, CompressedPointer b) {
    return a.value_ < b.value_;
  }
  V8_INLINE friend bool operator<=(CompressedPointer a, CompressedPointer b) {
    return a.value_ <= b.value_;
  }
  V8_INLINE friend bool operator>(CompressedPointer a, CompressedPointer b) {
    return a.value_ > b.value_;
  }
  V8_INLINE friend bool operator>=(CompressedPointer a, CompressedPointer b) {
    return a.value_ >= b.value_;
  }

  static V8_INLINE IntegralType Compress(const void* ptr) {
    static_assert(SentinelPointer::kSentinelValue ==
                      1 << api_constants::kPointerCompressionShift,
                  "The compression scheme relies on the sentinel encoded as 1 "
                  "<< kPointerCompressionShift");
    static constexpr size_t kGigaCageMask =
        ~(api_constants::kCagedHeapReservationAlignment - 1);
    static constexpr size_t kPointerCompressionShiftMask =
        (1 << api_constants::kPointerCompressionShift) - 1;

    CPPGC_DCHECK(CageBaseGlobal::IsSet());
    const uintptr_t base = CageBaseGlobal::Get();
    CPPGC_DCHECK(!ptr || ptr == kSentinelPointer ||
                 (base & kGigaCageMask) ==
                     (reinterpret_cast<uintptr_t>(ptr) & kGigaCageMask));
    CPPGC_DCHECK(
        (reinterpret_cast<uintptr_t>(ptr) & kPointerCompressionShiftMask) == 0);

    const auto uptr = reinterpret_cast<uintptr_t>(ptr);
    // Shift the pointer and truncate.
    auto compressed = static_cast<IntegralType>(
        uptr >> api_constants::kPointerCompressionShift);
    // Normal compressed pointers must have the MSB set. This is guaranteed by
    // the cage alignment.
    CPPGC_DCHECK((!compressed || compressed == kCompressedSentinel) ||
                 (compressed & (1 << 31)));
    return compressed;
  }

  static V8_INLINE void* Decompress(IntegralType ptr) {
    CPPGC_DCHECK(CageBaseGlobal::IsSet());
    const uintptr_t base = CageBaseGlobal::Get();
    return Decompress(ptr, base);
  }

  static V8_INLINE void* Decompress(IntegralType ptr, uintptr_t base) {
    CPPGC_DCHECK(CageBaseGlobal::IsSet());
    CPPGC_DCHECK(base == CageBaseGlobal::Get());
    // Sign-extend compressed pointer to full width. This ensure that normal
    // pointers have only 1s in the base part of the address. It's also
    // important to shift the unsigned value, as otherwise it would result in
    // undefined behavior.
    const uint64_t mask = static_cast<uint64_t>(static_cast<int32_t>(ptr))
                          << api_constants::kPointerCompressionShift;
    // Set the base part of the address for normal compressed pointers. Note
    // that nullptr and the sentinel value do not have 1s in the base part and
    // remain as-is in this operation.
    return reinterpret_cast<void*>(mask & base);
  }

  // For a given memory `address`, this method iterates all possible pointers
  // that can be reasonably recovered with the current compression scheme and
  // passes them to `callback`.
  template <typename Callback>
  static V8_INLINE void VisitPossiblePointers(const void* address,
                                              Callback callback);

 private:
  static constexpr IntegralType kCompressedSentinel =
      SentinelPointer::kSentinelValue >>
      api_constants::kPointerCompressionShift;
  // All constructors initialize `value_`. Do not add a default value here as it
  // results in a non-atomic write on some builds, even when the atomic version
  // of the constructor is used.
  IntegralType value_;
};

template <typename Callback>
// static
void CompressedPointer::VisitPossiblePointers(const void* address,
                                              Callback callback) {
  const uintptr_t base = CageBaseGlobal::Get();
  CPPGC_DCHECK(base);
  // We may have random compressed pointers on stack (e.g. due to inlined
  // collections). These could be present in both halfwords.
  const uint32_t compressed_low =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address));
  callback(CompressedPointer::Decompress(compressed_low, base));
  const uint32_t compressed_high = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(address) >> (sizeof(uint32_t) * CHAR_BIT));
  callback(CompressedPointer::Decompress(compressed_high, base));
  // Iterate possible intermediate values, see `Decompress()`. The intermediate
  // value of decompressing is a 64-bit value where 35 bits are the offset. We
  // don't assume sign extension is stored and recover that part.
  //
  // Note that this case conveniently also recovers the full pointer.
  static constexpr uintptr_t kBitForIntermediateValue =
      (sizeof(uint32_t) * CHAR_BIT) + api_constants::kPointerCompressionShift;
  static constexpr uintptr_t kSignExtensionMask =
      ~((uintptr_t{1} << kBitForIntermediateValue) - 1);
  const uintptr_t intermediate_sign_extended =
      reinterpret_cast<uintptr_t>(address) | kSignExtensionMask;
  callback(reinterpret_cast<void*>(intermediate_sign_extended & base));
}

#endif  // defined(CPPGC_POINTER_COMPRESSION)

class V8_TRIVIAL_ABI RawPointer final {
 public:
  struct AtomicInitializerTag {};

  using IntegralType = uintptr_t;
  static constexpr auto kWriteBarrierSlotType =
      WriteBarrierSlotType::kUncompressed;

  V8_INLINE RawPointer() : ptr_(nullptr) {}
  V8_INLINE explicit RawPointer(const void* ptr, AtomicInitializerTag) {
    StoreAtomic(ptr);
  }
  V8_INLINE explicit RawPointer(const void* ptr) : ptr_(ptr) {}

  V8_INLINE const void* Load() const { return ptr_; }
  V8_INLINE const void* LoadAtomic() const {
    return reinterpret_cast<const std::atomic<const void*>&>(ptr_).load(
        std::memory_order_relaxed);
  }

  V8_INLINE void Store(const void* ptr) { ptr_ = ptr; }
  V8_INLINE void StoreAtomic(const void* ptr) {
    reinterpret_cast<std::atomic<const void*>&>(ptr_).store(
        ptr, std::memory_order_relaxed);
  }

  V8_INLINE void Clear() { ptr_ = nullptr; }
  V8_INLINE bool IsCleared() const { return !ptr_; }

  V8_INLINE bool IsSentinel() const { return ptr_ == kSentinelPointer; }

  V8_INLINE uintptr_t GetAsInteger() const {
    return reinterpret_cast<uintptr_t>(ptr_);
  }

  V8_INLINE friend bool operator==(RawPointer a, RawPointer b) {
    return a.ptr_ == b.ptr_;
  }
  V8_INLINE friend bool operator!=(RawPointer a, RawPointer b) {
    return a.ptr_ != b.ptr_;
  }
  V8_INLINE friend bool operator<(RawPointer a, RawPointer b) {
    return a.ptr_ < b.ptr_;
  }
  V8_INLINE friend bool operator<=(RawPointer a, RawPointer b) {
    return a.ptr_ <= b.ptr_;
  }
  V8_INLINE friend bool operator>(RawPointer a, RawPointer b) {
    return a.ptr_ > b.ptr_;
  }
  V8_INLINE friend bool operator>=(RawPointer a, RawPointer b) {
    return a.ptr_ >= b.ptr_;
  }

  template <typename Callback>
  static V8_INLINE void VisitPossiblePointers(const void* address,
                                              Callback callback) {
    // Pass along the full pointer.
    return callback(const_cast<void*>(address));
  }

 private:
  // All constructors initialize `ptr_`. Do not add a default value here as it
  // results in a non-atomic write on some builds, even when the atomic version
  // of the constructor is used.
  const void* ptr_;
};

#if defined(CPPGC_POINTER_COMPRESSION)
using DefaultMemberStorage = CompressedPointer;
#else   // !defined(CPPGC_POINTER_COMPRESSION)
using DefaultMemberStorage = RawPointer;
#endif  // !defined(CPPGC_POINTER_COMPRESSION)

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_MEMBER_STORAGE_H_
