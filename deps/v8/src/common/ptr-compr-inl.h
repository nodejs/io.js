// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_PTR_COMPR_INL_H_
#define V8_COMMON_PTR_COMPR_INL_H_

#include "include/v8-internal.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate-inl.h"

namespace v8 {
namespace internal {

#ifdef V8_COMPRESS_POINTERS

PtrComprCageBase::PtrComprCageBase(const Isolate* isolate)
    : address_(isolate->cage_base()) {}
PtrComprCageBase::PtrComprCageBase(const LocalIsolate* isolate)
    : address_(isolate->cage_base()) {}

//
// V8HeapCompressionSchemeImpl
//

constexpr Address kPtrComprCageBaseMask = ~(kPtrComprCageBaseAlignment - 1);

// static
template <typename Cage>
Address V8HeapCompressionSchemeImpl<Cage>::GetPtrComprCageBaseAddress(
    Address on_heap_addr) {
  return RoundDown<kPtrComprCageBaseAlignment>(on_heap_addr);
}

// static
template <typename Cage>
Address V8HeapCompressionSchemeImpl<Cage>::GetPtrComprCageBaseAddress(
    PtrComprCageBase cage_base) {
  Address base = cage_base.address();
  V8_ASSUME((base & kPtrComprCageBaseMask) == base);
  base = reinterpret_cast<Address>(V8_ASSUME_ALIGNED(
      reinterpret_cast<void*>(base), kPtrComprCageBaseAlignment));
  return base;
}

// static
template <typename Cage>
void V8HeapCompressionSchemeImpl<Cage>::InitBase(Address base) {
  CHECK_EQ(base, GetPtrComprCageBaseAddress(base));
#if defined(USING_V8_SHARED_PRIVATE) && \
    defined(V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES)
  Cage::set_base_non_inlined(base);
#else
  Cage::base_ = base;
#endif
}

// static
template <typename Cage>
Address V8HeapCompressionSchemeImpl<Cage>::base() {
#if defined(USING_V8_SHARED_PRIVATE) && \
    defined(V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES)
  Address base = Cage::base_non_inlined();
#else
  Address base = Cage::base_;
#endif
  // V8_ASSUME_ALIGNED is often not preserved across ptr-to-int casts (i.e. when
  // casting to an Address). To increase our chances we additionally encode the
  // same information in this V8_ASSUME.
  V8_ASSUME((base & kPtrComprCageBaseMask) == base);
  return reinterpret_cast<Address>(V8_ASSUME_ALIGNED(
      reinterpret_cast<void*>(base), kPtrComprCageBaseAlignment));
}

// static
template <typename Cage>
Tagged_t V8HeapCompressionSchemeImpl<Cage>::CompressObject(Address tagged) {
  // This is used to help clang produce better code. Values which could be
  // invalid pointers need to be compressed with CompressAny.
#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
  V8_ASSUME((tagged & kPtrComprCageBaseMask) == base() || HAS_SMI_TAG(tagged));
#endif
  return static_cast<Tagged_t>(tagged);
}

// static
template <typename Cage>
Tagged_t V8HeapCompressionSchemeImpl<Cage>::CompressAny(Address tagged) {
  return static_cast<Tagged_t>(tagged);
}

// static
template <typename Cage>
Address V8HeapCompressionSchemeImpl<Cage>::DecompressTaggedSigned(
    Tagged_t raw_value) {
  // For runtime code the upper 32-bits of the Smi value do not matter.
  return static_cast<Address>(raw_value);
}

// static
template <typename Cage>
template <typename TOnHeapAddress>
Address V8HeapCompressionSchemeImpl<Cage>::DecompressTagged(
    TOnHeapAddress on_heap_addr, Tagged_t raw_value) {
#ifdef V8_COMPRESS_POINTERS
  Address cage_base = base();
#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  DCHECK_WITH_MSG(cage_base != kNullAddress,
                  "V8HeapCompressionSchemeImpl::base is not initialized for "
                  "current thread");
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
#else
  Address cage_base = GetPtrComprCageBaseAddress(on_heap_addr);
#endif  // V8_COMPRESS_POINTERS
  Address result = cage_base + static_cast<Address>(raw_value);
  V8_ASSUME(static_cast<uint32_t>(result) == raw_value);
  return result;
}

// static
template <typename Cage>
template <typename ProcessPointerCallback>
void V8HeapCompressionSchemeImpl<Cage>::ProcessIntermediatePointers(
    PtrComprCageBase cage_base, Address raw_value,
    ProcessPointerCallback callback) {
  // If pointer compression is enabled, we may have random compressed pointers
  // on the stack that may be used for subsequent operations.
  // Extract, decompress and trace both halfwords.
  Address decompressed_low =
      V8HeapCompressionSchemeImpl<Cage>::DecompressTagged(
          cage_base, static_cast<Tagged_t>(raw_value));
  callback(decompressed_low);
  Address decompressed_high =
      V8HeapCompressionSchemeImpl<Cage>::DecompressTagged(
          cage_base,
          static_cast<Tagged_t>(raw_value >> (sizeof(Tagged_t) * CHAR_BIT)));
  callback(decompressed_high);
}

#ifdef V8_EXTERNAL_CODE_SPACE

//
// ExternalCodeCompressionScheme
//

// static
Address ExternalCodeCompressionScheme::PrepareCageBaseAddress(
    Address on_heap_addr) {
  return RoundDown<kPtrComprCageBaseAlignment>(on_heap_addr);
}

// static
Address ExternalCodeCompressionScheme::GetPtrComprCageBaseAddress(
    PtrComprCageBase cage_base) {
  Address base = cage_base.address();
  V8_ASSUME((base & kPtrComprCageBaseMask) == base);
  base = reinterpret_cast<Address>(V8_ASSUME_ALIGNED(
      reinterpret_cast<void*>(base), kPtrComprCageBaseAlignment));
  return base;
}

// static
void ExternalCodeCompressionScheme::InitBase(Address base) {
  CHECK_EQ(base, PrepareCageBaseAddress(base));
#if defined(USING_V8_SHARED_PRIVATE) && \
    defined(V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES)
  set_base_non_inlined(base);
#else
  base_ = base;
#endif
}

// static
V8_CONST Address ExternalCodeCompressionScheme::base() {
#if defined(USING_V8_SHARED_PRIVATE) && \
    defined(V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES)
  Address base = base_non_inlined();
#else
  Address base = base_;
#endif
  // V8_ASSUME_ALIGNED is often not preserved across ptr-to-int casts (i.e. when
  // casting to an Address). To increase our chances we additionally encode the
  // same information in this V8_ASSUME.
  V8_ASSUME((base & kPtrComprCageBaseMask) == base);
  return reinterpret_cast<Address>(V8_ASSUME_ALIGNED(
      reinterpret_cast<void*>(base), kPtrComprCageBaseAlignment));
}

// static
Tagged_t ExternalCodeCompressionScheme::CompressObject(Address tagged) {
  // This is used to help clang produce better code. Values which could be
  // invalid pointers need to be compressed with CompressAny.
  // The DCHECK generated by this V8_ASSUME is also very helpful during
  // development when moving objects between pointer compression cages as it
  // quickly identifies any places where we still store a compressed pointer
  // slot with the wrong base.
#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
  V8_ASSUME((tagged & kPtrComprCageBaseMask) == base() || HAS_SMI_TAG(tagged));
#endif
  return static_cast<Tagged_t>(tagged);
}

// static
Tagged_t ExternalCodeCompressionScheme::CompressAny(Address tagged) {
  return static_cast<Tagged_t>(tagged);
}

// static
Address ExternalCodeCompressionScheme::DecompressTaggedSigned(
    Tagged_t raw_value) {
  // For runtime code the upper 32-bits of the Smi value do not matter.
  return static_cast<Address>(raw_value);
}

// static
template <typename TOnHeapAddress>
Address ExternalCodeCompressionScheme::DecompressTagged(
    TOnHeapAddress on_heap_addr, Tagged_t raw_value) {
#ifdef V8_COMPRESS_POINTERS
  Address cage_base = base();
#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  DCHECK_WITH_MSG(cage_base != kNullAddress,
                  "ExternalCodeCompressionScheme::base is not initialized for "
                  "current thread");
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
#else
  Address cage_base = GetPtrComprCageBaseAddress(on_heap_addr);
#endif  // V8_COMPRESS_POINTERS
  Address result = cage_base + static_cast<Address>(raw_value);
  V8_ASSUME(static_cast<uint32_t>(result) == raw_value);
  return result;
}

