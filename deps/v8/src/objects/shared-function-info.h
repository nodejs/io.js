// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SHARED_FUNCTION_INFO_H_
#define V8_OBJECTS_SHARED_FUNCTION_INFO_H_

#include <memory>
#include <optional>

#include "src/base/bit-field.h"
#include "src/builtins/builtins.h"
#include "src/codegen/bailout-reason.h"
#include "src/common/globals.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/function-kind.h"
#include "src/objects/function-syntax-kind.h"
#include "src/objects/name.h"
#include "src/objects/objects.h"
#include "src/objects/script.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/objects/struct.h"
#include "src/roots/roots.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

class AsmWasmData;
class BytecodeArray;
class CoverageInfo;
class DebugInfo;
class IsCompiledScope;
template <typename>
class Signature;
class WasmFunctionData;
class WasmCapiFunctionData;
class WasmExportedFunctionData;
class WasmJSFunctionData;
class WasmResumeData;

#if V8_ENABLE_WEBASSEMBLY
namespace wasm {
struct WasmModule;
class ValueType;
using FunctionSig = Signature<ValueType>;
}  // namespace wasm
#endif

#include "torque-generated/src/objects/shared-function-info-tq.inc"

// Defines whether the source positions should be created during function
// compilation.
enum class CreateSourcePositions { kNo, kYes };

// Data collected by the pre-parser storing information about scopes and inner
// functions.
//
// PreparseData Layout:
// +-------------------------------+
// | data_length | children_length |
// +-------------------------------+
// | Scope Byte Data ...           |
// | ...                           |
// +-------------------------------+
// | [Padding]                     |
// +-------------------------------+
// | Inner PreparseData 1          |
// +-------------------------------+
// | ...                           |
// +-------------------------------+
// | Inner PreparseData N          |
// +-------------------------------+
class PreparseData
    : public TorqueGeneratedPreparseData<PreparseData, HeapObject> {
 public:
  inline int inner_start_offset() const;
  inline ObjectSlot inner_data_start() const;

  inline uint8_t get(int index) const;
  inline void set(int index, uint8_t value);
  inline void copy_in(int index, const uint8_t* buffer, int length);

  inline Tagged<PreparseData> get_child(int index) const;
  inline void set_child(int index, Tagged<PreparseData> value,
                        WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Clear uninitialized padding space.
  inline void clear_padding();

  DECL_PRINTER(PreparseData)
  DECL_VERIFIER(PreparseData)

  static const int kDataStartOffset = kSize;

  class BodyDescriptor;

  static int InnerOffset(int data_length) {
    return RoundUp(kDataStartOffset + data_length * kByteSize, kTaggedSize);
  }

  static int SizeFor(int data_length, int children_length) {
    return InnerOffset(data_length) + children_length * kTaggedSize;
  }

  TQ_OBJECT_CONSTRUCTORS(PreparseData)

 private:
  inline Tagged<Object> get_child_raw(int index) const;
};

// Abstract class representing extra data for an uncompiled function, which is
// not stored in the SharedFunctionInfo.
class UncompiledData
    : public TorqueGeneratedUncompiledData<UncompiledData,
                                           ExposedTrustedObject> {
 public:
  inline void InitAfterBytecodeFlush(
      IsolateForSandbox isolate, Tagged<String> inferred_name,
      int start_position, int end_position,
      std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                         Tagged<HeapObject> target)>
          gc_notify_updated_slot);

  TQ_OBJECT_CONSTRUCTORS(UncompiledData)
};

// Class representing data for an uncompiled function that does not have any
// data from the pre-parser, either because it's a leaf function or because the
// pre-parser bailed out.
class UncompiledDataWithoutPreparseData
    : public TorqueGeneratedUncompiledDataWithoutPreparseData<
          UncompiledDataWithoutPreparseData, UncompiledData> {
 public:
  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(UncompiledDataWithoutPreparseData)
};

// Class representing data for an uncompiled function that has pre-parsed scope
// data.
class UncompiledDataWithPreparseData
    : public TorqueGeneratedUncompiledDataWithPreparseData<
          UncompiledDataWithPreparseData, UncompiledData> {
 public:
  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(UncompiledDataWithPreparseData)
};

// Class representing data for an uncompiled function that does not have any
// data from the pre-parser, either because it's a leaf function or because the
// pre-parser bailed out, but has a job pointer.
class UncompiledDataWithoutPreparseDataWithJob
    : public TorqueGeneratedUncompiledDataWithoutPreparseDataWithJob<
          UncompiledDataWithoutPreparseDataWithJob,
          UncompiledDataWithoutPreparseData> {
 public:
  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(UncompiledDataWithoutPreparseDataWithJob)
};

