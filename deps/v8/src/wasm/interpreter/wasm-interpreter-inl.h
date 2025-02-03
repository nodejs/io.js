// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INTERPRETER_WASM_INTERPRETER_INL_H_
#define V8_WASM_INTERPRETER_WASM_INTERPRETER_INL_H_

#include "src/handles/handles-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#include "src/wasm/interpreter/wasm-interpreter.h"
#include "src/wasm/wasm-module.h"

namespace v8 {
namespace internal {
namespace wasm {

inline InterpreterCode* WasmInterpreter::CodeMap::GetCode(
    uint32_t function_index) {
  DCHECK_LT(function_index, interpreter_code_.size());
  InterpreterCode* code = &interpreter_code_[function_index];
  if (V8_UNLIKELY(!code->bytecode && code->start)) {
    Preprocess(function_index);
  }
  return code;
}

inline WasmBytecode* WasmInterpreter::CodeMap::GetFunctionBytecode(
    uint32_t func_index) {
  DCHECK_LT(func_index, interpreter_code_.size());

  // This precompiles the target function.
  InterpreterCode* code = GetCode(func_index);
  return code->bytecode.get();
}

inline void WasmInterpreter::CodeMap::AddFunction(const WasmFunction* function,
                                                  const uint8_t* code_start,
                                                  const uint8_t* code_end) {
  DCHECK_EQ(interpreter_code_.size(), function->func_index);
  interpreter_code_.emplace_back(function, BodyLocalDecls(),
                                 const_cast<uint8_t*>(code_start),
                                 const_cast<uint8_t*>(code_end));
}

inline Isolate* WasmInterpreterThread::Activation::GetIsolate() const {
  return wasm_runtime_->GetIsolate();
}

inline WasmInterpreterThread::Activation*
WasmInterpreterThread::StartActivation(WasmInterpreterRuntime* wasm_runtime,
                                       Address frame_pointer,
                                       uint8_t* interpreter_fp,
                                       const FrameState& frame_state) {
  Run();
  activations_.emplace_back(std::make_unique<Activation>(
      this, wasm_runtime, frame_pointer, interpreter_fp, frame_state));
  return activations_.back().get();
}

inline void WasmInterpreterThread::FinishActivation() {
  DCHECK(!activations_.empty());
  activations_.pop_back();
  if (activations_.empty()) {
    if (state_ != State::TRAPPED && state_ != State::STOPPED) {
      Finish();
    }
  }
}

inline const FrameState* WasmInterpreterThread::GetCurrentActivationFor(
    const WasmInterpreterRuntime* wasm_runtime) const {
  for (int i = static_cast<int>(activations_.size()) - 1; i >= 0; i--) {
    if (activations_[i]->GetWasmRuntime() == wasm_runtime) {
      return &activations_[i]->GetCurrentFrame();
    }
  }
  return nullptr;
}

inline void WasmInterpreter::BeginExecution(
    WasmInterpreterThread* thread, uint32_t function_index,
    Address frame_pointer, uint8_t* interpreter_fp, uint32_t ref_stack_offset,
    const std::vector<WasmValue>& args) {
  codemap_.GetCode(function_index);
  wasm_runtime_->BeginExecution(thread, function_index, frame_pointer,
                                interpreter_fp, ref_stack_offset, &args);
}

inline void WasmInterpreter::BeginExecution(WasmInterpreterThread* thread,
                                            uint32_t function_index,
                                            Address frame_pointer,
                                            uint8_t* interpreter_fp) {
  codemap_.GetCode(function_index);
  wasm_runtime_->BeginExecution(thread, function_index, frame_pointer,
                                interpreter_fp, 0);
}

inline WasmValue WasmInterpreter::GetReturnValue(int index) const {
  return wasm_runtime_->GetReturnValue(index);
}

inline std::vector<WasmInterpreterStackEntry>
WasmInterpreter::GetInterpretedStack(Address frame_pointer) {
  return wasm_runtime_->GetInterpretedStack(frame_pointer);
}

inline int WasmInterpreter::GetFunctionIndex(Address frame_pointer,
                                             int index) const {
  return wasm_runtime_->GetFunctionIndex(frame_pointer, index);
}

inline void WasmInterpreter::SetTrapFunctionIndex(int32_t func_index) {
  wasm_runtime_->SetTrapFunctionIndex(func_index);
}

template <typename T>
inline T Read(const uint8_t*& code) {
  T res = base::ReadUnalignedValue<T>(reinterpret_cast<Address>(code));
  code += sizeof(T);
  return res;
}

// Returns the maximum of the two parameters according to JavaScript semantics.
template <typename T>
inline T JSMax(T x, T y) {
  if (std::isnan(x) || std::isnan(y)) {
    return std::numeric_limits<T>::quiet_NaN();
  }
  if (std::signbit(x) < std::signbit(y)) return x;
  return x > y ? x : y;
}

// Returns the minimum of the two parameters according to JavaScript semantics.
template <typename T>
inline T JSMin(T x, T y) {
  if (std::isnan(x) || std::isnan(y)) {
    return std::numeric_limits<T>::quiet_NaN();
  }
  if (std::signbit(x) < std::signbit(y)) return y;
  return x > y ? y : x;
}

inline uint8_t* ReadMemoryAddress(uint8_t*& code) {
  Address res =
      base::ReadUnalignedValue<Address>(reinterpret_cast<Address>(code));
  code += sizeof(Address);
  return reinterpret_cast<uint8_t*>(res);
}

inline uint32_t ReadGlobalIndex(const uint8_t*& code) {
  uint32_t res =
      base::ReadUnalignedValue<uint32_t>(reinterpret_cast<Address>(code));
  code += sizeof(uint32_t);
  return res;
}

template <typename T>
inline void push(uint32_t*& sp, const uint8_t*& code,
                 WasmInterpreterRuntime* wasm_runtime, T val) {
  uint32_t offset = Read<int32_t>(code);
  base::WriteUnalignedValue<T>(reinterpret_cast<Address>(sp + offset), val);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_execution)
    wasm_runtime->TracePush<T>(offset * kSlotSize);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
}

template <>
inline void push(uint32_t*& sp, const uint8_t*& code,
                 WasmInterpreterRuntime* wasm_runtime, WasmRef ref) {
  uint32_t offset = Read<int32_t>(code);
  uint32_t ref_stack_index = Read<int32_t>(code);
  base::WriteUnalignedValue<uint64_t>(reinterpret_cast<Address>(sp + offset),
                                      kSlotsZapValue);
  //*reinterpret_cast<uint64_t*>(sp + offset) = kSlotsZapValue;
  wasm_runtime->StoreWasmRef(ref_stack_index, ref);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_execution)
    wasm_runtime->TracePush<WasmRef>(offset * kSlotSize);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
}

template <typename T>
inline T pop(uint32_t*& sp, const uint8_t*& code,
             WasmInterpreterRuntime* wasm_runtime) {
  uint32_t offset = Read<int32_t>(code);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_execution) wasm_runtime->TracePop();
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  return base::ReadUnalignedValue<T>(reinterpret_cast<Address>(sp + offset));
}

