// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INTERPRETER_WASM_INTERPRETER_OBJECTS_H_
#define V8_WASM_INTERPRETER_WASM_INTERPRETER_OBJECTS_H_

#include "src/objects/struct.h"
#include "src/wasm/wasm-value.h"

namespace v8 {
namespace internal {
class Isolate;
class WasmInstanceObject;

namespace wasm {
class InterpreterHandle;
}  // namespace wasm

struct WasmInterpreterStackEntry {
  int function_index;
  int byte_offset;
};

// This class should declare a heap Object, and should derive from Struct. But,
// in order to avoid issues in static-roots.h with the DrumBrake build flag,
// it is better not to introduce DrumBrake-specific types. Therefore we use a
// Tuple2 as WasmInterpreterObject and class WasmInterpreterObject only has
// static methods that receive a Tagged<Tuple2> or Handle<Tuple2> as argument.
//
class WasmInterpreterObject {
 public:
  static inline Tagged<WasmInstanceObject> get_wasm_instance(
      Tagged<Tuple2> interpreter_object);
  static inline void set_wasm_instance(
      Tagged<Tuple2> interpreter_object,
      Tagged<WasmInstanceObject> wasm_instance);

  static inline Tagged<Object> get_interpreter_handle(
      Tagged<Tuple2> interpreter_object);
  static inline void set_interpreter_handle(Tagged<Tuple2> interpreter_object,
                                            Tagged<Object> interpreter_handle);

  static Handle<Tuple2> New(Handle<WasmInstanceObject>);

  // Execute the specified function in the interpreter. Read arguments from the
  // {argument_values} vector and write to {return_values} on regular exit.
  // The frame_pointer will be used to identify the new activation of the
  // interpreter for unwinding and frame inspection.
  // Returns true if exited regularly, false if a trap occurred. In the latter
  // case, a pending exception will have been set on the isolate.
  static bool RunInterpreter(
      Isolate* isolate, Address frame_pointer,
      Handle<WasmInstanceObject> instance, int func_index,
      const std::vector<wasm::WasmValue>& argument_values,
      std::vector<wasm::WasmValue>& return_values);
  static bool RunInterpreter(Isolate* isolate, Address frame_pointer,
                             Handle<WasmInstanceObject> instance,
                             int func_index, uint8_t* interpreter_sp);

  // Get the stack of the wasm interpreter as pairs of {function index, byte
  // offset}. The list is ordered bottom-to-top, i.e. caller before callee.
  static std::vector<WasmInterpreterStackEntry> GetInterpretedStack(
      Tagged<Tuple2> interpreter_object, Address frame_pointer);

  // Get the function index for the index-th frame in the Activation identified
  // by a given frame_pointer.
  static int GetFunctionIndex(Tagged<Tuple2> interpreter_object,
                              Address frame_pointer, int index);
};

namespace wasm {
V8_EXPORT_PRIVATE InterpreterHandle* GetInterpreterHandle(
    Isolate* isolate, Handle<Tuple2> interpreter_object);
V8_EXPORT_PRIVATE InterpreterHandle* GetOrCreateInterpreterHandle(
    Isolate* isolate, Handle<Tuple2> interpreter_object);
}  // namespace wasm

}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_INTERPRETER_WASM_INTERPRETER_OBJECTS_H_
