// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SCRIPT_H_
#define V8_OBJECTS_SCRIPT_H_

#include <memory>

#include "include/v8-script.h"
#include "src/base/export-template.h"
#include "src/heap/factory-base.h"
#include "src/heap/factory.h"
#include "src/heap/local-factory.h"
#include "src/objects/fixed-array.h"
#include "src/objects/objects.h"
#include "src/objects/struct.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {

namespace internal {

class FunctionLiteral;
class StructBodyDescriptor;

namespace wasm {
class NativeModule;
}  // namespace wasm

#include "torque-generated/src/objects/script-tq.inc"

// Script describes a script which has been added to the VM.
class Script : public TorqueGeneratedScript<Script, Struct> {
 public:
  // Script ID used for temporary scripts, which shouldn't be added to the
  // script list.
  static constexpr int kTemporaryScriptId = -2;

  NEVER_READ_ONLY_SPACE
  // Script types.
  enum class Type {
    kNative = 0,
    kExtension = 1,
    kNormal = 2,
#if V8_ENABLE_WEBASSEMBLY
    kWasm = 3,
#endif  // V8_ENABLE_WEBASSEMBLY
    kInspector = 4
  };

  // Script compilation types.
  enum class CompilationType { kHost = 0, kEval = 1 };

  // Script compilation state.
  enum class CompilationState { kInitial = 0, kCompiled = 1 };

  // [type]: the script type.
  DECL_PRIMITIVE_ACCESSORS(type, Type)

  DECL_ACCESSORS(eval_from_shared_or_wrapped_arguments, Tagged<Object>)

  // [eval_from_shared]: for eval scripts the shared function info for the
  // function from which eval was called.
  DECL_ACCESSORS(eval_from_shared, Tagged<SharedFunctionInfo>)

  // [wrapped_arguments]: for the list of arguments in a wrapped script.
  DECL_ACCESSORS(wrapped_arguments, Tagged<FixedArray>)

  // Whether the script is implicitly wrapped in a function.
  inline bool is_wrapped() const;

  // Whether the eval_from_shared field is set with a shared function info
  // for the eval site.
  inline bool has_eval_from_shared() const;

  // [eval_from_position]: the source position in the code for the function
  // from which eval was called, as positive integer. Or the code offset in the
  // code from which eval was called, as negative integer.
  DECL_INT_ACCESSORS(eval_from_position)

  // [shared_function_infos]: weak fixed array containing all shared
  // function infos created from this script.
  DECL_ACCESSORS(shared_function_infos, Tagged<WeakFixedArray>)

  inline int shared_function_info_count() const;

#if V8_ENABLE_WEBASSEMBLY
  // [wasm_breakpoint_infos]: the list of {BreakPointInfo} objects describing
  // all WebAssembly breakpoints for modules/instances managed via this script.
  // This must only be called if the type of this script is TYPE_WASM.
  DECL_ACCESSORS(wasm_breakpoint_infos, Tagged<FixedArray>)
  inline bool has_wasm_breakpoint_infos() const;

  // [wasm_native_module]: the wasm {NativeModule} this script belongs to.
  // This must only be called if the type of this script is TYPE_WASM.
  DECL_ACCESSORS(wasm_managed_native_module, Tagged<Object>)
  inline wasm::NativeModule* wasm_native_module() const;

  // [wasm_weak_instance_list]: the list of all {WasmInstanceObject} being
  // affected by breakpoints that are managed via this script.
  // This must only be called if the type of this script is TYPE_WASM.
  DECL_ACCESSORS(wasm_weak_instance_list, Tagged<WeakArrayList>)

  // [break_on_entry] (wasm only): whether an instrumentation breakpoint is set
  // for this script; this information will be transferred to existing and
  // future instances to make sure that we stop before executing any code in
  // this wasm module.
  inline bool break_on_entry() const;
  inline void set_break_on_entry(bool value);

  // Check if the script contains any Asm modules.
  bool ContainsAsmModule();
#endif  // V8_ENABLE_WEBASSEMBLY

  // [compilation_type]: how the the script was compiled. Encoded in the
  // 'flags' field.
  inline CompilationType compilation_type();
  inline void set_compilation_type(CompilationType type);

  inline bool produce_compile_hints() const;
  inline void set_produce_compile_hints(bool produce_compile_hints);

  inline bool deserialized() const;
  inline void set_deserialized(bool value);

  // [compilation_state]: determines whether the script has already been
  // compiled. Encoded in the 'flags' field.
  inline CompilationState compilation_state();
  inline void set_compilation_state(CompilationState state);

  // [is_repl_mode]: whether this script originated from a REPL via debug
  // evaluate and therefore has different semantics, e.g. re-declaring let.
  inline bool is_repl_mode() const;
  inline void set_is_repl_mode(bool value);

