// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_ACCESSORS_H_
#define V8_BUILTINS_ACCESSORS_H_

#include "include/v8-local-handle.h"
#include "src/base/bit-field.h"
#include "src/common/globals.h"
#include "src/objects/property-details.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// Forward declarations.
class AccessorInfo;
class FieldIndex;
class JavaScriptFrame;

// The list of accessor descriptors. This is a second-order macro
// taking a macro to be applied to all accessor descriptor names.
// V(accessor_name, AccessorName, GetterSideEffectType, SetterSideEffectType)
#define ACCESSOR_INFO_LIST_GENERATOR(V, _)                                    \
  V(_, arguments_iterator, ArgumentsIterator, kHasNoSideEffect,               \
    kHasSideEffectToReceiver)                                                 \
  V(_, array_length, ArrayLength, kHasNoSideEffect, kHasSideEffectToReceiver) \
  V(_, bound_function_length, BoundFunctionLength, kHasNoSideEffect,          \
    kHasSideEffectToReceiver)                                                 \
  V(_, bound_function_name, BoundFunctionName, kHasNoSideEffect,              \
    kHasSideEffectToReceiver)                                                 \
  V(_, function_arguments, FunctionArguments, kHasNoSideEffect,               \
    kHasSideEffectToReceiver)                                                 \
  V(_, function_caller, FunctionCaller, kHasNoSideEffect,                     \
    kHasSideEffectToReceiver)                                                 \
  V(_, function_name, FunctionName, kHasNoSideEffect,                         \
    kHasSideEffectToReceiver)                                                 \
  V(_, function_length, FunctionLength, kHasNoSideEffect,                     \
    kHasSideEffectToReceiver)                                                 \
  V(_, function_prototype, FunctionPrototype, kHasNoSideEffect,               \
    kHasSideEffectToReceiver)                                                 \
  V(_, string_length, StringLength, kHasNoSideEffect,                         \
    kHasSideEffectToReceiver)                                                 \
  V(_, value_unavailable, ValueUnavailable, kHasNoSideEffect,                 \
    kHasSideEffectToReceiver)                                                 \
  V(_, wrapped_function_length, WrappedFunctionLength, kHasNoSideEffect,      \
    kHasSideEffectToReceiver)                                                 \
  V(_, wrapped_function_name, WrappedFunctionName, kHasNoSideEffect,          \
    kHasSideEffectToReceiver)

#define ACCESSOR_GETTER_LIST(V) V(ModuleNamespaceEntryGetter)

#define ACCESSOR_SETTER_LIST(V) \
  V(ArrayLengthSetter)          \
  V(FunctionPrototypeSetter)    \
  V(ModuleNamespaceEntrySetter) \
  V(ReconfigureToDataProperty)

#define ACCESSOR_CALLBACK_LIST_GENERATOR(V, _) \
  V(_, ErrorStackGetter, kHasSideEffect)       \
  V(_, ErrorStackSetter, kHasSideEffectToReceiver)

// Accessors contains all predefined proxy accessors.

class Accessors : public AllStatic {
 public:
#define ACCESSOR_GETTER_DECLARATION(_, accessor_name, AccessorName, ...) \
  static void AccessorName##Getter(                                      \
      v8::Local<v8::Name> name,                                          \
      const v8::PropertyCallbackInfo<v8::Value>& info);
  ACCESSOR_INFO_LIST_GENERATOR(ACCESSOR_GETTER_DECLARATION, /* not used */)
#undef ACCESSOR_GETTER_DECLARATION

#define ACCESSOR_GETTER_DECLARATION(AccessorName)    \
  static void AccessorName(v8::Local<v8::Name> name, \
                           const v8::PropertyCallbackInfo<v8::Value>& info);
  ACCESSOR_GETTER_LIST(ACCESSOR_GETTER_DECLARATION)
#undef ACCESSOR_GETTER_DECLARATION

#define ACCESSOR_SETTER_DECLARATION(AccessorName)      \
  static void AccessorName(v8::Local<v8::Name> name,   \
                           v8::Local<v8::Value> value, \
                           const v8::PropertyCallbackInfo<v8::Boolean>& info);
  ACCESSOR_SETTER_LIST(ACCESSOR_SETTER_DECLARATION)
#undef ACCESSOR_SETTER_DECLARATION

#define ACCESSOR_CALLBACK_DECLARATION(_, AccessorName, ...) \
  static void AccessorName(const v8::FunctionCallbackInfo<v8::Value>& info);
  ACCESSOR_CALLBACK_LIST_GENERATOR(ACCESSOR_CALLBACK_DECLARATION,
                                   /* not used */)
#undef ACCESSOR_CALLBACK_DECLARATION

#define COUNT_ACCESSOR(...) +1
  static constexpr int kAccessorInfoCount =
      ACCESSOR_INFO_LIST_GENERATOR(COUNT_ACCESSOR, /* not used */);

  static constexpr int kAccessorGetterCount =
      ACCESSOR_GETTER_LIST(COUNT_ACCESSOR);

  static constexpr int kAccessorSetterCount =
      ACCESSOR_SETTER_LIST(COUNT_ACCESSOR);

  static constexpr int kAccessorCallbackCount =
      ACCESSOR_CALLBACK_LIST_GENERATOR(COUNT_ACCESSOR, /* not used */);
#undef COUNT_ACCESSOR

  static DirectHandle<AccessorInfo> MakeModuleNamespaceEntryInfo(
      Isolate* isolate, DirectHandle<String> name);

  // Accessor function called directly from the runtime system. Returns the
  // newly materialized arguments object for the given {frame}. Note that for
  // optimized frames it is possible to specify an {inlined_jsframe_index}.
  static Handle<JSObject> FunctionGetArguments(JavaScriptFrame* frame,
                                               int inlined_jsframe_index);

  // Returns true for properties that are accessors to object fields.
  // If true, the matching FieldIndex is returned through |field_index|.
  static bool IsJSObjectFieldAccessor(Isolate* isolate, DirectHandle<Map> map,
                                      DirectHandle<Name> name,
                                      FieldIndex* field_index);

  static MaybeDirectHandle<Object> ReplaceAccessorWithDataProperty(
      Isolate* isolate, DirectHandle<JSAny> receiver,
      DirectHandle<JSObject> holder, DirectHandle<Name> name,
      DirectHandle<Object> value);

  // Create an AccessorInfo. The setter is optional (can be nullptr).
  //
  // Note that the type of setter is AccessorNameBooleanSetterCallback instead
  // of v8::AccessorNameSetterCallback.  The difference is that the former can
  // set a (boolean) return value. The setter should roughly follow the same
  // conventions as many of the internal methods in objects.cc:
  // - The return value is unset iff there was an exception.
  // - If the ShouldThrow argument is true, the return value must not be false.
  using AccessorNameBooleanSetterCallback =
      void (*)(Local<v8::Name> property, Local<v8::Value> value,
               const PropertyCallbackInfo<v8::Boolean>& info);

  V8_EXPORT_PRIVATE static DirectHandle<AccessorInfo> MakeAccessor(
      Isolate* isolate, DirectHandle<Name> name,
      AccessorNameGetterCallback getter,
      AccessorNameBooleanSetterCallback setter);

 private:
#define ACCESSOR_INFO_DECLARATION(_, accessor_name, AccessorName, ...) \
  static DirectHandle<AccessorInfo> Make##AccessorName##Info(Isolate* isolate);
  ACCESSOR_INFO_LIST_GENERATOR(ACCESSOR_INFO_DECLARATION, /* not used */)
#undef ACCESSOR_INFO_DECLARATION

  friend class Heap;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_ACCESSORS_H_
