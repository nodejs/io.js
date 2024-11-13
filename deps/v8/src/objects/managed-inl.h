// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MANAGED_INL_H_
#define V8_OBJECTS_MANAGED_INL_H_

#include "src/handles/global-handles-inl.h"
#include "src/objects/managed.h"

namespace v8::internal {

namespace detail {
// Called by either isolate shutdown or the {ManagedObjectFinalizer} in order
// to actually delete the shared pointer and decrement the shared refcount.
template <typename CppType>
static void Destructor(void* ptr) {
  auto shared_ptr_ptr = reinterpret_cast<std::shared_ptr<CppType>*>(ptr);
  delete shared_ptr_ptr;
}
}  // namespace detail

// static
template <class CppType>
Handle<Managed<CppType>> Managed<CppType>::From(
    Isolate* isolate, size_t estimated_size,
    std::shared_ptr<CppType> shared_ptr, AllocationType allocation_type) {
  static constexpr ExternalPointerTag kTag = TagForManaged<CppType>::value;
  static_assert(IsManagedExternalPointerType(kTag));
  reinterpret_cast<v8::Isolate*>(isolate)
      ->AdjustAmountOfExternalAllocatedMemory(estimated_size);
  auto destructor = new ManagedPtrDestructor(
      estimated_size, new std::shared_ptr<CppType>{std::move(shared_ptr)},
      detail::Destructor<CppType>);
  Handle<Managed<CppType>> handle =
      Cast<Managed<CppType>>(isolate->factory()->NewForeign<kTag>(
          reinterpret_cast<Address>(destructor), allocation_type));
  Handle<Object> global_handle = isolate->global_handles()->Create(*handle);
  destructor->global_handle_location_ = global_handle.location();
  GlobalHandles::MakeWeak(destructor->global_handle_location_, destructor,
                          &ManagedObjectFinalizer,
                          v8::WeakCallbackType::kParameter);
  isolate->RegisterManagedPtrDestructor(destructor);
  return handle;
}

// static
template <class CppType>
Handle<TrustedManaged<CppType>> TrustedManaged<CppType>::From(
    Isolate* isolate, size_t estimated_size,
    std::shared_ptr<CppType> shared_ptr) {
  reinterpret_cast<v8::Isolate*>(isolate)
      ->AdjustAmountOfExternalAllocatedMemory(estimated_size);
  auto destructor = new ManagedPtrDestructor(
      estimated_size, new std::shared_ptr<CppType>{std::move(shared_ptr)},
      detail::Destructor<CppType>);
  Handle<TrustedManaged<CppType>> handle =
      Cast<TrustedManaged<CppType>>(isolate->factory()->NewTrustedForeign(
          reinterpret_cast<Address>(destructor)));
  Handle<Object> global_handle = isolate->global_handles()->Create(*handle);
  destructor->global_handle_location_ = global_handle.location();
  GlobalHandles::MakeWeak(destructor->global_handle_location_, destructor,
                          &ManagedObjectFinalizer,
                          v8::WeakCallbackType::kParameter);
  isolate->RegisterManagedPtrDestructor(destructor);
  return handle;
}

}  // namespace v8::internal

#endif  // V8_OBJECTS_MANAGED_INL_H_
