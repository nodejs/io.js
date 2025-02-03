// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INTERPRETER_WASM_INTERPRETER_RUNTIME_H_
#define V8_WASM_INTERPRETER_WASM_INTERPRETER_RUNTIME_H_

#include <memory>
#include <vector>

#include "src/base/vector.h"
#include "src/execution/simulator.h"
#include "src/wasm/interpreter/wasm-interpreter.h"

namespace v8 {

namespace internal {
class WasmInstanceObject;

namespace wasm {
class InterpreterTracer;
class WasmBytecodeGenerator;
struct WasmTag;

class WasmInterpreterRuntime {
 public:
  WasmInterpreterRuntime(const WasmModule* module, Isolate* isolate,
                         Handle<WasmInstanceObject> instance_object,
                         WasmInterpreter::CodeMap* codemap);
  ~WasmInterpreterRuntime();

  void Reset();

  inline WasmBytecode* GetFunctionBytecode(uint32_t func_index);

  std::vector<WasmInterpreterStackEntry> GetInterpretedStack(
      Address frame_pointer) const;

  int GetFunctionIndex(Address frame_pointer, int index) const;

  void SetTrapFunctionIndex(int32_t func_index);

  inline Isolate* GetIsolate() const { return isolate_; }

  inline uint8_t* GetGlobalAddress(uint32_t index);
  inline Handle<Object> GetGlobalRef(uint32_t index) const;
  inline void SetGlobalRef(uint32_t index, Handle<Object> ref) const;

  int32_t MemoryGrow(uint32_t delta_pages);
  inline uint64_t MemorySize() const;
  inline bool IsMemory64() const;
  inline uint8_t* GetMemoryStart() const { return memory_start_; }
  inline size_t GetMemorySize() const;

  bool MemoryInit(const uint8_t*& current_code, uint32_t data_segment_index,
                  uint64_t dst, uint64_t src, uint64_t size);
  bool MemoryCopy(const uint8_t*& current_code, uint64_t dst, uint64_t src,
                  uint64_t size);
  bool MemoryFill(const uint8_t*& current_code, uint64_t dst, uint32_t value,
                  uint64_t size);

  bool AllowsAtomicsWait() const;
  int32_t AtomicNotify(uint64_t effective_index, int32_t val);
  int32_t I32AtomicWait(uint64_t effective_index, int32_t val, int64_t timeout);
  int32_t I64AtomicWait(uint64_t effective_index, int64_t val, int64_t timeout);

  bool TableGet(const uint8_t*& current_code, uint32_t table_index,
                uint32_t entry_index, Handle<Object>* result);
  void TableSet(const uint8_t*& current_code, uint32_t table_index,
                uint32_t entry_index, Handle<Object> ref);
  void TableInit(const uint8_t*& current_code, uint32_t table_index,
                 uint32_t element_segment_index, uint32_t dst, uint32_t src,
                 uint32_t size);
  void TableCopy(const uint8_t*& current_code, uint32_t dst_table_index,
                 uint32_t src_table_index, uint32_t dst, uint32_t src,
                 uint32_t size);
  uint32_t TableGrow(uint32_t table_index, uint32_t delta,
                     Handle<Object> value);
  uint32_t TableSize(uint32_t table_index);
  void TableFill(const uint8_t*& current_code, uint32_t table_index,
                 uint32_t count, Handle<Object> value, uint32_t start);

  static void UpdateIndirectCallTable(Isolate* isolate,
                                      Handle<WasmInstanceObject> instance,
                                      uint32_t table_index);
  static void ClearIndirectCallCacheEntry(Isolate* isolate,
                                          Handle<WasmInstanceObject> instance,
                                          uint32_t table_index,
                                          uint32_t entry_index);

  static void UpdateMemoryAddress(Handle<WasmInstanceObject> instance);

  inline void DataDrop(uint32_t index);
  inline void ElemDrop(uint32_t index);

  void UnpackException(uint32_t* sp, const WasmTag& tag,
                       Handle<Object> exception_object,
                       uint32_t first_param_slot_index,
                       uint32_t first_param_ref_stack_index);
  void ThrowException(const uint8_t*& code, uint32_t* sp, uint32_t tag_index);
  void RethrowException(const uint8_t*& code, uint32_t* sp,
                        uint32_t catch_block_index);

  void BeginExecution(WasmInterpreterThread* thread, uint32_t function_index,
                      Address frame_pointer, uint8_t* interpreter_fp,
                      uint32_t ref_stack_offset,
                      const std::vector<WasmValue>* argument_values = nullptr);
  void ContinueExecution(WasmInterpreterThread* thread, bool called_from_js);

  void ExecuteImportedFunction(const uint8_t*& code, uint32_t func_index,
                               uint32_t current_stack_size,
                               uint32_t ref_stack_fp_offset,
                               uint32_t slot_offset,
                               uint32_t return_slot_offset);

