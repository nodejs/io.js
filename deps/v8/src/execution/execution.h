// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_EXECUTION_H_
#define V8_EXECUTION_EXECUTION_H_

#include "src/base/vector.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class MicrotaskQueue;

class Execution final : public AllStatic {
 public:
  // Whether to report pending messages, or keep them pending on the isolate.
  enum class MessageHandling { kReport, kKeepPending };
  enum class Target { kCallable, kRunMicrotasks };

  // Call a function (that is not a script), the caller supplies a receiver and
  // an array of arguments.
  // When the function called is not in strict mode, receiver is
  // converted to an object.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Call(
      Isolate* isolate, DirectHandle<Object> callable,
      DirectHandle<Object> receiver,
      base::Vector<const DirectHandle<Object>> args);
  // Run a script. For JSFunctions that are not scripts, use Execution::Call.
  // Depending on the script, the host_defined_options might not be used but the
  // caller has to provide it at all times.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object> CallScript(
      Isolate* isolate, DirectHandle<JSFunction> callable,
      DirectHandle<Object> receiver, DirectHandle<Object> host_defined_options);

  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> CallBuiltin(
      Isolate* isolate, DirectHandle<JSFunction> builtin,
      DirectHandle<Object> receiver,
      base::Vector<const DirectHandle<Object>> args);

  // Construct object from function, the caller supplies an array of
  // arguments.
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSReceiver> New(
      Isolate* isolate, DirectHandle<Object> constructor,
      base::Vector<const DirectHandle<Object>> args);
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSReceiver> New(
      Isolate* isolate, DirectHandle<Object> constructor,
      DirectHandle<Object> new_target,
      base::Vector<const DirectHandle<Object>> args);

  // Call a function, just like Call(), but handle don't report exceptions
  // externally.
  // The return value is either the result of calling the function (if no
  // exception occurred), or an empty handle.
  // If message_handling is MessageHandling::kReport, exceptions (except for
  // termination exceptions) will be stored in exception_out (if not a
  // nullptr).
  V8_EXPORT_PRIVATE static MaybeDirectHandle<Object> TryCall(
      Isolate* isolate, DirectHandle<Object> callable,
      DirectHandle<Object> receiver,
      base::Vector<const DirectHandle<Object>> args,
      MessageHandling message_handling,
      MaybeDirectHandle<Object>* exception_out);
  // Same as Execute::TryCall but for scripts which need an explicit
  // host-defined options object. See Execution:CallScript
  V8_EXPORT_PRIVATE static MaybeDirectHandle<Object> TryCallScript(
      Isolate* isolate, DirectHandle<JSFunction> script_function,
      DirectHandle<Object> receiver,
      DirectHandle<FixedArray> host_defined_options);

  // Convenience method for performing RunMicrotasks
  static MaybeDirectHandle<Object> TryRunMicrotasks(
      Isolate* isolate, MicrotaskQueue* microtask_queue);

#if V8_ENABLE_WEBASSEMBLY
  // Call a Wasm function identified by {wasm_call_target} through the
  // provided {wrapper_code}, which must match the function's signature.
  // Upon return, either isolate->has_exception() is true, or
  // the function's return values are in {packed_args}.
  V8_EXPORT_PRIVATE static void CallWasm(Isolate* isolate,
                                         DirectHandle<Code> wrapper_code,
                                         WasmCodePointer wasm_call_target,
                                         DirectHandle<Object> object_ref,
                                         Address packed_args);
#endif  // V8_ENABLE_WEBASSEMBLY
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_EXECUTION_H_