template <>
inline WasmRef pop(uint32_t*& sp, const uint8_t*& code,
                   WasmInterpreterRuntime* wasm_runtime) {
  uint32_t ref_stack_index = Read<int32_t>(code);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_execution) wasm_runtime->TracePop();
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  return wasm_runtime->ExtractWasmRef(ref_stack_index);
}

template <typename T>
inline T ExecuteRemS(T lval, T rval) {
  if (rval == -1) return 0;
  return lval % rval;
}

template <typename T>
inline T ExecuteRemU(T lval, T rval) {
  return lval % rval;
}

inline ValueType WasmBytecode::return_type(size_t index) const {
  DCHECK_LT(index, return_count());
  return signature_->GetReturn(index);
}

inline ValueType WasmBytecode::arg_type(size_t index) const {
  DCHECK_LT(index, args_count());
  return signature_->GetParam(index);
}

inline ValueType WasmBytecode::local_type(size_t index) const {
  DCHECK_LT(index, locals_count());
  DCHECK_LT(index, interpreter_code_->locals.num_locals);
  return interpreter_code_->locals.local_types[index];
}

inline uint32_t GetValueSizeInSlots(ValueKind kind) {
  switch (kind) {
    case kI32:
      return sizeof(int32_t) / kSlotSize;
    case kI64:
      return sizeof(int64_t) / kSlotSize;
    case kF32:
      return sizeof(float) / kSlotSize;
    case kF64:
      return sizeof(double) / kSlotSize;
    case kS128:
      return sizeof(Simd128) / kSlotSize;
    case kRef:
    case kRefNull:
      return sizeof(WasmRef) / kSlotSize;
    default:
      UNREACHABLE();
  }
}

