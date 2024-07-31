// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_API_ARGUMENTS_H_
#define V8_API_API_ARGUMENTS_H_

#include "include/v8-template.h"
#include "src/builtins/builtins-utils.h"
#include "src/execution/isolate.h"
#include "src/objects/slots.h"
#include "src/objects/visitors.h"

namespace v8 {
namespace internal {

// Custom arguments replicate a small segment of stack that can be
// accessed through an Arguments object the same way the actual stack
// can.
class CustomArgumentsBase : public Relocatable {
 protected:
  explicit inline CustomArgumentsBase(Isolate* isolate);
};

template <typename T>
class CustomArguments : public CustomArgumentsBase {
 public:
  static constexpr int kReturnValueIndex = T::kReturnValueIndex;
  static_assert(T::kSize == sizeof(T));

  ~CustomArguments() override;

  inline void IterateInstance(RootVisitor* v) override {
    v->VisitRootPointers(Root::kRelocatable, nullptr, slot_at(0),
                         slot_at(T::kArgsLength));
  }

 protected:
  explicit inline CustomArguments(Isolate* isolate)
      : CustomArgumentsBase(isolate) {}

  template <typename V>
  Handle<V> GetReturnValue(Isolate* isolate) const;

  template <typename V>
  Handle<V> GetReturnValueNoHoleCheck(Isolate* isolate) const;

  inline Isolate* isolate() const {
    return reinterpret_cast<Isolate*>((*slot_at(T::kIsolateIndex)).ptr());
  }

  inline FullObjectSlot slot_at(int index) const {
    // This allows index == T::kArgsLength so "one past the end" slots
    // can be retrieved for iterating purposes.
    DCHECK_LE(static_cast<unsigned>(index),
              static_cast<unsigned>(T::kArgsLength));
    return FullObjectSlot(values_ + index);
  }