  // [origin_options]: optional attributes set by the embedder via ScriptOrigin,
  // and used by the embedder to make decisions about the script. V8 just passes
  // this through. Encoded in the 'flags' field.
  inline v8::ScriptOriginOptions origin_options();
  inline void set_origin_options(ScriptOriginOptions origin_options);

  DECL_ACCESSORS(compiled_lazy_function_positions, Tagged<Object>)

  // If script source is an external string, check that the underlying
  // resource is accessible. Otherwise, always return true.
  inline bool HasValidSource();

  // If the script has a non-empty sourceURL comment.
  inline bool HasSourceURLComment() const;

  // Streaming compilation only attaches the source to the Script upon
  // finalization. This predicate returns true, if this script may still be
  // unfinalized.
  inline bool IsMaybeUnfinalized(Isolate* isolate) const;

  Tagged<Object> GetNameOrSourceURL();
  static Handle<String> GetScriptHash(Isolate* isolate, Handle<Script> script,
                                      bool forceForInspector);

  // Retrieve source position from where eval was called.
  static int GetEvalPosition(Isolate* isolate, Handle<Script> script);

  // Initialize line_ends array with source code positions of line ends if
  // it doesn't exist yet.
  static inline void InitLineEnds(Isolate* isolate, Handle<Script> script);
  static inline void InitLineEnds(LocalIsolate* isolate, Handle<Script> script);

  inline bool has_line_ends() const;

  // Will initialize the line ends if required.
  static void SetSource(Isolate* isolate, Handle<Script> script,
                        Handle<String> source);

  bool inline CanHaveLineEnds() const;

  // Carries information about a source position.
  struct PositionInfo {
    PositionInfo() : line(-1), column(-1), line_start(-1), line_end(-1) {}

    int line;        // Zero-based line number.
    int column;      // Zero-based column number.
    int line_start;  // Position of first character in line.
    int line_end;    // Position of final linebreak character in line.
  };

  // Specifies whether to add offsets to position infos.
  enum class OffsetFlag { kNoOffset, kWithOffset };

  // Retrieves information about the given position, optionally with an offset.
  // Returns false on failure, and otherwise writes into the given info object
  // on success.
  // The static method should is preferable for handlified callsites because it
  // initializes the line ends array, avoiding expensive recomputations.
  // The non-static version is not allocating and safe for unhandlified
  // callsites.
  static bool GetPositionInfo(Handle<Script> script, int position,
                              PositionInfo* info,
                              OffsetFlag offset_flag = OffsetFlag::kWithOffset);
  V8_EXPORT_PRIVATE bool GetPositionInfo(
      int position, PositionInfo* info,
      OffsetFlag offset_flag = OffsetFlag::kWithOffset) const;

  // Tells whether this script should be subject to debugging, e.g. for
  // - scope inspection
  // - internal break points
  // - coverage and type profile
  // - error stack trace
  bool IsSubjectToDebugging() const;

  bool IsUserJavaScript() const;

  // Wrappers for GetPositionInfo
  static int GetColumnNumber(Handle<Script> script, int code_offset);
  int GetColumnNumber(int code_pos) const;
  V8_EXPORT_PRIVATE static int GetLineNumber(Handle<Script> script,
                                             int code_offset);
  int GetLineNumber(int code_pos) const;

  // Look through the list of existing shared function infos to find one
  // that matches the function literal. Return empty handle if not found.
  template <typename IsolateT>
  static MaybeHandle<SharedFunctionInfo> FindSharedFunctionInfo(
      Handle<Script> script, IsolateT* isolate,
      FunctionLiteral* function_literal);

  // Iterate over all script objects on the heap.
  class V8_EXPORT_PRIVATE Iterator {
   public:
    explicit Iterator(Isolate* isolate);
    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    Tagged<Script> Next();

   private:
    WeakArrayList::Iterator iterator_;
  };

  // Dispatched behavior.
  DECL_PRINTER(Script)
  DECL_VERIFIER(Script)

  using BodyDescriptor = StructBodyDescriptor;

 private:
  friend Factory;
  friend FactoryBase<Factory>;
  friend FactoryBase<LocalFactory>;

  // Hide torque-generated accessor, use Script::SetSource instead.
  using TorqueGeneratedScript::set_source;

  // Bit positions in the flags field.
  DEFINE_TORQUE_GENERATED_SCRIPT_FLAGS()

  TQ_OBJECT_CONSTRUCTORS(Script)

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  static void V8_PRESERVE_MOST
      InitLineEndsInternal(IsolateT* isolate, Handle<Script> script);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SCRIPT_H_