  void PrepareTailCall(const uint8_t*& code, uint32_t func_index,
                       uint32_t current_stack_size,
                       uint32_t return_slot_offset);

  void ExecuteFunction(const uint8_t*& code, uint32_t function_index,
                       uint32_t current_stack_size,
                       uint32_t ref_stack_fp_offset, uint32_t slot_offset,
                       uint32_t return_slot_offset);

  void ExecuteIndirectCall(const uint8_t*& current_code, uint32_t table_index,
                           uint32_t sig_index, uint32_t entry_index,
                           uint32_t stack_pos, uint32_t* sp,
                           uint32_t ref_stack_fp_offset, uint32_t slot_offset,
                           uint32_t return_slot_offset, bool is_tail_call);

  void ExecuteCallRef(const uint8_t*& current_code, WasmRef func_ref,
                      uint32_t sig_index, uint32_t stack_pos, uint32_t* sp,
                      uint32_t ref_stack_fp_offset, uint32_t slot_offset,
                      uint32_t return_slot_offset, bool is_tail_call);

  const WasmValue& GetReturnValue(size_t index) const {
    DCHECK_LT(index, function_result_.size());
    return function_result_[index];
  }

  inline bool IsRefNull(Handle<Object> ref) const;
  inline Handle<Object> GetFunctionRef(uint32_t index) const;
  void StoreWasmRef(uint32_t ref_stack_index, const WasmRef& ref);
  WasmRef ExtractWasmRef(uint32_t ref_stack_index);
  void UnwindCurrentStackFrame(uint32_t* sp, uint32_t slot_offset,
                               uint32_t rets_size, uint32_t args_size,
                               uint32_t rets_refs, uint32_t args_refs,
                               uint32_t ref_stack_fp_offset);

  void PrintStack(uint32_t* sp, RegMode reg_mode, int64_t r0, double fp0);

  void SetTrap(TrapReason trap_reason, pc_t trap_pc);
  void SetTrap(TrapReason trap_reason, const uint8_t*& current_code);

  // GC helpers.
  Handle<Map> RttCanon(uint32_t type_index) const;
  std::pair<Handle<WasmStruct>, const StructType*> StructNewUninitialized(
      uint32_t index) const;
  std::pair<Handle<WasmArray>, const ArrayType*> ArrayNewUninitialized(
      uint32_t length, uint32_t array_index) const;
  WasmRef WasmArrayNewSegment(uint32_t array_index, uint32_t segment_index,
                              uint32_t offset, uint32_t length);
  bool WasmArrayInitSegment(uint32_t segment_index, WasmRef wasm_array,
                            uint32_t array_offset, uint32_t segment_offset,
                            uint32_t length);
  bool WasmArrayCopy(WasmRef dest_wasm_array, uint32_t dest_index,
                     WasmRef src_wasm_array, uint32_t src_index,
                     uint32_t length);
  WasmRef WasmJSToWasmObject(WasmRef extern_ref, ValueType value_type,
                             uint32_t canonical_index) const;
  WasmRef JSToWasmObject(WasmRef extern_ref, ValueType value_type) const;
  WasmRef WasmToJSObject(WasmRef ref) const;

  inline const ArrayType* GetArrayType(uint32_t array_index) const;
  inline WasmRef GetWasmArrayRefElement(Tagged<WasmArray> array,
                                        uint32_t index) const;
  bool SubtypeCheck(const WasmRef obj, const ValueType obj_type,
                    const Handle<Map> rtt, const ValueType rtt_type,
                    bool null_succeeds) const;
  bool RefIsEq(const WasmRef obj, const ValueType obj_type,
               bool null_succeeds) const;
  bool RefIsI31(const WasmRef obj, const ValueType obj_type,
                bool null_succeeds) const;
  bool RefIsStruct(const WasmRef obj, const ValueType obj_type,
                   bool null_succeeds) const;
  bool RefIsArray(const WasmRef obj, const ValueType obj_type,
                  bool null_succeeds) const;
  bool RefIsString(const WasmRef obj, const ValueType obj_type,
                   bool null_succeeds) const;
  inline bool IsNullTypecheck(const WasmRef obj,
                              const ValueType obj_type) const;
  inline static bool IsNull(Isolate* isolate, const WasmRef obj,
                            const ValueType obj_type);
  inline Tagged<Object> GetNullValue(const ValueType obj_type) const;

  static int memory_start_offset();
  static int instruction_table_offset();

  size_t TotalBytecodeSize() const { return codemap_->TotalBytecodeSize(); }

