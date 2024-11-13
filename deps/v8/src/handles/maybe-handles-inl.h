// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_MAYBE_HANDLES_INL_H_
#define V8_HANDLES_MAYBE_HANDLES_INL_H_

#include "src/base/macros.h"
#include "src/handles/handles-inl.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/casting.h"
#include "src/objects/maybe-object-inl.h"

namespace v8 {
namespace internal {

template <typename T>
MaybeHandle<T>::MaybeHandle(Tagged<T> object, Isolate* isolate)
    : MaybeHandle(handle(object, isolate)) {}

template <typename T>
MaybeHandle<T>::MaybeHandle(Tagged<T> object, LocalHeap* local_heap)
    : MaybeHandle(handle(object, local_heap)) {}

template <typename To, typename From>
inline MaybeHandle<To> Cast(MaybeHandle<From> value,
                            const v8::SourceLocation& loc) {
  DCHECK_WITH_MSG_AND_LOC(value.is_null() || Is<To>(*value.ToHandleChecked()),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return MaybeHandle<To>(value.location_);
}

#ifdef V8_ENABLE_DIRECT_HANDLE
template <typename T>
template <typename S>
bool MaybeHandle<T>::ToHandle(DirectHandle<S>* out) const {
  if (location_ == nullptr) {
    *out = DirectHandle<T>::null();
    return false;
  } else {
    *out = DirectHandle<T>(Handle<T>(location_));
    return true;
  }
}
#endif

MaybeObjectHandle::MaybeObjectHandle(Tagged<MaybeObject> object,
                                     Isolate* isolate) {
  Tagged<HeapObject> heap_object;
  DCHECK(!object.IsCleared());
  if (object.GetHeapObjectIfWeak(&heap_object)) {
    handle_ = handle(heap_object, isolate);
    reference_type_ = HeapObjectReferenceType::WEAK;
  } else {
    handle_ = handle(Cast<Object>(object), isolate);
    reference_type_ = HeapObjectReferenceType::STRONG;
  }
}

MaybeObjectHandle::MaybeObjectHandle(Tagged<MaybeObject> object,
                                     LocalHeap* local_heap) {
  Tagged<HeapObject> heap_object;
  DCHECK(!object.IsCleared());
  if (object.GetHeapObjectIfWeak(&heap_object)) {
    handle_ = handle(heap_object, local_heap);
    reference_type_ = HeapObjectReferenceType::WEAK;
  } else {
    handle_ = handle(Cast<Object>(object), local_heap);
    reference_type_ = HeapObjectReferenceType::STRONG;
  }
}

MaybeObjectHandle::MaybeObjectHandle(Handle<Object> object)
    : reference_type_(HeapObjectReferenceType::STRONG), handle_(object) {}

MaybeObjectHandle::MaybeObjectHandle(Tagged<Object> object, Isolate* isolate)
    : reference_type_(HeapObjectReferenceType::STRONG),
      handle_(object, isolate) {}

MaybeObjectHandle::MaybeObjectHandle(Tagged<Smi> object, Isolate* isolate)
    : reference_type_(HeapObjectReferenceType::STRONG),
      handle_(object, isolate) {}

MaybeObjectHandle::MaybeObjectHandle(Tagged<Object> object,
                                     LocalHeap* local_heap)
    : reference_type_(HeapObjectReferenceType::STRONG),
      handle_(object, local_heap) {}

MaybeObjectHandle::MaybeObjectHandle(Tagged<Smi> object, LocalHeap* local_heap)
    : reference_type_(HeapObjectReferenceType::STRONG),
      handle_(object, local_heap) {}

MaybeObjectHandle::MaybeObjectHandle(Tagged<Object> object,
                                     HeapObjectReferenceType reference_type,
                                     Isolate* isolate)
    : reference_type_(reference_type), handle_(handle(object, isolate)) {}

MaybeObjectHandle::MaybeObjectHandle(Handle<Object> object,
                                     HeapObjectReferenceType reference_type)
    : reference_type_(reference_type), handle_(object) {}

MaybeObjectHandle MaybeObjectHandle::Weak(Handle<Object> object) {
  return MaybeObjectHandle(object, HeapObjectReferenceType::WEAK);
}

MaybeObjectHandle MaybeObjectHandle::Weak(Tagged<Object> object,
                                          Isolate* isolate) {
  return MaybeObjectHandle(object, HeapObjectReferenceType::WEAK, isolate);
}

bool MaybeObjectHandle::is_identical_to(const MaybeObjectHandle& other) const {
  Handle<Object> this_handle;
  Handle<Object> other_handle;
  return reference_type_ == other.reference_type_ &&
         handle_.ToHandle(&this_handle) ==
             other.handle_.ToHandle(&other_handle) &&
         this_handle.is_identical_to(other_handle);
}

Tagged<MaybeObject> MaybeObjectHandle::operator*() const {
  if (reference_type_ == HeapObjectReferenceType::WEAK) {
    return MakeWeak(*handle_.ToHandleChecked());
  } else {
    return *handle_.ToHandleChecked();
  }
}

Tagged<MaybeObject> MaybeObjectHandle::operator->() const {
  if (reference_type_ == HeapObjectReferenceType::WEAK) {
    return MakeWeak(*handle_.ToHandleChecked());
  } else {
    return *handle_.ToHandleChecked();
  }
}

Handle<Object> MaybeObjectHandle::object() const {
  return handle_.ToHandleChecked();
}

inline MaybeObjectHandle handle(Tagged<MaybeObject> object, Isolate* isolate) {
  return MaybeObjectHandle(object, isolate);
}

inline MaybeObjectHandle handle(Tagged<MaybeObject> object,
                                LocalHeap* local_heap) {
  return MaybeObjectHandle(object, local_heap);
}

template <typename T>
inline std::ostream& operator<<(std::ostream& os, MaybeHandle<T> handle) {
  if (handle.is_null()) return os << "null";
  return os << handle.ToHandleChecked();
}

#ifdef V8_ENABLE_DIRECT_HANDLE

template <typename T>
MaybeDirectHandle<T>::MaybeDirectHandle(Tagged<T> object, Isolate* isolate)
    : MaybeDirectHandle(direct_handle(object, isolate)) {}

template <typename T>
MaybeDirectHandle<T>::MaybeDirectHandle(Tagged<T> object, LocalHeap* local_heap)
    : MaybeDirectHandle(direct_handle(object, local_heap)) {}

template <typename To, typename From>
inline MaybeDirectHandle<To> Cast(MaybeDirectHandle<From> value,
                                  const v8::SourceLocation& loc) {
  DCHECK_WITH_MSG_AND_LOC(value.is_null() || Is<To>(*value.ToHandleChecked()),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return MaybeDirectHandle<To>(value.location_);
}

template <typename T>
inline std::ostream& operator<<(std::ostream& os, MaybeDirectHandle<T> handle) {
  if (handle.is_null()) return os << "null";
  return os << handle.ToHandleChecked();
}

MaybeObjectDirectHandle::MaybeObjectDirectHandle(Tagged<MaybeObject> object,
                                                 Isolate* isolate) {
  Tagged<HeapObject> heap_object;
  DCHECK(!object.IsCleared());
  if (object.GetHeapObjectIfWeak(&heap_object)) {
    handle_ = direct_handle(heap_object, isolate);
    reference_type_ = HeapObjectReferenceType::WEAK;
  } else {
    handle_ = direct_handle(Cast<Object>(object), isolate);
    reference_type_ = HeapObjectReferenceType::STRONG;
  }
}

MaybeObjectDirectHandle::MaybeObjectDirectHandle(Tagged<MaybeObject> object,
                                                 LocalHeap* local_heap) {
  Tagged<HeapObject> heap_object;
  DCHECK(!object.IsCleared());
  if (object.GetHeapObjectIfWeak(&heap_object)) {
    handle_ = direct_handle(heap_object, local_heap);
    reference_type_ = HeapObjectReferenceType::WEAK;
  } else {
    handle_ = direct_handle(Cast<Object>(object), local_heap);
    reference_type_ = HeapObjectReferenceType::STRONG;
  }
}

MaybeObjectDirectHandle::MaybeObjectDirectHandle(DirectHandle<Object> object)
    : reference_type_(HeapObjectReferenceType::STRONG), handle_(object) {}

MaybeObjectDirectHandle::MaybeObjectDirectHandle(Tagged<Object> object,
                                                 Isolate* isolate)
    : reference_type_(HeapObjectReferenceType::STRONG),
      handle_(object, isolate) {}

MaybeObjectDirectHandle::MaybeObjectDirectHandle(Tagged<Object> object,
                                                 LocalHeap* local_heap)
    : reference_type_(HeapObjectReferenceType::STRONG),
      handle_(object, local_heap) {}

MaybeObjectDirectHandle::MaybeObjectDirectHandle(
    Tagged<Object> object, HeapObjectReferenceType reference_type,
    Isolate* isolate)
    : reference_type_(reference_type), handle_(object, isolate) {}

MaybeObjectDirectHandle::MaybeObjectDirectHandle(
    DirectHandle<Object> object, HeapObjectReferenceType reference_type)
    : reference_type_(reference_type), handle_(object) {}

MaybeObjectDirectHandle MaybeObjectDirectHandle::Weak(
    DirectHandle<Object> object) {
  return MaybeObjectDirectHandle(object, HeapObjectReferenceType::WEAK);
}

MaybeObjectDirectHandle MaybeObjectDirectHandle::Weak(Tagged<Object> object,
                                                      Isolate* isolate) {
  return MaybeObjectDirectHandle(object, HeapObjectReferenceType::WEAK,
                                 isolate);
}

bool MaybeObjectDirectHandle::is_identical_to(
    const MaybeObjectDirectHandle& other) const {
  DirectHandle<Object> this_handle;
  DirectHandle<Object> other_handle;
  return reference_type_ == other.reference_type_ &&
         handle_.ToHandle(&this_handle) ==
             other.handle_.ToHandle(&other_handle) &&
         this_handle.is_identical_to(other_handle);
}

Tagged<MaybeObject> MaybeObjectDirectHandle::operator*() const {
  if (reference_type_ == HeapObjectReferenceType::WEAK) {
    return MakeWeak(*handle_.ToHandleChecked());
  } else {
    return *handle_.ToHandleChecked();
  }
}

Tagged<MaybeObject> MaybeObjectDirectHandle::operator->() const {
  if (reference_type_ == HeapObjectReferenceType::WEAK) {
    return MakeWeak(*handle_.ToHandleChecked());
  } else {
    return *handle_.ToHandleChecked();
  }
}

DirectHandle<Object> MaybeObjectDirectHandle::object() const {
  return handle_.ToHandleChecked();
}

#endif  // V8_ENABLE_DIRECT_HANDLE

template <typename T>
V8_INLINE MaybeHandle<T> indirect_handle(MaybeDirectHandle<T> maybe_handle,
                                         Isolate* isolate) {
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (DirectHandle<T> handle; maybe_handle.ToHandle(&handle))
    return indirect_handle(handle, isolate);
  return {};
#else
  return maybe_handle;
#endif
}

template <typename T>
V8_INLINE MaybeHandle<T> indirect_handle(MaybeDirectHandle<T> maybe_handle,
                                         LocalIsolate* isolate) {
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (DirectHandle<T> handle; maybe_handle.ToHandle(&handle))
    return indirect_handle(handle, isolate);
  return {};
#else
  return maybe_handle;
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_MAYBE_HANDLES_INL_H_
