// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_IMPL_INL_H_
#define V8_OBJECTS_TAGGED_IMPL_INL_H_

#include "src/objects/tagged-impl.h"

#ifdef V8_COMPRESS_POINTERS
#include "src/execution/isolate.h"
#endif
#include "src/common/ptr-compr-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/smi.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::ToSmi(Tagged<Smi>* value) const {
  if (HAS_SMI_TAG(ptr_)) {
    *value = ToSmi();
    return true;
  }
  return false;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<Smi> TaggedImpl<kRefType, StorageType>::ToSmi() const {
  V8_ASSUME(HAS_SMI_TAG(ptr_));
  if constexpr (kIsFull) {
    return Tagged<Smi>(ptr_);
  }
  // Implementation for compressed pointers.
  return Tagged<Smi>(
      CompressionScheme::DecompressTaggedSigned(static_cast<Tagged_t>(ptr_)));
}

//
// TaggedImpl::GetHeapObject(Tagged<HeapObject>* result) implementation.
//

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObject(
    Tagged<HeapObject>* result) const {
  CHECK(kIsFull);
  if (!IsStrongOrWeak()) return false;
  *result = GetHeapObject();
  return true;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObject(
    Isolate* isolate, Tagged<HeapObject>* result) const {
  if (kIsFull) return GetHeapObject(result);
  // Implementation for compressed pointers.
  if (!IsStrongOrWeak()) return false;
  *result = GetHeapObject(isolate);
  return true;
}

//
// TaggedImpl::GetHeapObject(Tagged<HeapObject>* result,
//                           HeapObjectReferenceType* reference_type)
// implementation.
//

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObject(
    Tagged<HeapObject>* result, HeapObjectReferenceType* reference_type) const {
  CHECK(kIsFull);
  if (!IsStrongOrWeak()) return false;
  *reference_type = IsWeakOrCleared() ? HeapObjectReferenceType::WEAK
                                      : HeapObjectReferenceType::STRONG;
  *result = GetHeapObject();
  return true;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObject(
    Isolate* isolate, Tagged<HeapObject>* result,
    HeapObjectReferenceType* reference_type) const {
  if (kIsFull) return GetHeapObject(result, reference_type);
  // Implementation for compressed pointers.
  if (!IsStrongOrWeak()) return false;
  *reference_type = IsWeakOrCleared() ? HeapObjectReferenceType::WEAK
                                      : HeapObjectReferenceType::STRONG;
  *result = GetHeapObject(isolate);
  return true;
}

//
// TaggedImpl::GetHeapObjectIfStrong(Tagged<HeapObject>* result) implementation.
//

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObjectIfStrong(
    Tagged<HeapObject>* result) const {
  CHECK(kIsFull);
  if (IsStrong()) {
    *result = HeapObject::cast(Tagged<Object>(ptr_));
    return true;
  }
  return false;
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObjectIfStrong(
    Isolate* isolate, Tagged<HeapObject>* result) const {
  if (kIsFull) return GetHeapObjectIfStrong(result);
  // Implementation for compressed pointers.
  if (IsStrong()) {
    *result =
        HeapObject::cast(Tagged<Object>(CompressionScheme::DecompressTagged(
            isolate, static_cast<Tagged_t>(ptr_))));
    return true;
  }
  return false;
}

//
// TaggedImpl::GetHeapObjectAssumeStrong() implementation.
//

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<HeapObject>
TaggedImpl<kRefType, StorageType>::GetHeapObjectAssumeStrong() const {
  CHECK(kIsFull);
  DCHECK(IsStrong());
  return HeapObject::cast(Tagged<Object>(ptr_));
}

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<HeapObject> TaggedImpl<kRefType, StorageType>::GetHeapObjectAssumeStrong(
    Isolate* isolate) const {
  if (kIsFull) return GetHeapObjectAssumeStrong();
  // Implementation for compressed pointers.
  DCHECK(IsStrong());
  return HeapObject::cast(Tagged<Object>(CompressionScheme::DecompressTagged(
      isolate, static_cast<Tagged_t>(ptr_))));
}

//
// TaggedImpl::GetHeapObjectIfWeak(Tagged<HeapObject>* result) implementation
//

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObjectIfWeak(
    Tagged<HeapObject>* result) const {
  CHECK(kIsFull);
  if (kCanBeWeak) {
    if (IsWeak()) {
      *result = GetHeapObject();
      return true;
    }
    return false;
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return false;
  }
}

template <HeapObjectReferenceType kRefType, typename StorageType>
bool TaggedImpl<kRefType, StorageType>::GetHeapObjectIfWeak(
    Isolate* isolate, Tagged<HeapObject>* result) const {
  if (kIsFull) return GetHeapObjectIfWeak(result);
  // Implementation for compressed pointers.
  if (kCanBeWeak) {
    if (IsWeak()) {
      *result = GetHeapObject(isolate);
      return true;
    }
    return false;
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return false;
  }
}

//
// TaggedImpl::GetHeapObjectAssumeWeak() implementation.
//

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<HeapObject> TaggedImpl<kRefType, StorageType>::GetHeapObjectAssumeWeak()
    const {
  CHECK(kIsFull);
  DCHECK(IsWeak());
  return GetHeapObject();
}

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<HeapObject> TaggedImpl<kRefType, StorageType>::GetHeapObjectAssumeWeak(
    Isolate* isolate) const {
  if (kIsFull) return GetHeapObjectAssumeWeak();
  // Implementation for compressed pointers.
  DCHECK(IsWeak());
  return GetHeapObject(isolate);
}

//
// TaggedImpl::GetHeapObject() implementation.
//

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<HeapObject> TaggedImpl<kRefType, StorageType>::GetHeapObject() const {
  CHECK(kIsFull);
  DCHECK(!IsSmi());
  if (kCanBeWeak) {
    DCHECK(!IsCleared());
    return HeapObject::cast(Tagged<Object>(ptr_ & ~kWeakHeapObjectMask));
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return HeapObject::cast(Tagged<Object>(ptr_));
  }
}

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<HeapObject> TaggedImpl<kRefType, StorageType>::GetHeapObject(
    Isolate* isolate) const {
  if (kIsFull) return GetHeapObject();
  // Implementation for compressed pointers.
  DCHECK(!IsSmi());
  if (kCanBeWeak) {
    DCHECK(!IsCleared());
    return HeapObject::cast(Tagged<Object>(CompressionScheme::DecompressTagged(
        isolate, static_cast<Tagged_t>(ptr_) & ~kWeakHeapObjectMask)));
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return HeapObject::cast(Tagged<Object>(CompressionScheme::DecompressTagged(
        isolate, static_cast<Tagged_t>(ptr_))));
  }
}

//
// TaggedImpl::GetHeapObjectOrSmi() implementation.
//

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<Object> TaggedImpl<kRefType, StorageType>::GetHeapObjectOrSmi() const {
  CHECK(kIsFull);
  if (IsSmi()) {
    return Tagged<Object>(ptr_);
  }
  return GetHeapObject();
}

template <HeapObjectReferenceType kRefType, typename StorageType>
Tagged<Object> TaggedImpl<kRefType, StorageType>::GetHeapObjectOrSmi(
    Isolate* isolate) const {
  if constexpr (kIsFull) return GetHeapObjectOrSmi();
  // Implementation for compressed pointers.
  if (IsSmi()) return ToSmi();
  return GetHeapObject(isolate);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_TAGGED_IMPL_INL_H_