  void ResetCurrentHandleScope();

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  void Trace(const char* format, ...);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  // Stored in WasmExportedFunctionData::packed_args_size; used by
  // JSToWasmInterpreterWrapper and WasmToJSInterpreterWrapper builtins.
  // Note that the max size of the packed array of args is 16000, which fits
  // into 14 bits (kV8MaxWasmFunctionParams == 1000).
  static_assert(sizeof(Simd128) * kV8MaxWasmFunctionParams < (1 << 14));
  using PackedArgsSizeField = base::BitField<uint32_t, 0, 14>;
  using HasRefArgsField = base::BitField<bool, 14, 1>;
  using HasRefRetsField = base::BitField<bool, 15, 1>;

 private:
  ExternalCallResult CallImportedFunction(const uint8_t*& current_code,
                                          uint32_t function_index, uint32_t* sp,
                                          uint32_t current_stack_size,
                                          uint32_t ref_stack_fp_index,
                                          uint32_t current_slot_offset);
  void PurgeIndirectCallCache(uint32_t table_index);

  ExternalCallResult CallExternalJSFunction(const uint8_t*& current_code,
                                            const WasmModule* module,
                                            Handle<Object> object_ref,
                                            const FunctionSig* sig,
                                            uint32_t* sp,
                                            uint32_t current_stack_slot);

  inline Address EffectiveAddress(uint64_t index) const;

  // Checks if [index, index+size) is in range [0, WasmMemSize), where
  // WasmMemSize is the size of the Memory object associated to
  // {instance_object_}. (Notice that only a single memory is supported).
  // If not in range, {size} is clamped to its valid range.
  // It in range, out_address contains the (virtual memory) address of the
  // {index}th memory location in the Wasm memory.
  inline bool BoundsCheckMemRange(uint64_t index, uint64_t* size,
                                  Address* out_address) const;

  void InitGlobalAddressCache();
  inline void InitMemoryAddresses();
  void InitIndirectFunctionTables();
  bool CheckIndirectCallSignature(uint32_t table_index, uint32_t entry_index,
                                  uint32_t sig_index) const;

  void EnsureRefStackSpace(size_t new_size);
  void ClearRefStackValues(size_t index, size_t count);

  void StoreRefArgsIntoStackSlots(uint8_t* sp, uint32_t ref_stack_fp_index,
                                  const FunctionSig* sig);
  void StoreRefResultsIntoRefStack(uint8_t* sp, uint32_t ref_stack_fp_index,
                                   const FunctionSig* sig);

  WasmInterpreterThread::ExceptionHandlingResult HandleException(
      uint32_t* sp, const uint8_t*& current_code);
  bool MatchingExceptionTag(Handle<Object> exception_object,
                            uint32_t index) const;

  bool SubtypeCheck(Tagged<Map> rtt, Tagged<Map> formal_rtt,
                    uint32_t type_index) const;

  WasmInterpreterThread* thread() const {
    DCHECK_NOT_NULL(current_thread_);
    return current_thread_;
  }
  WasmInterpreterThread::State state() const { return thread()->state(); }

  void CallWasmToJSBuiltin(Isolate* isolate, Handle<Object> object_ref,
                           Address packed_args, const FunctionSig* sig);

  inline Handle<WasmTrustedInstanceData> wasm_trusted_instance_data() const;

  Isolate* isolate_;
  const WasmModule* module_;
  Handle<WasmInstanceObject> instance_object_;
  WasmInterpreter::CodeMap* codemap_;

  uint32_t start_function_index_;
  FrameState current_frame_;
  std::vector<WasmValue> function_result_;

  int trap_function_index_;
  pc_t trap_pc_;

  // References are kept on an on-heap stack. It would not be any good to store
  // reference object pointers into stack slots because the pointers obviously
  // could be invalidated if the object moves in a GC. Furthermore we need to
  // make sure that the reference objects in the Wasm stack are marked as alive
  // for GC. This is why in each Wasm thread we instantiate a FixedArray that
  // contains all the reference objects present in the execution stack.
  // Only while calling JS functions or Wasm functions in a separate instance we
  // need to store temporarily the reference objects pointers into stack slots,
  // and in this case we need to make sure to temporarily disallow GC and avoid
  // object allocation while the reference arguments are being passed to the
  // callee and while the reference return values are being passed back to the
  // caller.
  Handle<FixedArray> reference_stack_;
  size_t current_ref_stack_size_;

  WasmInterpreterThread* current_thread_;

  uint8_t* memory_start_;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif  // __clang__
  PWasmOp* const* instruction_table_;
#ifdef __clang__
#pragma clang diagnostic pop
#endif  // __clang__

  std::vector<uint8_t*> global_addresses_;

  struct IndirectCallValue {
    enum class Mode { kInvalid, kInternalCall, kExternalCall };

    static const uint32_t kInlineSignatureSentinel = UINT_MAX;
    static const uint32_t kInvalidFunctionIndex = UINT_MAX;