  Address values_[T::kArgsLength];
};

// Note: Calling args.Call() sets the return value on args. For multiple
// Call()'s, a new args should be used every time.
// This class also serves as a side effects detection scope (JavaScript code
// execution). It is used for ensuring correctness of the interceptor callback
// implementations. The idea is that the interceptor callback that does not
// intercept an operation must not produce side effects. If the callback
// signals that it has handled the operation (by either returning a respective
// result or by throwing an exception) then the AcceptSideEffects() method
// must be called to "accept" the side effects that have happened during the
// lifetime of the PropertyCallbackArguments object.
class PropertyCallbackArguments final
    : public CustomArguments<PropertyCallbackInfo<Value> > {
 public:
  using T = PropertyCallbackInfo<Value>;
  using Super = CustomArguments<T>;
  static constexpr int kArgsLength = T::kArgsLength;
  static constexpr int kThisIndex = T::kThisIndex;
  static constexpr int kDataIndex = T::kDataIndex;
  static constexpr int kUnusedIndex = T::kUnusedIndex;
  static constexpr int kHolderIndex = T::kHolderIndex;
  static constexpr int kIsolateIndex = T::kIsolateIndex;
  static constexpr int kShouldThrowOnErrorIndex = T::kShouldThrowOnErrorIndex;

  PropertyCallbackArguments(Isolate* isolate, Tagged<Object> data,
                            Tagged<Object> self, Tagged<JSObject> holder,
                            Maybe<ShouldThrow> should_throw);
  inline ~PropertyCallbackArguments();

  // Don't copy PropertyCallbackArguments, because they would both have the
  // same prev_ pointer.
  PropertyCallbackArguments(const PropertyCallbackArguments&) = delete;
  PropertyCallbackArguments& operator=(const PropertyCallbackArguments&) =
      delete;

  // -------------------------------------------------------------------------
  // Accessor Callbacks
  // Also used for AccessorSetterCallback.
  inline Handle<Object> CallAccessorSetter(Handle<AccessorInfo> info,
                                           Handle<Name> name,
                                           Handle<Object> value);
  // Also used for AccessorGetterCallback, AccessorNameGetterCallback.
  inline Handle<Object> CallAccessorGetter(Handle<AccessorInfo> info,
                                           Handle<Name> name);

  // -------------------------------------------------------------------------
  // Named Interceptor Callbacks
  inline Handle<Object> CallNamedQuery(Handle<InterceptorInfo> interceptor,
                                       Handle<Name> name);
  inline Handle<Object> CallNamedGetter(Handle<InterceptorInfo> interceptor,
                                        Handle<Name> name);
  inline Handle<Object> CallNamedSetter(Handle<InterceptorInfo> interceptor,
                                        Handle<Name> name,
                                        Handle<Object> value);
  inline Handle<Object> CallNamedDefiner(Handle<InterceptorInfo> interceptor,
                                         Handle<Name> name,
                                         const v8::PropertyDescriptor& desc);
  inline Handle<Object> CallNamedDeleter(Handle<InterceptorInfo> interceptor,
                                         Handle<Name> name);
  inline Handle<Object> CallNamedDescriptor(Handle<InterceptorInfo> interceptor,
                                            Handle<Name> name);
  inline Handle<JSObject> CallNamedEnumerator(
      Handle<InterceptorInfo> interceptor);

  // -------------------------------------------------------------------------
  // Indexed Interceptor Callbacks
  inline Handle<Object> CallIndexedQuery(Handle<InterceptorInfo> interceptor,
                                         uint32_t index);
  inline Handle<Object> CallIndexedGetter(Handle<InterceptorInfo> interceptor,
                                          uint32_t index);
  inline Handle<Object> CallIndexedSetter(Handle<InterceptorInfo> interceptor,
                                          uint32_t index, Handle<Object> value);
  inline Handle<Object> CallIndexedDefiner(Handle<InterceptorInfo> interceptor,
                                           uint32_t index,
                                           const v8::PropertyDescriptor& desc);
  inline Handle<Object> CallIndexedDeleter(Handle<InterceptorInfo> interceptor,
                                           uint32_t index);
  inline Handle<Object> CallIndexedDescriptor(
      Handle<InterceptorInfo> interceptor, uint32_t index);
  inline Handle<JSObject> CallIndexedEnumerator(
      Handle<InterceptorInfo> interceptor);

  // Accept potential JavaScript side effects that might occurr during life
  // time of this object.
  inline void AcceptSideEffects() {
#ifdef DEBUG
    javascript_execution_counter_ = 0;
#endif  // DEBUG
  }

 private:
  /*
   * The following Call functions wrap the calling of all callbacks to handle
   * calling either the old or the new style callbacks depending on which one
   * has been registered.
   * For old callbacks which return an empty handle, the ReturnValue is checked
   * and used if it's been set to anything inside the callback.
   * New style callbacks always use the return value.
   */
  inline Handle<JSObject> CallPropertyEnumerator(
      Handle<InterceptorInfo> interceptor);

  inline Tagged<JSObject> holder() const;
  inline Tagged<Object> receiver() const;

#ifdef DEBUG
  // This stores current value of Isolate::javascript_execution_counter().
  // It's used for detecting whether JavaScript code was executed between
  // PropertyCallbackArguments's constructor and destructor.
  uint32_t javascript_execution_counter_;
#endif  // DEBUG
};

class FunctionCallbackArguments
    : public CustomArguments<FunctionCallbackInfo<Value> > {
 public:
  using T = FunctionCallbackInfo<Value>;
  using Super = CustomArguments<T>;
  static constexpr int kArgsLength = T::kArgsLength;
  static constexpr int kArgsLengthWithReceiver = T::kArgsLengthWithReceiver;

  static constexpr int kHolderIndex = T::kHolderIndex;
  static constexpr int kIsolateIndex = T::kIsolateIndex;
  static constexpr int kUnusedIndex = T::kUnusedIndex;
  static constexpr int kDataIndex = T::kDataIndex;
  static constexpr int kNewTargetIndex = T::kNewTargetIndex;

  static_assert(T::kThisValuesIndex == BuiltinArguments::kReceiverArgsOffset);

  static constexpr int kSize = T::kSize;
  static constexpr int kImplicitArgsOffset = T::kImplicitArgsOffset;
  static constexpr int kValuesOffset = T::kValuesOffset;
  static constexpr int kLengthOffset = T::kLengthOffset;

  // Make sure all FunctionCallbackInfo constants are in sync.
  static_assert(T::kSize == sizeof(T));
  static_assert(T::kImplicitArgsOffset == offsetof(T, implicit_args_));
  static_assert(T::kValuesOffset == offsetof(T, values_));
  static_assert(T::kLengthOffset == offsetof(T, length_));

  FunctionCallbackArguments(Isolate* isolate, Tagged<Object> data,
                            Tagged<Object> holder,
                            Tagged<HeapObject> new_target, Address* argv,
                            int argc);

  /*
   * The following Call function wraps the calling of all callbacks to handle
   * calling either the old or the new style callbacks depending on which one
   * has been registered.
   * For old callbacks which return an empty handle, the ReturnValue is checked
   * and used if it's been set to anything inside the callback.
   * New style callbacks always use the return value.
   */
  inline Handle<Object> Call(Tagged<FunctionTemplateInfo> function);

 private:
  inline Tagged<JSReceiver> holder() const;

  internal::Address* argv_;
  int const argc_;
};

static_assert(BuiltinArguments::kNumExtraArgs ==
              BuiltinExitFrameConstants::kNumExtraArgsWithoutReceiver);
static_assert(BuiltinArguments::kNumExtraArgsWithReceiver ==
              BuiltinExitFrameConstants::kNumExtraArgsWithReceiver);

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_ARGUMENTS_H_
