// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_API_INL_H_
#define V8_API_API_INL_H_

#include "include/v8-fast-api-calls.h"
#include "src/api/api.h"
#include "src/common/assert-scope.h"
#include "src/execution/microtask-queue.h"
#include "src/flags/flags.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/objects/foreign-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {

template <typename T, internal::ExternalPointerTag tag>
inline T ToCData(i::Isolate* isolate,
                 v8::internal::Tagged<v8::internal::Object> obj) {
  static_assert(sizeof(T) == sizeof(v8::internal::Address));
  if (obj == v8::internal::Smi::zero()) return nullptr;
  return reinterpret_cast<T>(
      v8::internal::Cast<v8::internal::Foreign>(obj)->foreign_address<tag>(
          isolate));
}

template <internal::ExternalPointerTag tag>
inline v8::internal::Address ToCData(
    i::Isolate* isolate, v8::internal::Tagged<v8::internal::Object> obj) {
  if (obj == v8::internal::Smi::zero()) return v8::internal::kNullAddress;
  return v8::internal::Cast<v8::internal::Foreign>(obj)->foreign_address<tag>(
      isolate);
}

template <internal::ExternalPointerTag tag, typename T>
inline v8::internal::Handle<i::UnionOf<i::Smi, i::Foreign>> FromCData(
    v8::internal::Isolate* isolate, T obj) {
  static_assert(sizeof(T) == sizeof(v8::internal::Address));
  if (obj == nullptr) return handle(v8::internal::Smi::zero(), isolate);
  return isolate->factory()->NewForeign<tag>(
      reinterpret_cast<v8::internal::Address>(obj));
}

template <internal::ExternalPointerTag tag>
inline v8::internal::Handle<i::UnionOf<i::Smi, i::Foreign>> FromCData(
    v8::internal::Isolate* isolate, v8::internal::Address obj) {
  if (obj == v8::internal::kNullAddress) {
    return handle(v8::internal::Smi::zero(), isolate);
  }
  return isolate->factory()->NewForeign<tag>(obj);
}

template <class From, class To>
inline Local<To> Utils::Convert(v8::internal::DirectHandle<From> obj) {
  DCHECK(obj.is_null() || IsSmi(*obj) || !IsTheHole(*obj));
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (obj.is_null()) return Local<To>();
  return Local<To>::FromAddress(obj.address());
#else
  return Local<To>::FromSlot(obj.location());
#endif
}

// Implementations of ToLocal

#define MAKE_TO_LOCAL(Name)                                                  \
  template <template <typename T> typename HandleType, typename T, typename> \
  inline auto Utils::Name(HandleType<T> obj) {                               \
    return Utils::Name##_helper(v8::internal::DirectHandle<T>(obj));         \
  }

TO_LOCAL_NAME_LIST(MAKE_TO_LOCAL)

#define MAKE_TO_LOCAL_PRIVATE(Name, From, To)               \
  inline Local<v8::To> Utils::Name##_helper(                \
      v8::internal::DirectHandle<v8::internal::From> obj) { \
    return Convert<v8::internal::From, v8::To>(obj);        \
  }

TO_LOCAL_LIST(MAKE_TO_LOCAL_PRIVATE)

#define MAKE_TO_LOCAL_TYPED_ARRAY(Type, typeName, TYPE, ctype)        \
  Local<v8::Type##Array> Utils::ToLocal##Type##Array(                 \
      v8::internal::DirectHandle<v8::internal::JSTypedArray> obj) {   \
    DCHECK(obj->type() == v8::internal::kExternal##Type##Array);      \
    return Convert<v8::internal::JSTypedArray, v8::Type##Array>(obj); \
  }

TYPED_ARRAYS(MAKE_TO_LOCAL_TYPED_ARRAY)

#undef MAKE_TO_LOCAL_TYPED_ARRAY
#undef MAKE_TO_LOCAL
#undef MAKE_TO_LOCAL_PRIVATE
#undef TO_LOCAL_LIST

// Implementations of OpenHandle

#ifdef V8_ENABLE_DIRECT_HANDLE

#define MAKE_OPEN_HANDLE(From, To)                                           \
  v8::internal::Handle<v8::internal::To> Utils::OpenHandle(                  \
      const v8::From* that, bool allow_empty_handle) {                       \
    DCHECK(allow_empty_handle || !v8::internal::ValueHelper::IsEmpty(that)); \
    DCHECK(v8::internal::ValueHelper::IsEmpty(that) ||                       \
           Is##To(v8::internal::Tagged<v8::internal::Object>(                \
               v8::internal::ValueHelper::ValueAsAddress(that))));           \
    if (v8::internal::ValueHelper::IsEmpty(that)) {                          \
      return v8::internal::Handle<v8::internal::To>::null();                 \
    }                                                                        \
    return v8::internal::Handle<v8::internal::To>(                           \
        v8::HandleScope::CreateHandleForCurrentIsolate(                      \
            v8::internal::ValueHelper::ValueAsAddress(that)));               \
  }                                                                          \
                                                                             \
  v8::internal::DirectHandle<v8::internal::To> Utils::OpenDirectHandle(      \
      const v8::From* that, bool allow_empty_handle) {                       \
    DCHECK(allow_empty_handle || !v8::internal::ValueHelper::IsEmpty(that)); \
    DCHECK(v8::internal::ValueHelper::IsEmpty(that) ||                       \
           Is##To(v8::internal::Tagged<v8::internal::Object>(                \
               v8::internal::ValueHelper::ValueAsAddress(that))));           \
    return v8::internal::DirectHandle<v8::internal::To>(                     \
        v8::internal::ValueHelper::ValueAsAddress(that));                    \
  }                                                                          \
                                                                             \
  v8::internal::IndirectHandle<v8::internal::To> Utils::OpenIndirectHandle(  \
      const v8::From* that, bool allow_empty_handle) {                       \
    return Utils::OpenHandle(that, allow_empty_handle);                      \
  }