    IndirectCallValue()
        : mode(Mode::kInvalid),
          func_index(kInvalidFunctionIndex),
          sig_index(kInlineSignatureSentinel),
          signature(nullptr) {}
    IndirectCallValue(uint32_t func_index_, uint32_t sig_index)
        : mode(Mode::kInternalCall),
          func_index(func_index_),
          sig_index(sig_index),
          signature(nullptr) {}
    IndirectCallValue(const FunctionSig* signature_, uint32_t sig_index)
        : mode(Mode::kExternalCall),
          func_index(kInvalidFunctionIndex),
          sig_index(sig_index),
          signature(signature_) {}

    operator bool() const { return mode != Mode::kInvalid; }

    Mode mode;
    uint32_t func_index;
    uint32_t sig_index;
    const FunctionSig* signature;
  };
  typedef std::vector<IndirectCallValue> IndirectCallTable;
  std::vector<IndirectCallTable> indirect_call_tables_;

  using WasmToJSCallSig =
      // NOLINTNEXTLINE(readability/casting)
      Address(Address js_function, Address packed_args,
              Address saved_c_entry_fp, const FunctionSig* sig,
              Address c_entry_fp, Address callable);
  GeneratedCode<WasmToJSCallSig> generic_wasm_to_js_interpreter_wrapper_fn_;

 public:
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  void TracePop() { shadow_stack_->TracePop(); }

  size_t TracePush(ValueKind kind, uint32_t slot_offset) {
    switch (kind) {
      case kI32:
        return TracePush<int32_t>(slot_offset);
      case kI64:
        return TracePush<int64_t>(slot_offset);
      case kF32:
        return TracePush<float>(slot_offset);
      case kF64:
        return TracePush<double>(slot_offset);
      case kS128:
        return TracePush<Simd128>(slot_offset);
      case kRef:
      case kRefNull:
        return TracePush<WasmRef>(slot_offset);
      default:
        UNREACHABLE();
    }
  }
  template <typename T>
  size_t TracePush(uint32_t slot_offset) {
    shadow_stack_->TracePush<T>(slot_offset);
    return sizeof(T);
  }

  void TracePushCopy(uint32_t from_index) {
    shadow_stack_->TracePushCopy(from_index);
  }

  void TraceUpdate(uint32_t stack_index, uint32_t slot_offset) {
    shadow_stack_->TraceUpdate(stack_index, slot_offset);
  }

  void TraceSetSlotType(uint32_t stack_index, uint32_t type) {
    shadow_stack_->TraceSetSlotType(stack_index, type);
  }

  // Used to redirect tracing output from {stdout} to a file.
  InterpreterTracer* GetTracer();

  std::unique_ptr<InterpreterTracer> tracer_;
  ShadowStack* shadow_stack_;
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  WasmInterpreterRuntime(const WasmInterpreterRuntime&) = delete;
  WasmInterpreterRuntime& operator=(const WasmInterpreterRuntime&) = delete;
};

class V8_EXPORT_PRIVATE InterpreterHandle {
 public:
  static constexpr ExternalPointerTag kManagedTag = kGenericManagedTag;

  InterpreterHandle(Isolate* isolate, Handle<Tuple2> interpreter_object);

  WasmInterpreter* interpreter() { return &interpreter_; }
  const WasmModule* module() const { return module_; }

  // Returns true if exited regularly, false if a trap/exception occurred and
  // was not handled inside this activation. In the latter case, a pending
  // exception will have been set on the isolate.
  bool Execute(WasmInterpreterThread* thread, Address frame_pointer,
               uint32_t func_index,
               const std::vector<WasmValue>& argument_values,
               std::vector<WasmValue>& return_values);
  bool Execute(WasmInterpreterThread* thread, Address frame_pointer,
               uint32_t func_index, uint8_t* interpreter_fp);

  inline WasmInterpreterThread::State ContinueExecution(
      WasmInterpreterThread* thread, bool called_from_js);

  Handle<WasmInstanceObject> GetInstanceObject();

  std::vector<WasmInterpreterStackEntry> GetInterpretedStack(
      Address frame_pointer);

  int GetFunctionIndex(Address frame_pointer, int index) const;

  void SetTrapFunctionIndex(int32_t func_index);

 private:
  InterpreterHandle(const InterpreterHandle&) = delete;
  InterpreterHandle& operator=(const InterpreterHandle&) = delete;

  static ModuleWireBytes GetBytes(Tagged<Tuple2> interpreter_object);

  inline WasmInterpreterThread::State RunExecutionLoop(
      WasmInterpreterThread* thread, bool called_from_js);

  Isolate* isolate_;
  const WasmModule* module_;
  WasmInterpreter interpreter_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_INTERPRETER_WASM_INTERPRETER_RUNTIME_H_