inline void FrameState::ResetHandleScope(Isolate* isolate) {
  DCHECK_NOT_NULL(handle_scope_);
  {
    HandleScope old(std::move(*handle_scope_));
    // The HandleScope destructor cleans up the old HandleScope.
  }
  // Now that the old HandleScope has been destroyed, make a new one.
  *handle_scope_ = HandleScope(isolate);
}

inline uint32_t WasmBytecode::ArgsSizeInSlots(const FunctionSig* sig) {
  uint32_t args_slots_size = 0;
  size_t args_count = sig->parameter_count();
  for (size_t i = 0; i < args_count; i++) {
    args_slots_size += GetValueSizeInSlots(sig->GetParam(i).kind());
  }
  return args_slots_size;
}

inline uint32_t WasmBytecode::RetsSizeInSlots(const FunctionSig* sig) {
  uint32_t rets_slots_size = 0;
  size_t return_count = static_cast<uint32_t>(sig->return_count());
  for (size_t i = 0; i < return_count; i++) {
    rets_slots_size += GetValueSizeInSlots(sig->GetReturn(i).kind());
  }
  return rets_slots_size;
}

inline uint32_t WasmBytecode::RefArgsCount(const FunctionSig* sig) {
  uint32_t refs_args_count = 0;
  size_t args_count = static_cast<uint32_t>(sig->parameter_count());
  for (size_t i = 0; i < args_count; i++) {
    ValueKind kind = sig->GetParam(i).kind();
    if (wasm::is_reference(kind)) refs_args_count++;
  }
  return refs_args_count;
}

inline uint32_t WasmBytecode::RefRetsCount(const FunctionSig* sig) {
  uint32_t refs_rets_count = 0;
  size_t return_count = static_cast<uint32_t>(sig->return_count());
  for (size_t i = 0; i < return_count; i++) {
    ValueKind kind = sig->GetReturn(i).kind();
    if (wasm::is_reference(kind)) refs_rets_count++;
  }
  return refs_rets_count;
}

inline bool WasmBytecode::ContainsSimd(const FunctionSig* sig) {
  size_t args_count = static_cast<uint32_t>(sig->parameter_count());
  for (size_t i = 0; i < args_count; i++) {
    if (sig->GetParam(i).kind() == kS128) return true;
  }

  size_t return_count = static_cast<uint32_t>(sig->return_count());
  for (size_t i = 0; i < return_count; i++) {
    if (sig->GetReturn(i).kind() == kS128) return true;
  }

  return false;
}

inline bool WasmBytecode::HasRefOrSimdArgs(const FunctionSig* sig) {
  size_t args_count = static_cast<uint32_t>(sig->parameter_count());
  for (size_t i = 0; i < args_count; i++) {
    ValueKind kind = sig->GetParam(i).kind();
    if (wasm::is_reference(kind) || kind == kS128) return true;
  }
  return false;
}

inline uint32_t WasmBytecode::JSToWasmWrapperPackedArraySize(
    const FunctionSig* sig) {
  static_assert(kSystemPointerSize == 8);

  uint32_t args_size = 0;
  size_t args_count = static_cast<uint32_t>(sig->parameter_count());
  for (size_t i = 0; i < args_count; i++) {
    switch (sig->GetParam(i).kind()) {
      case kI32:
      case kF32:
        args_size += sizeof(int32_t);
        break;
      case kI64:
      case kF64:
        args_size += sizeof(int64_t);
        break;
      case kS128:
        args_size += sizeof(Simd128);
        break;
      case kRef:
      case kRefNull:
        // Make sure Ref slots are 64-bit aligned.
        args_size += (args_size & 0x04);
        args_size += sizeof(WasmRef);
        break;
      default:
        UNREACHABLE();
    }
  }

  uint32_t rets_size = 0;
  size_t rets_count = static_cast<uint32_t>(sig->return_count());
  for (size_t i = 0; i < rets_count; i++) {
    switch (sig->GetReturn(i).kind()) {
      case kI32:
      case kF32:
        rets_size += sizeof(int32_t);
        break;
      case kI64:
      case kF64:
        rets_size += sizeof(int64_t);
        break;
      case kS128:
        rets_size += sizeof(Simd128);
        break;
      case kRef:
      case kRefNull:
        // Make sure Ref slots are 64-bit aligned.
        rets_size += (rets_size & 0x04);
        rets_size += sizeof(WasmRef);
        break;
      default:
        UNREACHABLE();
    }
  }

  uint32_t size = std::max(args_size, rets_size);
  // Make sure final size is 64-bit aligned.
  size += (size & 0x04);
  return size;
}

