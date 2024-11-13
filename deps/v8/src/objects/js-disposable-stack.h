// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_DISPOSABLE_STACK_H_
#define V8_OBJECTS_JS_DISPOSABLE_STACK_H_

#include "src/base/bit-field.h"
#include "src/handles/handles.h"
#include "src/objects/contexts.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-objects.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-disposable-stack-tq.inc"

// Valid states for a DisposableStack.
// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack-objects
enum class DisposableStackState { kDisposed, kPending };

// kValueIsReceiver: Call the method with no argument
// kValueIsArgument: Pass the value as the argument to the dispose method,
// `disposablestack.prototype.adopt` is the only method that uses
// kValueIsArgument as DisposeMethodCallType.
enum class DisposeMethodCallType { kValueIsReceiver = 0, kValueIsArgument = 1 };

// Valid hints for a DisposableStack.
// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposableresource-records
enum class DisposeMethodHint { kSyncDispose = 0, kAsyncDispose = 1 };

// Types of disposable resources in a DisposableStack.
enum class DisposableStackResourcesType { kAllSync, kAtLeastOneAsync };

using DisposeCallTypeBit =
    base::BitField<DisposeMethodCallType, 0, 1, uint32_t>;
using DisposeHintBit = DisposeCallTypeBit::Next<DisposeMethodHint, 1>;

class JSDisposableStackBase
    : public TorqueGeneratedJSDisposableStackBase<JSDisposableStackBase,
                                                  JSObject> {
 public:
  DECL_PRINTER(JSDisposableStackBase)
  DECL_VERIFIER(JSDisposableStackBase)

  DEFINE_TORQUE_GENERATED_DISPOSABLE_STACK_STATUS()
  inline DisposableStackState state() const;
  inline void set_state(DisposableStackState value);
  DECL_INT_ACCESSORS(length)

  enum class AsyncDisposableStackContextSlots {
    kStack = Context::MIN_CONTEXT_SLOTS,
    kError,
    kLength,
  };

  enum class AsyncDisposeFromSyncDisposeContextSlots {
    kMethod = Context::MIN_CONTEXT_SLOTS,
    kLength,
  };

  static void InitializeJSDisposableStackBase(
      Isolate* isolate, DirectHandle<JSDisposableStackBase> stack);
  static void Add(Isolate* isolate,
                  DirectHandle<JSDisposableStackBase> disposable_stack,
                  DirectHandle<Object> value, DirectHandle<Object> method,
                  DisposeMethodCallType type, DisposeMethodHint hint);
  static MaybeHandle<Object> CheckValueAndGetDisposeMethod(
      Isolate* isolate, Handle<Object> value, DisposeMethodHint hint);
  static MaybeHandle<Object> DisposeResources(
      Isolate* isolate, DirectHandle<JSDisposableStackBase> disposable_stack,
      MaybeHandle<Object> maybe_error,
      DisposableStackResourcesType resources_type);
  static Handle<JSReceiver> DisposeResourcesAwaitPoint(
      Isolate* isolate, DirectHandle<JSDisposableStackBase> disposable_stack,
      int length, MaybeHandle<Object> result, MaybeHandle<Object> maybe_error);

  TQ_OBJECT_CONSTRUCTORS(JSDisposableStackBase)
};

class JSSyncDisposableStack
    : public TorqueGeneratedJSSyncDisposableStack<JSSyncDisposableStack,
                                                  JSDisposableStackBase> {
 public:
  DECL_VERIFIER(JSSyncDisposableStack)

  TQ_OBJECT_CONSTRUCTORS(JSSyncDisposableStack)
};

class JSAsyncDisposableStack
    : public TorqueGeneratedJSAsyncDisposableStack<JSAsyncDisposableStack,
                                                   JSDisposableStackBase> {
 public:
  DECL_PRINTER(JSAsyncDisposableStack)
  DECL_VERIFIER(JSAsyncDisposableStack)

  TQ_OBJECT_CONSTRUCTORS(JSAsyncDisposableStack)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DISPOSABLE_STACK_H_
