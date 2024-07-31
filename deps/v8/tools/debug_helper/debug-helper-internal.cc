// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug-helper-internal.h"
#include "src/common/ptr-compr-inl.h"
#include "torque-generated/class-debug-readers.h"

namespace i = v8::internal;

namespace v8 {
namespace internal {
namespace debug_helper_internal {

bool IsPointerCompressed(uintptr_t address) {
#if COMPRESS_POINTERS_BOOL
  return address < i::kPtrComprCageReservationSize;
#else
  return false;
#endif
}

uintptr_t EnsureDecompressed(uintptr_t address,
                             uintptr_t any_uncompressed_ptr) {
  if (!COMPRESS_POINTERS_BOOL || !IsPointerCompressed(address)) return address;
#ifdef V8_COMPRESS_POINTERS
  Address base =
      V8HeapCompressionScheme::GetPtrComprCageBaseAddress(any_uncompressed_ptr);
  if (base != V8HeapCompressionScheme::base()) {
    V8HeapCompressionScheme::InitBase(base);
  }
#endif  // V8_COMPRESS_POINTERS
  // TODO(v8:11880): ExternalCodeCompressionScheme might be needed here for
  // decompressing Code pointers from external code space.
  return i::V8HeapCompressionScheme::DecompressTagged(
      any_uncompressed_ptr, static_cast<i::Tagged_t>(address));
}

d::PropertyKind GetArrayKind(d::MemoryAccessResult mem_result) {
  d::PropertyKind indexed_field_kind{};
  switch (mem_result) {
    case d::MemoryAccessResult::kOk:
      indexed_field_kind = d::PropertyKind::kArrayOfKnownSize;
      break;
    case d::MemoryAccessResult::kAddressNotValid:
      indexed_field_kind =
          d::PropertyKind::kArrayOfUnknownSizeDueToInvalidMemory;
      break;
    default:
      indexed_field_kind =
          d::PropertyKind::kArrayOfUnknownSizeDueToValidButInaccessibleMemory;
      break;
  }
  return indexed_field_kind;
}

std::vector<std::unique_ptr<ObjectProperty>> TqObject::GetProperties(
    d::MemoryAccessor accessor) const {
  return std::vector<std::unique_ptr<ObjectProperty>>();
}

const char* TqObject::GetName() const { return "v8::internal::Object"; }

void TqObject::Visit(TqObjectVisitor* visitor) const {
  visitor->VisitObject(this);
}

bool TqObject::IsSuperclassOf(const TqObject* other) const {
  return GetName() != other->GetName();
}

}  // namespace debug_helper_internal
}  // namespace internal
}  // namespace v8