// Class representing data for an uncompiled function that has pre-parsed scope
// data and a job pointer.
class UncompiledDataWithPreparseDataAndJob
    : public TorqueGeneratedUncompiledDataWithPreparseDataAndJob<
          UncompiledDataWithPreparseDataAndJob,
          UncompiledDataWithPreparseData> {
 public:
  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(UncompiledDataWithPreparseDataAndJob)
};

class InterpreterData
    : public TorqueGeneratedInterpreterData<InterpreterData,
                                            ExposedTrustedObject> {
 public:
  DECL_PROTECTED_POINTER_ACCESSORS(bytecode_array, BytecodeArray)
  DECL_PROTECTED_POINTER_ACCESSORS(interpreter_trampoline, Code)

  class BodyDescriptor;

 private:
  TQ_OBJECT_CONSTRUCTORS(InterpreterData)
};

using NameOrScopeInfoT = UnionOf<Smi, String, ScopeInfo>;

// SharedFunctionInfo describes the JSFunction information that can be
// shared by multiple instances of the function.
class SharedFunctionInfo
    : public TorqueGeneratedSharedFunctionInfo<SharedFunctionInfo, HeapObject> {
 public:
  DEFINE_TORQUE_GENERATED_SHARED_FUNCTION_INFO_FLAGS()
  DEFINE_TORQUE_GENERATED_SHARED_FUNCTION_INFO_FLAGS2()

  // This initializes the SharedFunctionInfo after allocation. It must
  // initialize all fields, and leave the SharedFunctionInfo in a state where
  // it is safe for the GC to visit it.
  //
  // Important: This function MUST not allocate.
  void Init(ReadOnlyRoots roots, int unique_id);

  V8_EXPORT_PRIVATE static constexpr Tagged<Smi> const kNoSharedNameSentinel =
      Smi::zero();

  // [name]: Returns shared name if it exists or an empty string otherwise.
  inline Tagged<String> Name() const;
  inline void SetName(Tagged<String> name);

  // Get the code object which represents the execution of this function.
  V8_EXPORT_PRIVATE Tagged<Code> GetCode(Isolate* isolate) const;

  // Get the abstract code associated with the function, which will either be
  // a Code object or a BytecodeArray.
  inline Tagged<AbstractCode> abstract_code(Isolate* isolate);

  // Set up the link between shared function info and the script. The shared
  // function info is added to the list on the script.
  V8_EXPORT_PRIVATE void SetScript(IsolateForSandbox isolate,
                                   ReadOnlyRoots roots,
                                   Tagged<HeapObject> script_object,
                                   int function_literal_id,
                                   bool reset_preparsed_scope_data = true);

  // Copy the data from another SharedFunctionInfo. Used for copying data into
  // and out of a placeholder SharedFunctionInfo, for off-thread compilation
  // which is not allowed to touch a main-thread-visible SharedFunctionInfo.
  void CopyFrom(Tagged<SharedFunctionInfo> other, IsolateForSandbox isolate);

  // Layout description of the optimized code map.
  static const int kEntriesStart = 0;
  static const int kContextOffset = 0;
  static const int kCachedCodeOffset = 1;
  static const int kEntryLength = 2;
  static const int kInitialLength = kEntriesStart + kEntryLength;

  static const int kNotFound = -1;

  static constexpr int kAgeSize = kAgeOffsetEnd - kAgeOffset + 1;
  static constexpr uint16_t kMaxAge = UINT16_MAX;

  DECL_ACQUIRE_GETTER(scope_info, Tagged<ScopeInfo>)
  // Deprecated, use the ACQUIRE version instead.
  DECL_GETTER(scope_info, Tagged<ScopeInfo>)
  // Slow but safe:
  inline Tagged<ScopeInfo> EarlyScopeInfo(AcquireLoadTag tag);

  // Set scope_info without moving the existing name onto the ScopeInfo.
  inline void set_raw_scope_info(Tagged<ScopeInfo> scope_info,
                                 WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline void SetScopeInfo(Tagged<ScopeInfo> scope_info,
                           WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline bool is_script() const;
  inline bool needs_script_context() const;

  // End position of this function in the script source.
  V8_EXPORT_PRIVATE int EndPosition() const;

  // Start position of this function in the script source.
  V8_EXPORT_PRIVATE int StartPosition() const;

  V8_EXPORT_PRIVATE void UpdateFromFunctionLiteralForLiveEdit(
      IsolateForSandbox isolate, FunctionLiteral* lit);

  // [outer scope info | feedback metadata] Shared storage for outer scope info
  // (on uncompiled functions) and feedback metadata (on compiled functions).
  DECL_ACCESSORS(raw_outer_scope_info_or_feedback_metadata, Tagged<HeapObject>)
  DECL_ACQUIRE_GETTER(raw_outer_scope_info_or_feedback_metadata,
                      Tagged<HeapObject>)
 private:
  using TorqueGeneratedSharedFunctionInfo::
      outer_scope_info_or_feedback_metadata;
  using TorqueGeneratedSharedFunctionInfo::
      set_outer_scope_info_or_feedback_metadata;

 public:
  // Get the outer scope info whether this function is compiled or not.
  inline bool HasOuterScopeInfo() const;
  inline Tagged<ScopeInfo> GetOuterScopeInfo() const;

  // [feedback metadata] Metadata template for feedback vectors of instances of
  // this function.
  inline bool HasFeedbackMetadata() const;
  inline bool HasFeedbackMetadata(AcquireLoadTag tag) const;
  DECL_GETTER(feedback_metadata, Tagged<FeedbackMetadata>)
  DECL_RELEASE_ACQUIRE_ACCESSORS(feedback_metadata, Tagged<FeedbackMetadata>)

  // Returns if this function has been compiled yet. Note: with bytecode
  // flushing, any GC after this call is made could cause the function
  // to become uncompiled. If you need to ensure the function remains compiled
  // for some period of time, use IsCompiledScope instead.
  inline bool is_compiled() const;

  // Returns an IsCompiledScope which reports whether the function is compiled,
  // and if compiled, will avoid the function becoming uncompiled while it is
  // held.
  template <typename IsolateT>
  inline IsCompiledScope is_compiled_scope(IsolateT* isolate) const;

  // [internal formal parameter count]: The declared number of parameters.
  // For subclass constructors, also includes new.target.
  // The size of function's frame is
  // internal_formal_parameter_count_with_receiver.
  //
  // NOTE: this API should be considered DEPRECATED. Please obtain the
  // parameter count from the Code/BytecodeArray or another trusted source
  // instead. See also crbug.com/40931165.
  // TODO(saelo): mark as V8_DEPRECATE_SOON once the remaining users are fixed.
  inline void set_internal_formal_parameter_count(int value);
  inline uint16_t internal_formal_parameter_count_with_receiver() const;
  inline uint16_t internal_formal_parameter_count_without_receiver() const;

 private:
  using TorqueGeneratedSharedFunctionInfo::formal_parameter_count;
  using TorqueGeneratedSharedFunctionInfo::set_formal_parameter_count;

 public:
  // Set the formal parameter count so the function code will be
  // called without using argument adaptor frames.
  inline void DontAdaptArguments();
  inline bool IsDontAdaptArguments() const;

  // Accessors for the data associated with this SFI.
  //
  // Currently it can be one of:
  //  - a FunctionTemplateInfo to make benefit the API [IsApiFunction()].
  //  - a BytecodeArray for the interpreter [HasBytecodeArray()].
  //  - a InterpreterData with the BytecodeArray and a copy of the
  //    interpreter trampoline [HasInterpreterData()]
  //  - an AsmWasmData with Asm->Wasm conversion [HasAsmWasmData()].
  //  - a Smi containing the builtin id [HasBuiltinId()]
  //  - a UncompiledDataWithoutPreparseData for lazy compilation
  //    [HasUncompiledDataWithoutPreparseData()]
  //  - a UncompiledDataWithPreparseData for lazy compilation
  //    [HasUncompiledDataWithPreparseData()]
  //  - a WasmExportedFunctionData for Wasm [HasWasmExportedFunctionData()]
  //  - a WasmJSFunctionData for functions created with WebAssembly.Function
  //  - a WasmCapiFunctionData for Wasm C-API functions
  //  - a WasmResumeData for JSPI Wasm functions
  //
  // If the (expected) type of data is known, prefer to use the specialized
  // accessors (e.g. bytecode_array(), uncompiled_data(), etc.).
  inline Tagged<Object> GetTrustedData(IsolateForSandbox isolate) const;
  inline Tagged<Object> GetUntrustedData() const;

  // Helper function for use when a specific data type is expected.
  template <typename T, IndirectPointerTag tag>
  inline Tagged<T> GetTrustedData(IsolateForSandbox isolate) const;

  // Helper function when no Isolate is available. Prefer to use the variant
  // with an isolate parameter if possible.
  inline Tagged<Object> GetTrustedData() const;

 private:
  // For the sandbox, the function's data is split across two fields, with the
  // "trusted" part containing a trusted pointer and the regular/untrusted part
  // containing a tagged pointer. In that case, code accessing the data field
  // will first load the trusted data field. If that is empty (i.e.
  // kNullIndirectPointerHandle), it will then load the regular field. With
  // that, the only racy transition would be a tagged -> trusted transition
  // (one thread may first read the empty trusted pointer, then another thread
  // transitions to the trusted field, clearing the tagged field, and then the
  // first thread continues to load the tagged field). As such, this transition
  // is only allowed on the main thread. From a GC perspective, both fields
  // always contain a valid value and so can be processed unconditionally.
  // Only one of these two fields should be in use at any time and the other
  // field should be cleared. As such, when setting these fields use
  // SetTrustedData() and SetUntrustedData() which automatically clear the
  // inactive field.
  // TODO(chromium:1490564): try to merge these two fields back together, for
  // example by moving all data objects into trusted space.
  inline void SetTrustedData(Tagged<ExposedTrustedObject> value,
                             WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline void SetUntrustedData(Tagged<Object> value,
                               WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline bool HasTrustedData() const;
  inline bool HasUntrustedData() const;

 public:
  inline bool IsApiFunction() const;
  inline bool is_class_constructor() const;
  DECL_ACCESSORS(api_func_data, Tagged<FunctionTemplateInfo>)
  DECL_GETTER(HasBytecodeArray, bool)
  template <typename IsolateT>
  inline Tagged<BytecodeArray> GetBytecodeArray(IsolateT* isolate) const;

  // Sets the bytecode for this SFI. This is only allowed when this SFI has not
  // yet been compiled or if it has been "uncompiled", or in other words when
  // there is no existing bytecode yet.
  inline void set_bytecode_array(Tagged<BytecodeArray> bytecode);
  // Like set_bytecode_array but allows overwriting existing bytecode.
  inline void overwrite_bytecode_array(Tagged<BytecodeArray> bytecode);

  inline Tagged<Code> InterpreterTrampoline(IsolateForSandbox isolate) const;
  inline bool HasInterpreterData(IsolateForSandbox isolate) const;
  inline Tagged<InterpreterData> interpreter_data(
      IsolateForSandbox isolate) const;
  inline void set_interpreter_data(
      Tagged<InterpreterData> interpreter_data,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  DECL_GETTER(HasBaselineCode, bool)
  DECL_RELEASE_ACQUIRE_ACCESSORS(baseline_code, Tagged<Code>)
  inline void FlushBaselineCode();
  inline Tagged<BytecodeArray> GetActiveBytecodeArray(
      IsolateForSandbox isolate) const;
  inline void SetActiveBytecodeArray(Tagged<BytecodeArray> bytecode,
                                     IsolateForSandbox isolate);

#if V8_ENABLE_WEBASSEMBLY
  inline bool HasAsmWasmData() const;
  inline bool HasWasmFunctionData() const;
  inline bool HasWasmExportedFunctionData() const;
  inline bool HasWasmJSFunctionData() const;
  inline bool HasWasmCapiFunctionData() const;
  inline bool HasWasmResumeData() const;
  DECL_ACCESSORS(asm_wasm_data, Tagged<AsmWasmData>)

  DECL_GETTER(wasm_function_data, Tagged<WasmFunctionData>)
  DECL_GETTER(wasm_exported_function_data, Tagged<WasmExportedFunctionData>)
  DECL_GETTER(wasm_js_function_data, Tagged<WasmJSFunctionData>)
  DECL_GETTER(wasm_capi_function_data, Tagged<WasmCapiFunctionData>)
  DECL_GETTER(wasm_resume_data, Tagged<WasmResumeData>)

  inline const wasm::WasmModule* wasm_module() const;
  inline const wasm::FunctionSig* wasm_function_signature() const;
  inline int wasm_function_index() const;
#endif  // V8_ENABLE_WEBASSEMBLY

  // builtin corresponds to the auto-generated Builtin enum.
  inline bool HasBuiltinId() const;
  DECL_PRIMITIVE_ACCESSORS(builtin_id, Builtin)

  inline bool HasUncompiledData() const;
  inline Tagged<UncompiledData> uncompiled_data(
      IsolateForSandbox isolate) const;
  inline void set_uncompiled_data(Tagged<UncompiledData> data,
                                  WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline bool HasUncompiledDataWithPreparseData() const;
  inline Tagged<UncompiledDataWithPreparseData>
  uncompiled_data_with_preparse_data(IsolateForSandbox isolate) const;
  inline void set_uncompiled_data_with_preparse_data(
      Tagged<UncompiledDataWithPreparseData> data,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline bool HasUncompiledDataWithoutPreparseData() const;
  inline void ClearUncompiledDataJobPointer(IsolateForSandbox isolate);

  // Clear out pre-parsed scope data from UncompiledDataWithPreparseData,
  // turning it into UncompiledDataWithoutPreparseData.
  inline void ClearPreparseData(IsolateForSandbox isolate);

  // The inferred_name is inferred from variable or property assignment of this
  // function. It is used to facilitate debugging and profiling of JavaScript
  // code written in OO style, where almost all functions are anonymous but are
  // assigned to object properties.
  inline bool HasInferredName();
  DECL_GETTER(inferred_name, Tagged<String>)

  // All DebugInfo accessors forward to the Debug object which stores DebugInfo
  // objects in a sidetable.
  bool HasDebugInfo(Isolate* isolate) const;
  V8_EXPORT_PRIVATE Tagged<DebugInfo> GetDebugInfo(Isolate* isolate) const;
  V8_EXPORT_PRIVATE std::optional<Tagged<DebugInfo>> TryGetDebugInfo(
      Isolate* isolate) const;
  V8_EXPORT_PRIVATE bool HasBreakInfo(Isolate* isolate) const;
  bool BreakAtEntry(Isolate* isolate) const;
  bool HasCoverageInfo(Isolate* isolate) const;
  Tagged<CoverageInfo> GetCoverageInfo(Isolate* isolate) const;

  // The function's name if it is non-empty, otherwise the inferred name.
  std::unique_ptr<char[]> DebugNameCStr() const;
  static Handle<String> DebugName(Isolate* isolate,
                                  DirectHandle<SharedFunctionInfo> shared);

  // Used for flags such as --turbo-filter.
  bool PassesFilter(const char* raw_filter);

  // [script]: the Script from which the function originates, or undefined.
  DECL_RELEASE_ACQUIRE_ACCESSORS(script, Tagged<HeapObject>)
  // Use `raw_script` if deserialization of this SharedFunctionInfo may still
  // be in progress and thus the `script` field still equal to
  // Smi::uninitialized_deserialization_value.
  DECL_RELEASE_ACQUIRE_ACCESSORS(raw_script, Tagged<Object>)
  // TODO(jgruber): Remove these overloads and pass the kAcquireLoad tag
  // explicitly.
  inline Tagged<HeapObject> script() const;
  inline Tagged<HeapObject> script(PtrComprCageBase cage_base) const;
  inline bool has_script(AcquireLoadTag tag) const;

  // True if the underlying script was parsed and compiled in REPL mode.
  inline bool is_repl_mode() const;

  // The offset of the 'function' token in the script source relative to the
  // start position. Can return kFunctionTokenOutOfRange if offset doesn't
  // fit in 16 bits.
  DECL_UINT16_ACCESSORS(raw_function_token_offset)
 private:
  using TorqueGeneratedSharedFunctionInfo::function_token_offset;
  using TorqueGeneratedSharedFunctionInfo::set_function_token_offset;

 public:
  // The position of the 'function' token in the script source. Can return
  // kNoSourcePosition if raw_function_token_offset() returns
  // kFunctionTokenOutOfRange.
  inline int function_token_position() const;

  // Returns true if the function has shared name.
  inline bool HasSharedName() const;

  // [flags] Bit field containing various flags about the function.
  DECL_RELAXED_INT32_ACCESSORS(flags)
  DECL_UINT8_ACCESSORS(flags2)

  DECL_UINT16_ACCESSORS(age)

  // True if the outer class scope contains a private brand for
  // private instance methods.
  DECL_BOOLEAN_ACCESSORS(class_scope_has_private_brand)
  DECL_BOOLEAN_ACCESSORS(has_static_private_methods_or_accessors)

  DECL_BOOLEAN_ACCESSORS(is_sparkplug_compiling)
  DECL_BOOLEAN_ACCESSORS(maglev_compilation_failed)

  DECL_BOOLEAN_ACCESSORS(sparkplug_compiled)

  CachedTieringDecision cached_tiering_decision();
  void set_cached_tiering_decision(CachedTieringDecision decision);

  DECL_BOOLEAN_ACCESSORS(function_context_independent_compiled)

  // Is this function a top-level function (scripts, evals).
  DECL_BOOLEAN_ACCESSORS(is_toplevel)

  // Indicates if this function can be lazy compiled.
  DECL_BOOLEAN_ACCESSORS(allows_lazy_compilation)

  // Indicates the language mode.
  inline LanguageMode language_mode() const;
  inline void set_language_mode(LanguageMode language_mode);

  // How the function appears in source text.
  DECL_PRIMITIVE_ACCESSORS(syntax_kind, FunctionSyntaxKind)

  // Indicates whether the source is implicitly wrapped in a function.
  inline bool is_wrapped() const;

  // True if the function has any duplicated parameter names.
  DECL_BOOLEAN_ACCESSORS(has_duplicate_parameters)

  // Indicates whether the function is a native function.
  // These needs special treatment in .call and .apply since
  // null passed as the receiver should not be translated to the
  // global object.
  DECL_BOOLEAN_ACCESSORS(native)

#if V8_ENABLE_WEBASSEMBLY
  // Indicates that asm->wasm conversion failed and should not be re-attempted.
  DECL_BOOLEAN_ACCESSORS(is_asm_wasm_broken)
#endif  // V8_ENABLE_WEBASSEMBLY

  // Indicates that the function was created by the Function function.
  // Though it's anonymous, toString should treat it as if it had the name
  // "anonymous".  We don't set the name itself so that the system does not
  // see a binding for it.
  DECL_BOOLEAN_ACCESSORS(name_should_print_as_anonymous)

  // Whether or not the number of expected properties may change.
  DECL_BOOLEAN_ACCESSORS(are_properties_final)

  // Indicates that the function has been reported for binary code coverage.
  DECL_BOOLEAN_ACCESSORS(has_reported_binary_coverage)

  // Indicates that the private name lookups inside the function skips the
  // closest outer class scope.
  DECL_BOOLEAN_ACCESSORS(private_name_lookup_skips_outer_class)

  inline FunctionKind kind() const;

  int UniqueIdInScript() const;

  // Defines the index in a native context of closure's map instantiated using
  // this shared function info.
  DECL_INT_ACCESSORS(function_map_index)

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic.
  inline void clear_padding();

  // Recalculates the |map_index| value after modifications of this shared info.
  inline void UpdateFunctionMapIndex();

  // Indicates whether optimizations have been disabled for this shared function
  // info. If we cannot optimize the function we disable optimization to avoid
  // spending time attempting to optimize it again.
  inline bool optimization_disabled() const;

  // The reason why optimization was disabled.
  inline BailoutReason disabled_optimization_reason() const;

  // Disable (further) attempted optimization of all functions sharing this
  // shared function info.
  void DisableOptimization(Isolate* isolate, BailoutReason reason);

  // This class constructor needs to call out to an instance fields
  // initializer. This flag is set when creating the
  // SharedFunctionInfo as a reminder to emit the initializer call
  // when generating code later.
  DECL_BOOLEAN_ACCESSORS(requires_instance_members_initializer)

  // [source code]: Source code for the function.
  bool HasSourceCode() const;
  static Handle<Object> GetSourceCode(Isolate* isolate,
                                      DirectHandle<SharedFunctionInfo> shared);
  static Handle<Object> GetSourceCodeHarmony(
      Isolate* isolate, DirectHandle<SharedFunctionInfo> shared);

  // Tells whether this function should be subject to debugging, e.g. for
  // - scope inspection
  // - internal break points
  // - coverage and type profile
  // - error stack trace
  inline bool IsSubjectToDebugging() const;

  // Whether this function is defined in user-provided JavaScript code.
  inline bool IsUserJavaScript() const;

  // True if one can flush compiled code from this function, in such a way that
  // it can later be re-compiled.
  inline bool CanDiscardCompiled() const;

  // Flush compiled data from this function, setting it back to CompileLazy and
  // clearing any compiled metadata.
  V8_EXPORT_PRIVATE static void DiscardCompiled(
      Isolate* isolate, DirectHandle<SharedFunctionInfo> shared_info);

  // Discard the compiled metadata. If called during GC then
  // |gc_notify_updated_slot| should be used to record any slot updates.
  void DiscardCompiledMetadata(
      Isolate* isolate,
      std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                         Tagged<HeapObject> target)>
          gc_notify_updated_slot = [](Tagged<HeapObject> object,
                                      ObjectSlot slot,
                                      Tagged<HeapObject> target) {});

  // Returns true if the function has old bytecode that could be flushed. This
  // function shouldn't access any flags as it is used by concurrent marker.
  // Hence it takes the mode as an argument.
  inline bool ShouldFlushCode(base::EnumSet<CodeFlushMode> code_flush_mode);

  enum Inlineability {
    // Different reasons for not being inlineable:
    kHasNoScript,
    kNeedsBinaryCoverage,
    kIsBuiltin,
    kIsNotUserCode,
    kHasNoBytecode,
    kExceedsBytecodeLimit,
    kMayContainBreakPoints,
    kHasOptimizationDisabled,
    // Actually inlineable!
    kIsInlineable,
  };
  // Returns the first value that applies (see enum definition for the order).
  template <typename IsolateT>
  Inlineability GetInlineability(IsolateT* isolate) const;

  // Source size of this function.
  int SourceSize();

  // Returns `false` if formal parameters include rest parameters, optional
  // parameters, or destructuring parameters.
  // TODO(caitp): make this a flag set during parsing
  inline bool has_simple_parameters();

  // Initialize a SharedFunctionInfo from a parsed or preparsed function
  // literal.
  template <typename IsolateT>
  static void InitFromFunctionLiteral(IsolateT* isolate,
                                      FunctionLiteral* lit, bool is_toplevel);

  template <typename IsolateT>
  static void CreateAndSetUncompiledData(IsolateT* isolate,
                                         FunctionLiteral* lit);

  // Updates the expected number of properties based on estimate from parser.
  void UpdateExpectedNofPropertiesFromEstimate(FunctionLiteral* literal);
  void UpdateAndFinalizeExpectedNofPropertiesFromEstimate(
      FunctionLiteral* literal);

  // Sets the FunctionTokenOffset field based on the given token position and
  // start position.
  void SetFunctionTokenPosition(int function_token_position,
                                int start_position);

  static void EnsureBytecodeArrayAvailable(
      Isolate* isolate, Handle<SharedFunctionInfo> shared_info,
      IsCompiledScope* is_compiled_scope,
      CreateSourcePositions flag = CreateSourcePositions::kNo);

  inline bool CanCollectSourcePosition(Isolate* isolate);
  static void EnsureSourcePositionsAvailable(
      Isolate* isolate, Handle<SharedFunctionInfo> shared_info);

  template <typename IsolateT>
  bool AreSourcePositionsAvailable(IsolateT* isolate) const;

  // Hash based on function literal id and script id.
  V8_EXPORT_PRIVATE uint32_t Hash();

  inline bool construct_as_builtin() const;

  // Determines and sets the ConstructAsBuiltinBit in |flags|, based on the
  // |function_data|. Must be called when creating the SFI after other fields
  // are initialized. The ConstructAsBuiltinBit determines whether
  // JSBuiltinsConstructStub or JSConstructStubGeneric should be called to
  // construct this function.
  inline void CalculateConstructAsBuiltin();

  // Replaces the current age with a new value if the current value matches the
  // one expected. Returns the value before this operation.
  inline uint16_t CompareExchangeAge(uint16_t expected_age, uint16_t new_age);

  // Bytecode aging
  V8_EXPORT_PRIVATE static void EnsureOldForTesting(
      Tagged<SharedFunctionInfo> sfu);

  // Dispatched behavior.
  DECL_PRINTER(SharedFunctionInfo)
  DECL_VERIFIER(SharedFunctionInfo)
#ifdef VERIFY_HEAP
  void SharedFunctionInfoVerify(LocalIsolate* isolate);
#endif
#ifdef OBJECT_PRINT
  void PrintSourceCode(std::ostream& os);
#endif

  // Iterate over all shared function infos in a given script.
  class ScriptIterator {
   public:
    V8_EXPORT_PRIVATE ScriptIterator(Isolate* isolate, Tagged<Script> script);
    explicit ScriptIterator(Handle<WeakFixedArray> infos);
    ScriptIterator(const ScriptIterator&) = delete;
    ScriptIterator& operator=(const ScriptIterator&) = delete;
    V8_EXPORT_PRIVATE Tagged<SharedFunctionInfo> Next();
    int CurrentIndex() const { return index_ - 1; }

    // Reset the iterator to run on |script|.
    void Reset(Isolate* isolate, Tagged<Script> script);

   private:
    Handle<WeakFixedArray> infos_;
    int index_;
  };

  // Constants.
  static const int kMaximumFunctionTokenOffset = kMaxUInt16 - 1;
  static const uint16_t kFunctionTokenOutOfRange = static_cast<uint16_t>(-1);
  static_assert(kMaximumFunctionTokenOffset + 1 == kFunctionTokenOutOfRange);

  static_assert(kSize % kTaggedSize == 0);

  class BodyDescriptor;

  // Bailout reasons must fit in the DisabledOptimizationReason bitfield.
  static_assert(BailoutReason::kLastErrorMessage <=
                DisabledOptimizationReasonBits::kMax);

  static_assert(FunctionKind::kLastFunctionKind <= FunctionKindBits::kMax);
  static_assert(FunctionSyntaxKind::kLastFunctionSyntaxKind <=
                FunctionSyntaxKindBits::kMax);

  // Sets the bytecode in {shared}'s DebugInfo as the bytecode to
  // be returned by following calls to GetActiveBytecodeArray. Stores a
  // reference to the original bytecode in the DebugInfo.
  static void InstallDebugBytecode(DirectHandle<SharedFunctionInfo> shared,
                                   Isolate* isolate);
  // Removes the debug bytecode and restores the original bytecode to be
  // returned by following calls to GetActiveBytecodeArray.
  static void UninstallDebugBytecode(Tagged<SharedFunctionInfo> shared,
                                     Isolate* isolate);

#ifdef DEBUG
  // Verifies that all SFI::unique_id values on the heap are unique, including
  // Isolate::new_unique_sfi_id_.
  static bool UniqueIdsAreUnique(Isolate* isolate);
#endif  // DEBUG

 private:
#ifdef VERIFY_HEAP
  void SharedFunctionInfoVerify(ReadOnlyRoots roots);
#endif

  // [name_or_scope_info]: Function name string, kNoSharedNameSentinel or
  // ScopeInfo.
  DECL_RELEASE_ACQUIRE_ACCESSORS(name_or_scope_info, Tagged<NameOrScopeInfoT>)

  // [outer scope info] The outer scope info, needed to lazily parse this
  // function.
  DECL_ACCESSORS(outer_scope_info, Tagged<HeapObject>)

  // [properties_are_final]: This bit is used to track if we have finished
  // parsing its properties. The properties final bit is only used by
  // class constructors to handle lazily parsed properties.
  DECL_BOOLEAN_ACCESSORS(properties_are_final)

  inline void set_kind(FunctionKind kind);

  inline uint16_t get_property_estimate_from_literal(FunctionLiteral* literal);

  // For ease of use of the BITFIELD macro.
  inline int32_t relaxed_flags() const;
  inline void set_relaxed_flags(int32_t flags);

  template <typename Impl>
  friend class FactoryBase;
  friend class V8HeapExplorer;
  FRIEND_TEST(PreParserTest, LazyFunctionLength);

  TQ_OBJECT_CONSTRUCTORS(SharedFunctionInfo)
};

// A SharedFunctionInfoWrapper wraps a SharedFunctionInfo from trusted space.
// It can be useful when a protected pointer reference to a SharedFunctionInfo
// is needed, for example for a ProtectedFixedArray.
class SharedFunctionInfoWrapper : public TrustedObject {
 public:
  DECL_ACCESSORS(shared_info, Tagged<SharedFunctionInfo>)

  DECL_PRINTER(SharedFunctionInfoWrapper)
  DECL_VERIFIER(SharedFunctionInfoWrapper)

#define FIELD_LIST(V)               \
  V(kSharedInfoOffset, kTaggedSize) \
  V(kHeaderSize, 0)                 \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(TrustedObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(SharedFunctionInfoWrapper, TrustedObject);
};

static constexpr int kStaticRootsSFISize = 48;
#ifdef V8_STATIC_ROOTS
static_assert(SharedFunctionInfo::kSize == kStaticRootsSFISize);
#endif  // V8_STATIC_ROOTS

// Printing support.
struct SourceCodeOf {
  explicit SourceCodeOf(Tagged<SharedFunctionInfo> v, int max = -1)
      : value(v), max_length(max) {}
  const Tagged<SharedFunctionInfo> value;
  int max_length;
};

// IsCompiledScope enables a caller to check if a function is compiled, and
// ensure it remains compiled (i.e., doesn't have it's bytecode flushed) while
// the scope is retained.
class V8_NODISCARD IsCompiledScope {
 public:
  inline IsCompiledScope(const Tagged<SharedFunctionInfo> shared,
                         Isolate* isolate);
  inline IsCompiledScope(const Tagged<SharedFunctionInfo> shared,
                         LocalIsolate* isolate);
  inline IsCompiledScope() : retain_code_(), is_compiled_(false) {}

  inline bool is_compiled() const { return is_compiled_; }

 private:
  MaybeHandle<HeapObject> retain_code_;
  bool is_compiled_;
};

std::ostream& operator<<(std::ostream& os, const SourceCodeOf& v);

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SHARED_FUNCTION_INFO_H_
