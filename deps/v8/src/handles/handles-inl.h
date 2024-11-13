// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_HANDLES_INL_H_
#define V8_HANDLES_HANDLES_INL_H_

#include "src/base/sanitizer/msan.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/handles/handles.h"
#include "src/handles/local-handles-inl.h"
#include "src/objects/casting.h"
#include "src/objects/objects.h"

#ifdef DEBUG
#include "src/utils/ostreams.h"
#endif

namespace v8 {
namespace internal {

class LocalHeap;

HandleBase::HandleBase(Address object, Isolate* isolate)
    : location_(HandleScope::CreateHandle(isolate, object)) {}

HandleBase::HandleBase(Address object, LocalIsolate* isolate)
    : location_(LocalHandleScope::GetHandle(isolate->heap(), object)) {}

HandleBase::HandleBase(Address object, LocalHeap* local_heap)
    : location_(LocalHandleScope::GetHandle(local_heap, object)) {}

bool HandleBase::is_identical_to(const HandleBase& that) const {
  SLOW_DCHECK((this->location_ == nullptr || this->IsDereferenceAllowed()) &&
              (that.location_ == nullptr || that.IsDereferenceAllowed()));
  if (this->location_ == that.location_) return true;
  if (this->location_ == nullptr || that.location_ == nullptr) return false;
  return Tagged<Object>(*this->location_) == Tagged<Object>(*that.location_);
}

// Allocate a new handle for the object, do not canonicalize.
template <typename T>
Handle<T> Handle<T>::New(Tagged<T> object, Isolate* isolate) {
  return Handle(HandleScope::CreateHandle(isolate, object.ptr()));
}

template <typename To, typename From>
inline Handle<To> Cast(Handle<From> value, const v8::SourceLocation& loc) {
  DCHECK_WITH_MSG_AND_LOC(value.is_null() || Is<To>(*value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return Handle<To>(value.location());
}

template <typename T>
Handle<T>::Handle(Tagged<T> object, Isolate* isolate)
    : HandleBase(object.ptr(), isolate) {}

template <typename T>
Handle<T>::Handle(Tagged<T> object, LocalIsolate* isolate)
    : HandleBase(object.ptr(), isolate) {}

template <typename T>
Handle<T>::Handle(Tagged<T> object, LocalHeap* local_heap)
    : HandleBase(object.ptr(), local_heap) {}

template <typename T>
V8_INLINE Handle<T> handle(Tagged<T> object, Isolate* isolate) {
  return Handle<T>(object, isolate);
}

template <typename T>
V8_INLINE Handle<T> handle(Tagged<T> object, LocalIsolate* isolate) {
  return Handle<T>(object, isolate);
}

template <typename T>
V8_INLINE Handle<T> handle(Tagged<T> object, LocalHeap* local_heap) {
  return Handle<T>(object, local_heap);
}

template <typename T>
V8_INLINE Handle<T> handle(T object, Isolate* isolate) {
  static_assert(kTaggedCanConvertToRawObjects);
  return handle(Tagged<T>(object), isolate);
}

template <typename T>
V8_INLINE Handle<T> handle(T object, LocalIsolate* isolate) {
  static_assert(kTaggedCanConvertToRawObjects);
  return handle(Tagged<T>(object), isolate);
}

template <typename T>
V8_INLINE Handle<T> handle(T object, LocalHeap* local_heap) {
  static_assert(kTaggedCanConvertToRawObjects);
  return handle(Tagged<T>(object), local_heap);
}

template <typename T>
inline std::ostream& operator<<(std::ostream& os, Handle<T> handle) {
  return os << Brief(*handle);
}

#ifdef V8_ENABLE_DIRECT_HANDLE

template <typename T>
V8_INLINE DirectHandle<T>::DirectHandle(Tagged<T> object)
    : DirectHandle(object.ptr()) {}

template <typename To, typename From>
inline DirectHandle<To> Cast(DirectHandle<From> value,
                             const v8::SourceLocation& loc) {
  DCHECK_WITH_MSG_AND_LOC(value.is_null() || Is<To>(*value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return DirectHandle<To>(value.obj_);
}

template <typename T>
inline std::ostream& operator<<(std::ostream& os, DirectHandle<T> handle) {
  return os << Brief(*handle);
}

#endif  // V8_ENABLE_DIRECT_HANDLE

template <typename T>
V8_INLINE DirectHandle<T> direct_handle(Tagged<T> object, Isolate* isolate) {
  return DirectHandle<T>(object, isolate);
}

template <typename T>
V8_INLINE DirectHandle<T> direct_handle(Tagged<T> object,
                                        LocalIsolate* isolate) {
  return DirectHandle<T>(object, isolate);
}

template <typename T>
V8_INLINE DirectHandle<T> direct_handle(Tagged<T> object,
                                        LocalHeap* local_heap) {
  return DirectHandle<T>(object, local_heap);
}

template <typename T>
V8_INLINE DirectHandle<T> direct_handle(T object, Isolate* isolate) {
  static_assert(kTaggedCanConvertToRawObjects);
  return direct_handle(Tagged<T>(object), isolate);
}

template <typename T>
V8_INLINE DirectHandle<T> direct_handle(T object, LocalIsolate* isolate) {
  static_assert(kTaggedCanConvertToRawObjects);
  return direct_handle(Tagged<T>(object), isolate);
}

template <typename T>
V8_INLINE DirectHandle<T> direct_handle(T object, LocalHeap* local_heap) {
  static_assert(kTaggedCanConvertToRawObjects);
  return direct_handle(Tagged<T>(object), local_heap);
}

HandleScope::HandleScope(Isolate* isolate) {
  HandleScopeData* data = isolate->handle_scope_data();
  isolate_ = isolate;
  prev_next_ = data->next;
  prev_limit_ = data->limit;
  data->level++;
#ifdef V8_ENABLE_CHECKS
  scope_level_ = data->level;
#endif
}

HandleScope::HandleScope(HandleScope&& other) V8_NOEXCEPT
    : isolate_(other.isolate_),
      prev_next_(other.prev_next_),
      prev_limit_(other.prev_limit_) {
  other.isolate_ = nullptr;
#ifdef V8_ENABLE_CHECKS
  scope_level_ = other.scope_level_;
#endif
}

HandleScope::~HandleScope() {
  if (V8_UNLIKELY(isolate_ == nullptr)) return;
#ifdef V8_ENABLE_CHECKS
  CHECK_EQ(scope_level_, isolate_->handle_scope_data()->level);
#endif
  CloseScope(isolate_, prev_next_, prev_limit_);
}

HandleScope& HandleScope::operator=(HandleScope&& other) V8_NOEXCEPT {
  if (isolate_ == nullptr) {
    isolate_ = other.isolate_;
  } else {
    DCHECK_EQ(isolate_, other.isolate_);
#ifdef V8_ENABLE_CHECKS
    CHECK_EQ(scope_level_, isolate_->handle_scope_data()->level);
#endif
    CloseScope(isolate_, prev_next_, prev_limit_);
  }
  prev_next_ = other.prev_next_;
  prev_limit_ = other.prev_limit_;
  other.isolate_ = nullptr;
#ifdef V8_ENABLE_CHECKS
  scope_level_ = other.scope_level_;
#endif
  return *this;
}

void HandleScope::CloseScope(Isolate* isolate, Address* prev_next,
                             Address* prev_limit) {
#ifdef DEBUG
  int before = v8_flags.check_handle_count ? NumberOfHandles(isolate) : 0;
#endif
  DCHECK_NOT_NULL(isolate);
  HandleScopeData* current = isolate->handle_scope_data();

  std::swap(current->next, prev_next);
  current->level--;
  Address* limit = prev_next;
  if (V8_UNLIKELY(current->limit != prev_limit)) {
    current->limit = prev_limit;
    limit = prev_limit;
    DeleteExtensions(isolate);
  }
#ifdef ENABLE_HANDLE_ZAPPING
  ZapRange(current->next, limit);
#endif
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(
      current->next,
      static_cast<size_t>(reinterpret_cast<Address>(limit) -
                          reinterpret_cast<Address>(current->next)));
#ifdef DEBUG
  int after = v8_flags.check_handle_count ? NumberOfHandles(isolate) : 0;
  DCHECK_LT(after - before, kCheckHandleThreshold);
  DCHECK_LT(before, kCheckHandleThreshold);
#endif
}

template <typename T>
Handle<T> HandleScope::CloseAndEscape(Handle<T> handle_value) {
  HandleScopeData* current = isolate_->handle_scope_data();
  Tagged<T> value = *handle_value;
#ifdef V8_ENABLE_CHECKS
  CHECK_EQ(scope_level_, isolate_->handle_scope_data()->level);
#endif
  // Throw away all handles in the current scope.
  CloseScope(isolate_, prev_next_, prev_limit_);
  // Allocate one handle in the parent scope.
  DCHECK(current->level > current->sealed_level);
  Handle<T> result(value, isolate_);
  // Reinitialize the current scope (so that it's ready
  // to be used or closed again).
  prev_next_ = current->next;
  prev_limit_ = current->limit;
  current->level++;
  return result;
}

Address* HandleScope::CreateHandle(Isolate* isolate, Address value) {
  DCHECK(AllowHandleAllocation::IsAllowed());
  DCHECK(isolate->main_thread_local_heap()->IsRunning());
  DCHECK_WITH_MSG(isolate->thread_id() == ThreadId::Current(),
                  "main-thread handle can only be created on the main thread.");
  HandleScopeData* data = isolate->handle_scope_data();
  Address* result = data->next;
  if (V8_UNLIKELY(result == data->limit)) {
    result = Extend(isolate);
  }
  // Update the current next field, set the value in the created handle,
  // and return the result.
  DCHECK_LT(reinterpret_cast<Address>(result),
            reinterpret_cast<Address>(data->limit));
  data->next = reinterpret_cast<Address*>(reinterpret_cast<Address>(result) +
                                          sizeof(Address));
  *result = value;
  return result;
}

#ifdef DEBUG
inline SealHandleScope::SealHandleScope(Isolate* isolate) : isolate_(isolate) {
  // Make sure the current thread is allowed to create handles to begin with.
  DCHECK(AllowHandleAllocation::IsAllowed());
  HandleScopeData* current = isolate_->handle_scope_data();
  // Shrink the current handle scope to make it impossible to do
  // handle allocations without an explicit handle scope.
  prev_limit_ = current->limit;
  current->limit = current->next;
  prev_sealed_level_ = current->sealed_level;
  current->sealed_level = current->level;
}

inline SealHandleScope::~SealHandleScope() {
  // Restore state in current handle scope to re-enable handle
  // allocations.
  HandleScopeData* current = isolate_->handle_scope_data();
  DCHECK_EQ(current->next, current->limit);
  current->limit = prev_limit_;
  DCHECK_EQ(current->level, current->sealed_level);
  current->sealed_level = prev_sealed_level_;
}
#endif  // DEBUG

#ifdef V8_ENABLE_DIRECT_HANDLE
bool HandleBase::is_identical_to(const DirectHandleBase& that) const {
  SLOW_DCHECK(
      (this->location_ == nullptr || this->IsDereferenceAllowed()) &&
      (that.address() == kTaggedNullAddress || that.IsDereferenceAllowed()));
  if (this->location_ == nullptr && that.address() == kTaggedNullAddress)
    return true;
  if (this->location_ == nullptr || that.address() == kTaggedNullAddress)
    return false;
  return Tagged<Object>(*this->location_) == Tagged<Object>(that.address());
}

bool DirectHandleBase::is_identical_to(const HandleBase& that) const {
  SLOW_DCHECK(
      (this->address() == kTaggedNullAddress || this->IsDereferenceAllowed()) &&
      (that.location_ == nullptr || that.IsDereferenceAllowed()));
  if (this->address() == kTaggedNullAddress && that.location_ == nullptr)
    return true;
  if (this->address() == kTaggedNullAddress || that.location_ == nullptr)
    return false;
  return Tagged<Object>(this->address()) == Tagged<Object>(*that.location_);
}

bool DirectHandleBase::is_identical_to(const DirectHandleBase& that) const {
  SLOW_DCHECK(
      (this->address() == kTaggedNullAddress || this->IsDereferenceAllowed()) &&
      (that.address() == kTaggedNullAddress || that.IsDereferenceAllowed()));
  if (this->address() == kTaggedNullAddress &&
      that.address() == kTaggedNullAddress)
    return true;
  if (this->address() == kTaggedNullAddress ||
      that.address() == kTaggedNullAddress)
    return false;
  return Tagged<Object>(this->address()) == Tagged<Object>(that.address());
}
#endif  // V8_ENABLE_DIRECT_HANDLE

template <typename T>
V8_INLINE Handle<T> indirect_handle(DirectHandle<T> handle, Isolate* isolate) {
#ifdef V8_ENABLE_DIRECT_HANDLE
  return Handle<T>(*handle, isolate);
#else
  return handle;
#endif
}

template <typename T>
V8_INLINE Handle<T> indirect_handle(DirectHandle<T> handle,
                                    LocalIsolate* isolate) {
#ifdef V8_ENABLE_DIRECT_HANDLE
  return Handle<T>(*handle, isolate);
#else
  return handle;
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_HANDLES_INL_H_