inline uint32_t WasmBytecode::RefLocalsCount(const InterpreterCode* wasm_code) {
  uint32_t refs_locals_count = 0;
  size_t locals_count = wasm_code->locals.num_locals;
  for (size_t i = 0; i < locals_count; i++) {
    ValueKind kind = wasm_code->locals.local_types[i].kind();
    if (wasm::is_reference(kind)) {
      refs_locals_count++;
    }
  }
  return refs_locals_count;
}

inline uint32_t WasmBytecode::LocalsSizeInSlots(
    const InterpreterCode* wasm_code) {
  uint32_t locals_slots_size = 0;
  size_t locals_count = wasm_code->locals.num_locals;
  for (size_t i = 0; i < locals_count; i++) {
    locals_slots_size +=
        GetValueSizeInSlots(wasm_code->locals.local_types[i].kind());
  }
  return locals_slots_size;
}

inline bool WasmBytecode::InitializeSlots(uint8_t* sp,
                                          size_t stack_space) const {
  // Check for overflow
  if (total_frame_size_in_bytes_ > stack_space) {
    return false;
  }

  uint32_t args_slots_size_in_bytes = args_slots_size() * kSlotSize;
  uint32_t rets_slots_size_in_bytes = rets_slots_size() * kSlotSize;
  uint32_t const_slots_size_in_bytes = this->const_slots_size_in_bytes();

  uint8_t* start_const_area =
      sp + args_slots_size_in_bytes + rets_slots_size_in_bytes;

  // Initialize const slots
  if (const_slots_size_in_bytes) {
    memcpy(start_const_area, const_slots_values_.data(),
           const_slots_size_in_bytes);
  }

  // Initialize local slots
  memset(start_const_area + const_slots_size_in_bytes, 0,
         locals_slots_size() * kSlotSize);

  return true;
}

inline bool WasmBytecodeGenerator::ToRegisterIsAllowed(
    const WasmInstruction& instr) {
  if (!instr.SupportsToRegister()) return false;

  // Even if the instruction is marked as supporting ToRegister, reference
  // values should not be stored in the register.
  switch (instr.opcode) {
    case kExprGlobalGet: {
      ValueKind kind = GetGlobalType(instr.optional.index);
      return !wasm::is_reference(kind) && kind != kS128;
    }
    case kExprSelect:
    case kExprSelectWithType: {
      DCHECK_GE(stack_size(), 2);
      ValueKind kind = slots_[stack_[stack_size() - 2]].kind();
      return !wasm::is_reference(kind) && kind != kS128;
    }
    default:
      return true;
  }
}

inline void WasmBytecodeGenerator::I32Push(bool emit) {
  uint32_t slot_index = _PushSlot(kWasmI32);
  uint32_t slot_offset = slots_[slot_index].slot_offset;
  if (emit) Emit(&slot_offset, sizeof(uint32_t));
}

inline void WasmBytecodeGenerator::I64Push(bool emit) {
  uint32_t slot_index = _PushSlot(kWasmI64);
  uint32_t slot_offset = slots_[slot_index].slot_offset;
  if (emit) Emit(&slot_offset, sizeof(uint32_t));
}

inline void WasmBytecodeGenerator::F32Push(bool emit) {
  uint32_t slot_index = _PushSlot(kWasmF32);
  uint32_t slot_offset = slots_[slot_index].slot_offset;
  if (emit) Emit(&slot_offset, sizeof(uint32_t));
}

