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
  static constexpr int kHolderV2Index = T::kHolderV2Index;
  static constexpr int kHolderIndex = T::kHolderIndex;
  static constexpr int kIsolateIndex = T::kIsolateIndex;
  static constexpr int kShouldThrowOnErrorIndex = T::kShouldThrowOnErrorIndex;
  static constexpr int kPropertyKeyIndex = T::kPropertyKeyIndex;

  // This constructor leaves kPropertyKeyIndex and kReturnValueIndex slots
  // uninitialized in order to let them be initialized by the subsequent
  // CallXXX(..) and avoid double initialization. As a consequence, there
  // must be no GC call between this constructor and CallXXX(..).
  // In debug mode these slots are zapped, so GC should be able to detect
  // the misuse of this object.
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

  // Returns the result of [[Get]] operation or throws an exception.
  // In case of exception empty handle is returned.
  // TODO(ishell, 328490288): stop returning empty handles.
  inline Handle<JSAny> CallAccessorGetter(DirectHandle<AccessorInfo> info,
                                          Handle<Name> name);
  // Returns the result of [[Set]] operation or throws an exception.
  V8_WARN_UNUSED_RESULT
  inline bool CallAccessorSetter(DirectHandle<AccessorInfo> info,
                                 Handle<Name> name, Handle<Object> value);

  // -------------------------------------------------------------------------
  // Named Interceptor Callbacks

  // Empty handle means that the request was not intercepted.
  // Pending exception handling should be done by the caller.
  inline Handle<Object> CallNamedQuery(Handle<InterceptorInfo> interceptor,
                                       Handle<Name> name);
  inline Handle<JSAny> CallNamedGetter(Handle<InterceptorInfo> interceptor,
                                       Handle<Name> name);

  // Calls Setter/Definer/Deleter callback and returns whether the request
  // was intercepted.
  // Pending exception handling and interpretation of the result should be
  // done by the caller using GetBooleanReturnValue(..).
  inline v8::Intercepted CallNamedSetter(
      DirectHandle<InterceptorInfo> interceptor, Handle<Name> name,
      Handle<Object> value);
  inline v8::Intercepted CallNamedDefiner(
      DirectHandle<InterceptorInfo> interceptor, Handle<Name> name,
      const v8::PropertyDescriptor& desc);
  inline v8::Intercepted CallNamedDeleter(
      DirectHandle<InterceptorInfo> interceptor, Handle<Name> name);

  // Empty handle means that the request was not intercepted.
  // Pending exception handling should be done by the caller.
  inline Handle<JSAny> CallNamedDescriptor(Handle<InterceptorInfo> interceptor,
                                           Handle<Name> name);
  // Returns JSArray-like object with property names or undefined.
  inline Handle<JSObjectOrUndefined> CallNamedEnumerator(
      Handle<InterceptorInfo> interceptor);

  // -------------------------------------------------------------------------
  // Indexed Interceptor Callbacks

  // Empty handle means that the request was not intercepted.
  // Pending exception handling should be done by the caller.
  inline Handle<Object> CallIndexedQuery(Handle<InterceptorInfo> interceptor,
                                         uint32_t index);
  inline Handle<JSAny> CallIndexedGetter(Handle<InterceptorInfo> interceptor,
                                         uint32_t index);

  // Calls Setter/Definer/Deleter callback and returns whether the request
  // was intercepted.
  // Pending exception handling and interpretation of the result should be
  // done by the caller using GetBooleanReturnValue(..).
  inline v8::Intercepted CallIndexedSetter(
      DirectHandle<InterceptorInfo> interceptor, uint32_t index,
      Handle<Object> value);
  inline v8::Intercepted CallIndexedDefiner(
      DirectHandle<InterceptorInfo> interceptor, uint32_t index,
      const v8::PropertyDescriptor& desc);
  inline v8::Intercepted CallIndexedDeleter(Handle<InterceptorInfo> interceptor,
                                            uint32_t index);

  // Empty handle means that the request was not intercepted.
  // Pending exception handling should be done by the caller.
  inline Handle<JSAny> CallIndexedDescriptor(
      Handle<InterceptorInfo> interceptor, uint32_t index);
  // Returns JSArray-like object with property names or undefined.
  inline Handle<JSObjectOrUndefined> CallIndexedEnumerator(
      Handle<InterceptorInfo> interceptor);

  // Accept potential JavaScript side effects that might occur during life
  // time of this object.
  inline void AcceptSideEffects() {
#ifdef DEBUG
    javascript_execution_counter_ = 0;
#endif  // DEBUG
  }

  // Converts the result of Setter/Definer/Deleter interceptor callback to
  // Maybe<InterceptorResult>.
  // Currently, in certain scenarios the actual boolean result returned by
  // the Setter/Definer operation is ignored and thus we don't need to process
  // the actual return value.
  inline Maybe<InterceptorResult> GetBooleanReturnValue(
      v8::Intercepted intercepted, const char* callback_kind_for_error_message,
      bool ignore_return_value = false);

  // TODO(ishell): cleanup this hack by embedding the PropertyCallbackInfo
  // into PropertyCallbackArguments object.
  template <typename T>
  const v8::PropertyCallbackInfo<T>& GetPropertyCallbackInfo() {
    return *(reinterpret_cast<PropertyCallbackInfo<T>*>(&values_[0]));
  }

  // Forwards ShouldThrowOnError() request to the underlying
  // v8::PropertyCallbackInfo<> object.
  bool ShouldThrowOnError() {
    return GetPropertyCallbackInfo<Value>().ShouldThrowOnError();
  }

  // Unofficial way of getting property key from v8::PropertyCallbackInfo<T>.
  template <typename T>
  static Tagged<Object> GetPropertyKey(const PropertyCallbackInfo<T>& info) {
    return Tagged<Object>(info.args_[kPropertyKeyIndex]);
  }
  template <typename T>
  static Handle<Object> GetPropertyKeyHandle(
      const PropertyCallbackInfo<T>& info) {
    return Handle<Object>(&info.args_[kPropertyKeyIndex]);
  }

  // Returns index value passed to CallIndexedXXX(). This works as long as
  // all the calls to indexed interceptor callbacks are done via
  // PropertyCallbackArguments.
  template <typename T>
  static uint32_t GetPropertyIndex(const PropertyCallbackInfo<T>& info) {
    // Currently all indexed interceptor callbacks are called via
    // PropertyCallbackArguments, so it's guaranteed that
    // v8::PropertyCallbackInfo<T>::args_ array IS the
    // PropertyCallbackArguments::values_ array. As a result we can restore
    // pointer to PropertyCallbackArguments object from the former.
    Address ptr = reinterpret_cast<Address>(&info.args_) -
                  offsetof(PropertyCallbackArguments, values_);
    auto pca = reinterpret_cast<const PropertyCallbackArguments*>(ptr);
    return pca->index_;
  }

 private:
  // Returns JSArray-like object with property names or undefined.
  inline Handle<JSObjectOrUndefined> CallPropertyEnumerator(
      Handle<InterceptorInfo> interceptor);

  inline Tagged<JSObject> holder() const;
  inline Tagged<Object> receiver() const;

  // This field is used for propagating index value from CallIndexedXXX()
  // to ExceptionPropagationCallback.
  uint32_t index_ = kMaxUInt32;

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
  static constexpr int kContextIndex = T::kContextIndex;
  static constexpr int kTargetIndex = T::kTargetIndex;
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

  FunctionCallbackArguments(Isolate* isolate,
                            Tagged<FunctionTemplateInfo> target,
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
  inline Handle<Object> CallOrConstruct(Tagged<FunctionTemplateInfo> function,
                                        bool is_construct);

  // Unofficial way of getting target FunctionTemplateInfo from
  // v8::FunctionCallbackInfo<T>.
  template <typename T>
  static Tagged<Object> GetTarget(const FunctionCallbackInfo<T>& info) {
    return Tagged<Object>(info.implicit_args_[kTargetIndex]);
  }

 private:
  inline Tagged<JSReceiver> holder() const;

  Address* argv_;
  int const argc_;
};

static_assert(BuiltinArguments::kNumExtraArgs ==
              BuiltinExitFrameConstants::kNumExtraArgsWithoutReceiver);
static_assert(BuiltinArguments::kNumExtraArgsWithReceiver ==
              BuiltinExitFrameConstants::kNumExtraArgsWithReceiver);

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_ARGUMENTS_H_