// static
template <typename ProcessPointerCallback>
void ExternalCodeCompressionScheme::ProcessIntermediatePointers(
    PtrComprCageBase cage_base, Address raw_value,
    ProcessPointerCallback callback) {
  // If pointer compression is enabled, we may have random compressed pointers
  // on the stack that may be used for subsequent operations.
  // Extract, decompress and trace both halfwords.
  Address decompressed_low = ExternalCodeCompressionScheme::DecompressTagged(
      cage_base, static_cast<Tagged_t>(raw_value));
  callback(decompressed_low);
  Address decompressed_high = ExternalCodeCompressionScheme::DecompressTagged(
      cage_base,
      static_cast<Tagged_t>(raw_value >> (sizeof(Tagged_t) * CHAR_BIT)));
  callback(decompressed_high);
}

#endif  // V8_EXTERNAL_CODE_SPACE

//
// Misc functions.
//

V8_INLINE PtrComprCageBase
GetPtrComprCageBaseFromOnHeapAddress(Address address) {
  return PtrComprCageBase(
      V8HeapCompressionScheme::GetPtrComprCageBaseAddress(address));
}

// Load the main pointer compression cage base.
V8_INLINE PtrComprCageBase GetPtrComprCageBase() {
  return PtrComprCageBase(V8HeapCompressionScheme::base());
}