inline void WasmBytecodeGenerator::F64Push(bool emit) {
  uint32_t slot_index = _PushSlot(kWasmF64);
  uint32_t slot_offset = slots_[slot_index].slot_offset;
  if (emit) Emit(&slot_offset, sizeof(uint32_t));
}

inline void WasmBytecodeGenerator::S128Push(bool emit) {
  uint32_t slot_index = _PushSlot(kWasmS128);
  uint32_t slot_offset = slots_[slot_index].slot_offset;
  if (emit) Emit(&slot_offset, sizeof(uint32_t));
}

inline void WasmBytecodeGenerator::RefPush(ValueType type, bool emit) {
  uint32_t slot_index = _PushSlot(type);
  uint32_t slot_offset = slots_[slot_index].slot_offset;
  if (emit) {
    Emit(&slot_offset, sizeof(uint32_t));
    Emit(&slots_[slot_index].ref_stack_index, sizeof(uint32_t));
  }
}

inline void WasmBytecodeGenerator::Push(ValueType type) {
  switch (type.kind()) {
    case kI32:
      I32Push();
      break;
    case kI64:
      I64Push();
      break;
    case kF32:
      F32Push();
      break;
    case kF64:
      F64Push();
      break;
    case kS128:
      S128Push();
      break;
    case kRef:
    case kRefNull:
      RefPush(type);
      break;
    default:
      UNREACHABLE();
  }
}

inline void WasmBytecodeGenerator::PushCopySlot(uint32_t from) {
  DCHECK_LT(from, stack_.size());
  PushSlot(stack_[from]);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  TracePushCopySlot(from);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
}

inline void WasmBytecodeGenerator::PushConstSlot(uint32_t slot_index) {
  PushSlot(slot_index);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  TracePushConstSlot(slot_index);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
}

inline bool WasmBytecodeGenerator::HasVoidSignature(
    const WasmBytecodeGenerator::BlockData& block_data) const {
  if (block_data.signature_.value_type() == kWasmBottom) {
    const FunctionSig* sig =
        module_->signature(block_data.signature_.sig_index);
    return 0 == (sig->parameter_count() + sig->return_count());
  } else if (block_data.signature_.value_type() == kWasmVoid) {
    return true;
  }
  return false;
}

inline uint32_t WasmBytecodeGenerator::ParamsCount(
    const WasmBytecodeGenerator::BlockData& block_data) const {
  if (block_data.signature_.value_type() == kWasmBottom) {
    const FunctionSig* sig =
        module_->signature(block_data.signature_.sig_index);
    return static_cast<uint32_t>(sig->parameter_count());
  }
  return 0;
}

inline ValueType WasmBytecodeGenerator::GetParamType(
    const WasmBytecodeGenerator::BlockData& block_data, size_t index) const {
  DCHECK_EQ(block_data.signature_.value_type(), kWasmBottom);
  const FunctionSig* sig = module_->signature(block_data.signature_.sig_index);
  return sig->GetParam(index);
}

inline uint32_t WasmBytecodeGenerator::ReturnsCount(
    const WasmBytecodeGenerator::BlockData& block_data) const {
  if (block_data.signature_.value_type() == kWasmBottom) {
    const FunctionSig* sig =
        module_->signature(block_data.signature_.sig_index);
    return static_cast<uint32_t>(sig->return_count());
  } else if (block_data.signature_.value_type() == kWasmVoid) {
    return 0;
  }
  return 1;
}

inline ValueType WasmBytecodeGenerator::GetReturnType(
    const WasmBytecodeGenerator::BlockData& block_data, size_t index) const {
  DCHECK_NE(block_data.signature_.value_type(), kWasmVoid);
  if (block_data.signature_.value_type() == kWasmBottom) {
    const FunctionSig* sig =
        module_->signature(block_data.signature_.sig_index);
    return sig->GetReturn(index);
  }
  DCHECK_EQ(index, 0);
  return block_data.signature_.value_type();
}

inline ValueKind WasmBytecodeGenerator::GetGlobalType(uint32_t index) const {
  return module_->globals[index].type.kind();
}

