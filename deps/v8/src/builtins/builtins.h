// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_H_
#define V8_BUILTINS_BUILTINS_H_

#include "src/base/flags.h"
#include "src/builtins/builtins-definitions.h"
#include "src/common/globals.h"
#include "src/objects/type-hints.h"
#include "src/sandbox/code-entrypoint-tag.h"

namespace v8 {
namespace internal {

class ByteArray;
class CallInterfaceDescriptor;
class Callable;

// Forward declarations.
class BytecodeOffset;
class RootVisitor;
enum class InterpreterPushArgsMode : unsigned;
class Zone;
namespace compiler {
class CodeAssemblerState;
namespace turboshaft {
class Graph;
class PipelineData;
}  // namespace turboshaft
}  // namespace compiler

template <typename T>
static constexpr T FirstFromVarArgs(T x, ...) noexcept {
  return x;
}

// Convenience macro to avoid generating named accessors for all builtins.
#define BUILTIN_CODE(isolate, name) \
  (isolate)->builtins()->code_handle(i::Builtin::k##name)

enum class Builtin : int32_t {
  kNoBuiltinId = -1,
#define DEF_ENUM(Name, ...) k##Name,
  BUILTIN_LIST(DEF_ENUM, DEF_ENUM, DEF_ENUM, DEF_ENUM, DEF_ENUM, DEF_ENUM,
               DEF_ENUM, DEF_ENUM, DEF_ENUM)
#undef DEF_ENUM
#define EXTRACT_NAME(Name, ...) k##Name,
  // Define kFirstBytecodeHandler,
  kFirstBytecodeHandler =
      FirstFromVarArgs(BUILTIN_LIST_BYTECODE_HANDLERS(EXTRACT_NAME) 0)
#undef EXTRACT_NAME
};

V8_INLINE constexpr bool operator<(Builtin a, Builtin b) {
  using type = typename std::underlying_type<Builtin>::type;
  return static_cast<type>(a) < static_cast<type>(b);
}

V8_INLINE Builtin operator++(Builtin& builtin) {
  using type = typename std::underlying_type<Builtin>::type;
  return builtin = static_cast<Builtin>(static_cast<type>(builtin) + 1);
}

class Builtins {
 public:
  explicit Builtins(Isolate* isolate) : isolate_(isolate) {}

  Builtins(const Builtins&) = delete;
  Builtins& operator=(const Builtins&) = delete;

  void TearDown();

  // Disassembler support.
  const char* Lookup(Address pc);

#if !defined(V8_SHORT_BUILTIN_CALLS) || \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
  static constexpr bool kCodeObjectsAreInROSpace = true;
#else
  static constexpr bool kCodeObjectsAreInROSpace = false;
#endif  // !defined(V8_SHORT_BUILTIN_CALLS) || \
        // defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)

#define ADD_ONE(Name, ...) +1
  static constexpr int kBuiltinCount =
      0 BUILTIN_LIST(ADD_ONE, ADD_ONE, ADD_ONE, ADD_ONE, ADD_ONE, ADD_ONE,
                     ADD_ONE, ADD_ONE, ADD_ONE);
  static constexpr int kBuiltinTier0Count = 0 BUILTIN_LIST_TIER0(
      ADD_ONE, ADD_ONE, ADD_ONE, ADD_ONE, ADD_ONE, ADD_ONE, ADD_ONE);
#undef ADD_ONE

  static constexpr Builtin kFirst = static_cast<Builtin>(0);
  static constexpr Builtin kLast = static_cast<Builtin>(kBuiltinCount - 1);
  static constexpr Builtin kLastTier0 =
      static_cast<Builtin>(kBuiltinTier0Count - 1);

  static constexpr int kFirstWideBytecodeHandler =
      static_cast<int>(Builtin::kFirstBytecodeHandler) +
      kNumberOfBytecodeHandlers;
  static constexpr int kFirstExtraWideBytecodeHandler =
      kFirstWideBytecodeHandler + kNumberOfWideBytecodeHandlers;
  static constexpr int kLastBytecodeHandlerPlusOne =
      kFirstExtraWideBytecodeHandler + kNumberOfWideBytecodeHandlers;
  static constexpr bool kBytecodeHandlersAreSortedLast =
      kLastBytecodeHandlerPlusOne == kBuiltinCount;
  static_assert(kBytecodeHandlersAreSortedLast);

  static constexpr bool IsBuiltinId(Builtin builtin) {
    return builtin != Builtin::kNoBuiltinId;
  }
  static constexpr bool IsBuiltinId(int maybe_id) {
    static_assert(static_cast<int>(Builtin::kNoBuiltinId) == -1);
    return static_cast<uint32_t>(maybe_id) <
           static_cast<uint32_t>(kBuiltinCount);
  }
  static constexpr bool IsTier0(Builtin builtin) {
    return builtin <= kLastTier0 && IsBuiltinId(builtin);
  }

  static constexpr Builtin FromInt(int id) {
    DCHECK(IsBuiltinId(id));
    return static_cast<Builtin>(id);
  }
  static constexpr int ToInt(Builtin id) {
    DCHECK(IsBuiltinId(id));
    return static_cast<int>(id);
  }

  // The different builtin kinds are documented in builtins-definitions.h.
  enum Kind { CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, BCH, ASM };

  static BytecodeOffset GetContinuationBytecodeOffset(Builtin builtin);
  static Builtin GetBuiltinFromBytecodeOffset(BytecodeOffset);

  //
  // Convenience wrappers.
  //
  static inline constexpr Builtin RecordWrite(SaveFPRegsMode fp_mode);
  static inline constexpr Builtin IndirectPointerBarrier(
      SaveFPRegsMode fp_mode);
  static inline constexpr Builtin EphemeronKeyBarrier(SaveFPRegsMode fp_mode);

  static inline constexpr Builtin CallFunction(
      ConvertReceiverMode = ConvertReceiverMode::kAny);
  static inline constexpr Builtin Call(
      ConvertReceiverMode = ConvertReceiverMode::kAny);

  static inline constexpr Builtin NonPrimitiveToPrimitive(
      ToPrimitiveHint hint = ToPrimitiveHint::kDefault);
  static inline constexpr Builtin OrdinaryToPrimitive(
      OrdinaryToPrimitiveHint hint);

  static inline constexpr Builtin StringAdd(
      StringAddFlags flags = STRING_ADD_CHECK_NONE);

  static inline constexpr Builtin LoadGlobalIC(TypeofMode typeof_mode);
  static inline constexpr Builtin LoadGlobalICInOptimizedCode(
      TypeofMode typeof_mode);

  static inline constexpr Builtin CEntry(int result_size, ArgvMode argv_mode,
                                         bool builtin_exit_frame = false,
                                         bool switch_to_central_stack = false);

  static inline constexpr Builtin RuntimeCEntry(
      int result_size, bool switch_to_central_stack = false);

  static inline constexpr Builtin InterpreterCEntry(int result_size);
  static inline constexpr Builtin InterpreterPushArgsThenCall(
      ConvertReceiverMode receiver_mode, InterpreterPushArgsMode mode);
  static inline constexpr Builtin InterpreterPushArgsThenConstruct(
      InterpreterPushArgsMode mode);

  // Used by CreateOffHeapTrampolines in isolate.cc.
  void set_code(Builtin builtin, Tagged<Code> code);

  V8_EXPORT_PRIVATE Tagged<Code> code(Builtin builtin);
  V8_EXPORT_PRIVATE Handle<Code> code_handle(Builtin builtin);

  static CallInterfaceDescriptor CallInterfaceDescriptorFor(Builtin builtin);
  V8_EXPORT_PRIVATE static Callable CallableFor(Isolate* isolate,
                                                Builtin builtin);
  static bool HasJSLinkage(Builtin builtin);

  V8_EXPORT_PRIVATE static int GetStackParameterCount(Builtin builtin);

  V8_EXPORT_PRIVATE static const char* name(Builtin builtin);
  V8_EXPORT_PRIVATE static const char* NameForStackTrace(Isolate* isolate,
                                                         Builtin builtin);

  // Support for --print-builtin-size and --print-builtin-code.
  void PrintBuiltinCode();
  void PrintBuiltinSize();

  // Returns the C++ entry point for builtins implemented in C++, and the null
  // Address otherwise.
  static Address CppEntryOf(Builtin builtin);

  // Loads the builtin's entry (start of instruction stream) from the isolate's
  // builtin_entry_table, initialized earlier via {InitializeIsolateDataTables}.
  static inline Address EntryOf(Builtin builtin, Isolate* isolate);

  V8_EXPORT_PRIVATE static Kind KindOf(Builtin builtin);
  static const char* KindNameOf(Builtin builtin);

  // The tag for the builtins entrypoint.
  V8_EXPORT_PRIVATE static CodeEntrypointTag EntrypointTagFor(Builtin builtin);

  static bool IsCpp(Builtin builtin);

  // True, iff the given code object is a builtin. Note that this does not
  // necessarily mean that its kind is InstructionStream::BUILTIN.
  static bool IsBuiltin(const Tagged<Code> code);

  // As above, but safe to access off the main thread since the check is done
  // by handle location. Similar to Heap::IsRootHandle.
  bool IsBuiltinHandle(Handle<HeapObject> maybe_code, Builtin* index) const;

  // True, iff the given builtin contains no isolate-specific code and can be
  // embedded into the binary.
  static constexpr bool kAllBuiltinsAreIsolateIndependent = true;
  static constexpr bool AllBuiltinsAreIsolateIndependent() {
    return kAllBuiltinsAreIsolateIndependent;
  }
  static constexpr bool IsIsolateIndependent(Builtin builtin) {
    static_assert(kAllBuiltinsAreIsolateIndependent);
    return kAllBuiltinsAreIsolateIndependent;
  }

  // True, iff the given code object is a builtin with off-heap embedded code.
  static bool IsIsolateIndependentBuiltin(Tagged<Code> code);

  static void InitializeIsolateDataTables(Isolate* isolate);

  // Emits a CodeCreateEvent for every builtin.
  static void EmitCodeCreateEvents(Isolate* isolate);

  bool is_initialized() const { return initialized_; }

  // Used by SetupIsolateDelegate and Deserializer.
  void MarkInitialized() {
    DCHECK(!initialized_);
    initialized_ = true;
  }

  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> InvokeApiFunction(
      Isolate* isolate, bool is_construct,
      Handle<FunctionTemplateInfo> function, Handle<Object> receiver, int argc,
      Handle<Object> args[], Handle<HeapObject> new_target);

  static void Generate_Adaptor(MacroAssembler* masm, Address builtin_address);

  static void Generate_CEntry(MacroAssembler* masm, int result_size,
                              ArgvMode argv_mode, bool builtin_exit_frame,
                              bool switch_to_central_stack);

  static bool AllowDynamicFunction(Isolate* isolate,
                                   DirectHandle<JSFunction> target,
                                   Handle<JSObject> target_global_proxy);

  // Creates a copy of InterpreterEntryTrampolineForProfiling in the code space.
  static Handle<Code> CreateInterpreterEntryTrampolineForProfiling(
      Isolate* isolate);

  static inline constexpr bool IsJSEntryVariant(Builtin builtin);

  int js_entry_handler_offset() const {
    DCHECK_NE(js_entry_handler_offset_, 0);
    return js_entry_handler_offset_;
  }

  int jspi_prompt_handler_offset() const {
    DCHECK_NE(jspi_prompt_handler_offset_, 0);
    return jspi_prompt_handler_offset_;
  }

  void SetJSEntryHandlerOffset(int offset) {
    // Check the stored offset is either uninitialized or unchanged (we
    // generate multiple variants of this builtin but they should all have the
    // same handler offset).
    CHECK(js_entry_handler_offset_ == 0 || js_entry_handler_offset_ == offset);
    js_entry_handler_offset_ = offset;
  }

  void SetJSPIPromptHandlerOffset(int offset) {
    CHECK_EQ(jspi_prompt_handler_offset_, 0);
    jspi_prompt_handler_offset_ = offset;
  }

#if V8_ENABLE_DRUMBRAKE
  int cwasm_interpreter_entry_handler_offset() const {
    DCHECK_NE(cwasm_interpreter_entry_handler_offset_, 0);
    return cwasm_interpreter_entry_handler_offset_;
  }

  void SetCWasmInterpreterEntryHandlerOffset(int offset) {
    // Check the stored offset is either uninitialized or unchanged (we
    // generate multiple variants of this builtin but they should all have the
    // same handler offset).
    CHECK(cwasm_interpreter_entry_handler_offset_ == 0 ||
          cwasm_interpreter_entry_handler_offset_ == offset);
    cwasm_interpreter_entry_handler_offset_ = offset;
  }
#endif  // V8_ENABLE_DRUMBRAKE

  // Returns given builtin's slot in the main builtin table.
  FullObjectSlot builtin_slot(Builtin builtin);
  // Returns given builtin's slot in the tier0 builtin table.
  FullObjectSlot builtin_tier0_slot(Builtin builtin);

  // Public for ia32-specific helper.
  enum class ForwardWhichFrame { kCurrentFrame, kParentFrame };

 private:
  static void Generate_CallFunction(MacroAssembler* masm,
                                    ConvertReceiverMode mode);

  static void Generate_CallBoundFunctionImpl(MacroAssembler* masm);

  static void Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode);

  static void Generate_CallOrConstructVarargs(MacroAssembler* masm,
                                              Builtin target_builtin);
  enum class CallOrConstructMode { kCall, kConstruct };
  static void Generate_CallOrConstructForwardVarargs(MacroAssembler* masm,
                                                     CallOrConstructMode mode,
                                                     Builtin target_builtin);

  static void Generate_MaglevFunctionEntryStackCheck(MacroAssembler* masm,
                                                     bool save_new_target);

  enum class InterpreterEntryTrampolineMode {
    // The version of InterpreterEntryTrampoline used by default.
    kDefault,
    // The position independent version of InterpreterEntryTrampoline used as
    // a template to create copies of the builtin at runtime. The copies are
    // used to create better profiling information for ticks in bytecode
    // execution. See v8_flags.interpreted_frames_native_stack for details.
    kForProfiling
  };
  static void Generate_InterpreterEntryTrampoline(
      MacroAssembler* masm, InterpreterEntryTrampolineMode mode);

  static void Generate_InterpreterPushArgsThenCallImpl(
      MacroAssembler* masm, ConvertReceiverMode receiver_mode,
      InterpreterPushArgsMode mode);

  static void Generate_InterpreterPushArgsThenConstructImpl(
      MacroAssembler* masm, InterpreterPushArgsMode mode);

  static void Generate_ConstructForwardAllArgsImpl(
      MacroAssembler* masm, ForwardWhichFrame which_frame);

  static void Generate_CallApiCallbackImpl(MacroAssembler* masm,
                                           CallApiCallbackMode mode);

#define DECLARE_ASM(Name, ...) \
  static void Generate_##Name(MacroAssembler* masm);
#define DECLARE_TF(Name, ...) \
  static void Generate_##Name(compiler::CodeAssemblerState* state);
#define DECLARE_TS(Name, ...)                                           \
  static void Generate_##Name(compiler::turboshaft::PipelineData* data, \
                              Isolate* isolate,                         \
                              compiler::turboshaft::Graph& graph, Zone* zone);