#else

//
// V8HeapCompressionSchemeImpl
//

// static
template <typename Cage>
Address V8HeapCompressionSchemeImpl<Cage>::GetPtrComprCageBaseAddress(
    Address on_heap_addr) {
  UNREACHABLE();
}

// static
template <typename Cage>
Tagged_t V8HeapCompressionSchemeImpl<Cage>::CompressObject(Address tagged) {
  UNREACHABLE();
}

// static
template <typename Cage>
Tagged_t V8HeapCompressionSchemeImpl<Cage>::CompressAny(Address tagged) {
  UNREACHABLE();
}

// static
template <typename Cage>
Address V8HeapCompressionSchemeImpl<Cage>::DecompressTaggedSigned(
    Tagged_t raw_value) {
  UNREACHABLE();
}

// static
template <typename Cage>
template <typename TOnHeapAddress>
Address V8HeapCompressionSchemeImpl<Cage>::DecompressTagged(
    TOnHeapAddress on_heap_addr, Tagged_t raw_value) {
  UNREACHABLE();
}

// static
template <typename Cage>
template <typename ProcessPointerCallback>
void V8HeapCompressionSchemeImpl<Cage>::ProcessIntermediatePointers(
    PtrComprCageBase cage_base, Address raw_value,
    ProcessPointerCallback callback) {
  UNREACHABLE();
}

//
// Misc functions.
//

V8_INLINE constexpr PtrComprCageBase GetPtrComprCageBaseFromOnHeapAddress(
    Address address) {
  return PtrComprCageBase();
}

V8_INLINE PtrComprCageBase GetPtrComprCageBase() { return PtrComprCageBase(); }

#endif  // V8_COMPRESS_POINTERS

V8_INLINE PtrComprCageBase GetPtrComprCageBase(Tagged<HeapObject> object) {
  return GetPtrComprCageBaseFromOnHeapAddress(object.ptr());
}

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

PtrComprCageAccessScope::PtrComprCageAccessScope(Isolate* isolate)
    : cage_base_(V8HeapCompressionScheme::base()),
#ifdef V8_EXTERNAL_CODE_SPACE
      code_cage_base_(ExternalCodeCompressionScheme::base()),
#endif  // V8_EXTERNAL_CODE_SPACE
      saved_current_isolate_group_(IsolateGroup::current()) {
  V8HeapCompressionScheme::InitBase(isolate->cage_base());
#ifdef V8_EXTERNAL_CODE_SPACE
  ExternalCodeCompressionScheme::InitBase(isolate->code_cage_base());
#endif  // V8_EXTERNAL_CODE_SPACE
  IsolateGroup::set_current(isolate->isolate_group());
}

PtrComprCageAccessScope::~PtrComprCageAccessScope() {
  V8HeapCompressionScheme::InitBase(cage_base_);
#ifdef V8_EXTERNAL_CODE_SPACE
  ExternalCodeCompressionScheme::InitBase(code_cage_base_);
#endif  // V8_EXTERNAL_CODE_SPACE
  IsolateGroup::set_current(saved_current_isolate_group_);
}

#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_PTR_COMPR_INL_H_