#else  // !V8_ENABLE_DIRECT_HANDLE

#define MAKE_OPEN_HANDLE(From, To)                                           \
  v8::internal::Handle<v8::internal::To> Utils::OpenHandle(                  \
      const v8::From* that, bool allow_empty_handle) {                       \
    DCHECK(allow_empty_handle || !v8::internal::ValueHelper::IsEmpty(that)); \
    DCHECK(v8::internal::ValueHelper::IsEmpty(that) ||                       \
           Is##To(v8::internal::Tagged<v8::internal::Object>(                \
               v8::internal::ValueHelper::ValueAsAddress(that))));           \
    return v8::internal::Handle<v8::internal::To>(                           \
        reinterpret_cast<v8::internal::Address*>(                            \
            const_cast<v8::From*>(that)));                                   \
  }                                                                          \
                                                                             \
  v8::internal::DirectHandle<v8::internal::To> Utils::OpenDirectHandle(      \
      const v8::From* that, bool allow_empty_handle) {                       \
    return Utils::OpenHandle(that, allow_empty_handle);                      \
  }                                                                          \
                                                                             \
  v8::internal::IndirectHandle<v8::internal::To> Utils::OpenIndirectHandle(  \
      const v8::From* that, bool allow_empty_handle) {                       \
    return Utils::OpenHandle(that, allow_empty_handle);                      \
  }

#endif  // V8_ENABLE_DIRECT_HANDLE

OPEN_HANDLE_LIST(MAKE_OPEN_HANDLE)

#undef MAKE_OPEN_HANDLE
#undef OPEN_HANDLE_LIST

template <bool do_callback>
class V8_NODISCARD CallDepthScope {
 public:
  CallDepthScope(i::Isolate* isolate, Local<Context> context)
      : isolate_(isolate), saved_context_(isolate->context(), isolate_) {
    isolate_->thread_local_top()->IncrementCallDepth<do_callback>(this);
    i::Tagged<i::NativeContext> env = *Utils::OpenDirectHandle(*context);
    isolate->set_context(env);

    if (do_callback) isolate_->FireBeforeCallEnteredCallback();
  }
  ~CallDepthScope() {
    i::MicrotaskQueue* microtask_queue =
        i::Cast<i::NativeContext>(isolate_->context())
            ->microtask_queue(isolate_);

    isolate_->thread_local_top()->DecrementCallDepth(this);
    // Clear the exception when exiting V8 to avoid memory leaks.
    // Also clear termination exceptions iff there's no TryCatch handler.
    // TODO(verwaest): Drop this once we propagate exceptions to external
    // TryCatch on Throw. This should be debug-only.
    if (isolate_->thread_local_top()->CallDepthIsZero() &&
        (isolate_->thread_local_top()->try_catch_handler_ == nullptr ||
         !isolate_->is_execution_terminating())) {
      isolate_->clear_internal_exception();
    }
    if (do_callback) isolate_->FireCallCompletedCallback(microtask_queue);
#ifdef DEBUG
    if (do_callback) {
      if (microtask_queue && microtask_queue->microtasks_policy() ==
                                 v8::MicrotasksPolicy::kScoped) {
        DCHECK(microtask_queue->GetMicrotasksScopeDepth() ||
               !microtask_queue->DebugMicrotasksScopeDepthIsZero());
      }
    }
    DCHECK(CheckKeptObjectsClearedAfterMicrotaskCheckpoint(microtask_queue));
#endif

    isolate_->set_context(*saved_context_);
  }

  CallDepthScope(const CallDepthScope&) = delete;
  CallDepthScope& operator=(const CallDepthScope&) = delete;