inline bool WasmBytecodeGenerator::IsMemory64() const {
  return !module_->memories.empty() && module_->memories[0].is_memory64;
}

inline bool WasmBytecodeGenerator::IsMultiMemory() const {
  return module_->memories.size() > 1;
}

inline void WasmBytecodeGenerator::EmitGlobalIndex(uint32_t index) {
  Emit(&index, sizeof(index));
}

inline uint32_t WasmBytecodeGenerator::GetCurrentBranchDepth() const {
  DCHECK_GE(current_block_index_, 0);
  int index = blocks_[current_block_index_].parent_block_index_;
  uint32_t depth = 0;
  while (index >= 0) {
    depth++;
    index = blocks_[index].parent_block_index_;
  }
  return depth;
}

inline int32_t WasmBytecodeGenerator::GetTargetBranch(uint32_t delta) const {
  int index = current_block_index_;
  while (delta--) {
    DCHECK_GE(index, 0);
    index = blocks_[index].parent_block_index_;
  }
  return index;
}

inline void WasmBytecodeGenerator::EmitBranchOffset(uint32_t delta) {
  int32_t target_branch_index = GetTargetBranch(delta);
  DCHECK_GE(target_branch_index, 0);
  blocks_[target_branch_index].branch_code_offsets_.emplace_back(
      CurrentCodePos());

  const uint32_t current_code_offset = CurrentCodePos();
  Emit(&current_code_offset, sizeof(current_code_offset));
}

inline void WasmBytecodeGenerator::EmitBranchTableOffset(uint32_t delta,
                                                         uint32_t code_pos) {
  int32_t target_branch_index = GetTargetBranch(delta);
  DCHECK_GE(target_branch_index, 0);
  blocks_[target_branch_index].branch_code_offsets_.emplace_back(code_pos);

  Emit(&code_pos, sizeof(code_pos));
}

inline void WasmBytecodeGenerator::EmitIfElseBranchOffset() {
  // Initially emits offset to jump the end of the 'if' block. If we meet an
  // 'else' instruction later, this offset needs to be updated with the offset
  // to the beginning of that 'else' block.
  blocks_[current_block_index_].branch_code_offsets_.emplace_back(
      CurrentCodePos());

  const uint32_t current_code_offset = CurrentCodePos();
  Emit(&current_code_offset, sizeof(current_code_offset));
}

inline void WasmBytecodeGenerator::EmitTryCatchBranchOffset() {
  // Initially emits offset to jump the end of the 'try/catch' blocks. When we
  // meet the corresponding 'end' instruction later, this offset needs to be
  // updated with the offset to the 'end' instruction.
  blocks_[current_block_index_].branch_code_offsets_.emplace_back(
      CurrentCodePos());

  const uint32_t current_code_offset = CurrentCodePos();
  Emit(&current_code_offset, sizeof(current_code_offset));
}

inline void WasmBytecodeGenerator::BeginElseBlock(uint32_t if_block_index,
                                                  bool dummy) {
  EndBlock(kExprElse);  // End matching if block.
  RestoreIfElseParams(if_block_index);

  int32_t else_block_index =
      BeginBlock(kExprElse, blocks_[if_block_index].signature_);
  blocks_[if_block_index].if_else_block_index_ = else_block_index;
  blocks_[else_block_index].if_else_block_index_ = if_block_index;
  blocks_[else_block_index].first_block_index_ =
      blocks_[if_block_index].first_block_index_;
}

inline const FunctionSig* WasmBytecodeGenerator::GetFunctionSignature(
    uint32_t function_index) const {
  return module_->functions[function_index].sig;
}

inline ValueKind WasmBytecodeGenerator::GetTopStackType(
    RegMode reg_mode) const {
  switch (reg_mode) {
    case RegMode::kNoReg:
      if (stack_.empty()) return kI32;  // not used
      return slots_[stack_[stack_top_index()]].kind();
    case RegMode::kI32Reg:
      return kI32;
    case RegMode::kI64Reg:
      return kI64;
    case RegMode::kF32Reg:
      return kF32;
    case RegMode::kF64Reg:
      return kF64;
    case RegMode::kAnyReg:
    default:
      UNREACHABLE();
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_INTERPRETER_WASM_INTERPRETER_INL_H_