  BUILTIN_LIST(IGNORE_BUILTIN, DECLARE_TS, DECLARE_TF, DECLARE_TS, DECLARE_TF,
               DECLARE_TF, DECLARE_TF, IGNORE_BUILTIN, DECLARE_ASM)

#undef DECLARE_ASM
#undef DECLARE_TF

  Isolate* isolate_;
  bool initialized_ = false;

  // Stores the offset of exception handler entry point (the handler_entry
  // label) in JSEntry and its variants. It's used to generate the handler table
  // during codegen (mksnapshot-only).
  int js_entry_handler_offset_ = 0;

#if V8_ENABLE_DRUMBRAKE
  // Stores the offset of exception handler entry point (the handler_entry
  // label) in CWasmInterpreterEntry. It's used to generate the handler table
  // during codegen (mksnapshot-only).
  int cwasm_interpreter_entry_handler_offset_ = 0;
#endif  // V8_ENABLE_DRUMBRAKE

  // Do the same for the JSPI prompt, which catches uncaught exceptions and
  // rejects the corresponding promise.
  int jspi_prompt_handler_offset_ = 0;

  friend class SetupIsolateDelegate;
};

V8_INLINE constexpr bool IsInterpreterTrampolineBuiltin(Builtin builtin_id) {
  // Check for kNoBuiltinId first to abort early when the current
  // InstructionStream object is not a builtin.
  return builtin_id != Builtin::kNoBuiltinId &&
         (builtin_id == Builtin::kInterpreterEntryTrampoline ||
          builtin_id == Builtin::kInterpreterEnterAtBytecode ||
          builtin_id == Builtin::kInterpreterEnterAtNextBytecode);
}

V8_INLINE constexpr bool IsBaselineTrampolineBuiltin(Builtin builtin_id) {
  // Check for kNoBuiltinId first to abort early when the current
  // InstructionStream object is not a builtin.
  return builtin_id != Builtin::kNoBuiltinId &&
         (builtin_id == Builtin::kBaselineOutOfLinePrologue ||
          builtin_id == Builtin::kBaselineOutOfLinePrologueDeopt ||
          builtin_id == Builtin::kBaselineOrInterpreterEnterAtBytecode ||
          builtin_id == Builtin::kBaselineOrInterpreterEnterAtNextBytecode);
}

Builtin ExampleBuiltinForTorqueFunctionPointerType(
    size_t function_pointer_type_id);

}  // namespace internal
}  // namespace v8

// Helper while transitioning some functions to libm.
#if defined(V8_USE_LIBM_TRIG_FUNCTIONS)
#define SIN_IMPL(X)                                             \
  v8_flags.use_libm_trig_functions ? base::ieee754::libm_sin(X) \
                                   : base::ieee754::fdlibm_sin(X)
#define COS_IMPL(X)                                             \
  v8_flags.use_libm_trig_functions ? base::ieee754::libm_cos(X) \
                                   : base::ieee754::fdlibm_cos(X)
#else
#define SIN_IMPL(X) base::ieee754::sin(X)
#define COS_IMPL(X) base::ieee754::cos(X)
#endif

#endif  // V8_BUILTINS_BUILTINS_H_