 private:
#ifdef DEBUG
  bool CheckKeptObjectsClearedAfterMicrotaskCheckpoint(
      i::MicrotaskQueue* microtask_queue) {
    bool did_perform_microtask_checkpoint =
        isolate_->thread_local_top()->CallDepthIsZero() && do_callback &&
        microtask_queue &&
        microtask_queue->microtasks_policy() == MicrotasksPolicy::kAuto &&
        !isolate_->is_execution_terminating();
    return !did_perform_microtask_checkpoint ||
           IsUndefined(isolate_->heap()->weak_refs_keep_during_job(), isolate_);
  }
#endif

  i::Isolate* const isolate_;
  i::Handle<i::Context> saved_context_;

  i::Address previous_stack_height_;

  friend class i::ThreadLocalTop;

  DISALLOW_NEW_AND_DELETE()
};

class V8_NODISCARD InternalEscapableScope : public EscapableHandleScopeBase {
 public:
  explicit inline InternalEscapableScope(i::Isolate* isolate)
      : EscapableHandleScopeBase(reinterpret_cast<v8::Isolate*>(isolate)) {}

  /**
   * Pushes the value into the previous scope and returns a handle to it.
   * Cannot be called twice.
   */
  template <class T>
  V8_INLINE Local<T> Escape(Local<T> value) {
#ifdef V8_ENABLE_DIRECT_HANDLE
    return value;
#else
    DCHECK(!value.IsEmpty());
    return Local<T>::FromSlot(EscapeSlot(value.slot()));
#endif
  }

  template <class T>
  V8_INLINE MaybeLocal<T> EscapeMaybe(MaybeLocal<T> maybe_value) {
    Local<T> value;
    if (!maybe_value.ToLocal(&value)) return maybe_value;
    return Escape(value);
  }
};

template <typename T>
void CopySmiElementsToTypedBuffer(T* dst, uint32_t length,
                                  i::Tagged<i::FixedArray> elements) {
  for (uint32_t i = 0; i < length; ++i) {
    double value = i::Object::NumberValue(
        i::Cast<i::Smi>(elements->get(static_cast<int>(i))));
    // TODO(mslekova): Avoid converting back-and-forth when possible, e.g
    // avoid int->double->int conversions to boost performance.
    dst[i] = i::ConvertDouble<T>(value);
  }
}

template <typename T>
void CopyDoubleElementsToTypedBuffer(T* dst, uint32_t length,
                                     i::Tagged<i::FixedDoubleArray> elements) {
  for (uint32_t i = 0; i < length; ++i) {
    double value = elements->get_scalar(static_cast<int>(i));
    // TODO(mslekova): There are certain cases, e.g. double->double, in which
    // we could do a memcpy directly.
    dst[i] = i::ConvertDouble<T>(value);
  }
}

template <CTypeInfo::Identifier type_info_id, typename T>
bool CopyAndConvertArrayToCppBuffer(Local<Array> src, T* dst,
                                    uint32_t max_length) {
  static_assert(
      std::is_same<T, typename i::CTypeInfoTraits<
                          CTypeInfo(type_info_id).GetType()>::ctype>::value,
      "Type mismatch between the expected CTypeInfo::Type and the destination "
      "array");

  uint32_t length = src->Length();
  if (length > max_length) {
    return false;
  }

  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::JSArray> obj = *Utils::OpenDirectHandle(*src);
  if (i::Object::IterationHasObservableEffects(obj)) {
    // The array has a custom iterator.
    return false;
  }

  i::Tagged<i::FixedArrayBase> elements = obj->elements();
  switch (obj->GetElementsKind()) {
    case i::PACKED_SMI_ELEMENTS:
      CopySmiElementsToTypedBuffer(dst, length,
                                   i::Cast<i::FixedArray>(elements));
      return true;
    case i::PACKED_DOUBLE_ELEMENTS:
      CopyDoubleElementsToTypedBuffer(dst, length,
                                      i::Cast<i::FixedDoubleArray>(elements));
      return true;
    default:
      return false;
  }
}

// Deprecated; to be removed.
template <const CTypeInfo* type_info, typename T>
inline bool V8_EXPORT TryCopyAndConvertArrayToCppBuffer(Local<Array> src,
                                                        T* dst,
                                                        uint32_t max_length) {
  return CopyAndConvertArrayToCppBuffer<type_info->GetId(), T>(src, dst,
                                                               max_length);
}

template <CTypeInfo::Identifier type_info_id, typename T>
inline bool V8_EXPORT TryToCopyAndConvertArrayToCppBuffer(Local<Array> src,
                                                          T* dst,
                                                          uint32_t max_length) {
  return CopyAndConvertArrayToCppBuffer<type_info_id, T>(src, dst, max_length);
}

namespace internal {

void HandleScopeImplementer::EnterContext(Tagged<NativeContext> context) {
  entered_contexts_.push_back(context);
}

Handle<NativeContext> HandleScopeImplementer::LastEnteredContext() {
  if (entered_contexts_.empty()) return {};
  return handle(entered_contexts_.back(), isolate_);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_INL_H_
