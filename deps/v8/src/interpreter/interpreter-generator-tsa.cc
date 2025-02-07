// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter-generator-tsa.h"

#include "src/builtins/number-builtins-reducer-inl.h"
#include "src/codegen/turboshaft-builtins-assembler-inl.h"
#include "src/compiler/linkage.h"

namespace v8::internal::interpreter {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using namespace compiler::turboshaft;  // NOLINT(build/namespaces)

#define IGNITION_HANDLER_TS(Name, BaseAssembler)                            \
  class Name##AssemblerTS : public BaseAssembler {                          \
   public:                                                                  \
    using Base = BaseAssembler;                                             \
    Name##AssemblerTS(compiler::turboshaft::PipelineData* data,             \
                      Isolate* isolate, compiler::turboshaft::Graph& graph, \
                      Zone* phase_zone)                                     \
        : Base(data, graph, phase_zone) {}                                  \
    Name##AssemblerTS(const Name##AssemblerTS&) = delete;                   \
    Name##AssemblerTS& operator=(const Name##AssemblerTS&) = delete;        \
    void Generate##Name##Impl();                                            \
  };                                                                        \
  void Name##AssemblerTS_Generate(                                          \
      compiler::turboshaft::PipelineData* data, Isolate* isolate,           \
      compiler::turboshaft::Graph& graph, Zone* zone) {                     \
    Name##AssemblerTS assembler(data, isolate, graph, zone);                \
    assembler.EmitBytecodeHandlerProlog();                                  \
    compiler::turboshaft::Block* catch_block = assembler.NewBlock();        \
    Name##AssemblerTS::CatchScope catch_scope(assembler, catch_block);      \
    assembler.Generate##Name##Impl();                                       \
    assembler.EmitEpilog(catch_block);                                      \
  }                                                                         \
  void Name##AssemblerTS::Generate##Name##Impl()

template <typename Next>
class BytecodeHandlerReducer : public Next {
 public:
  BUILTIN_REDUCER(BytecodeHandler)

  ~BytecodeHandlerReducer() {
    // If the following check fails the handler does not use the
    // accumulator in the way described in the bytecode definitions in
    // bytecodes.h.
    DCHECK_EQ(data_.implicit_register_use,
              Bytecodes::GetImplicitRegisterUse(data_.bytecode));
  }

  void InitializeParameters(V<Object> accumulator,
                            V<BytecodeArray> bytecode_array,
                            V<WordPtr> bytecode_offset,
                            V<WordPtr> dispatch_table) {
    accumulator_ = accumulator;
    bytecode_array_ = bytecode_array;
    bytecode_offset_ = bytecode_offset;
    dispatch_table_ = dispatch_table;
  }

  V<Object> GetAccumulator() {
    DCHECK(Bytecodes::ReadsAccumulator(data_.bytecode));
    TrackRegisterUse(ImplicitRegisterUse::kReadAccumulator);
    return accumulator_;
  }

  void SetAccumulator(V<Object> value) {
    DCHECK(Bytecodes::WritesAccumulator(data_.bytecode));
    TrackRegisterUse(ImplicitRegisterUse::kWriteAccumulator);
    accumulator_ = value;
  }

  V<Context> GetContext() {
    return V<Context>::Cast(LoadRegister(Register::current_context()));
  }

  void Dispatch() {
    __ CodeComment("========= Dispatch");
    DCHECK_IMPLIES(Bytecodes::MakesCallAlongCriticalPath(data_.bytecode),
                   data_.made_call);
    V<WordPtr> target_offset = Advance(CurrentBytecodeSize());
    V<WordPtr> target_bytecode = LoadBytecode(target_offset);
    DispatchToBytecodeWithOptionalStarLookahead(target_bytecode);
  }

  void DispatchToBytecodeWithOptionalStarLookahead(V<WordPtr> target_bytecode) {
    if (Bytecodes::IsStarLookahead(data_.bytecode, operand_scale())) {
      StarDispatchLookahead(target_bytecode);
    }
    DispatchToBytecode(target_bytecode, BytecodeOffset());
  }

  void DispatchToBytecode(V<WordPtr> target_bytecode,
                          V<WordPtr> new_bytecode_offset) {
#ifdef V8_IGNITION_DISPATCH_COUNTING
    TraceBytecodeDispatch(target_bytecode);
#endif

    static_assert(kSystemPointerSizeLog2 ==
                  MemoryRepresentation::UintPtr().SizeInBytesLog2());
    V<WordPtr> target_code_entry =
        __ LoadOffHeap(DispatchTablePointer(), target_bytecode, 0,
                       MemoryRepresentation::UintPtr());

    DispatchToBytecodeHandlerEntry(target_code_entry, new_bytecode_offset);
  }

  void DispatchToBytecodeHandlerEntry(V<WordPtr> handler_entry,
                                      V<WordPtr> bytecode_offset) {
    TailCallBytecodeDispatch(
        InterpreterDispatchDescriptor{}, handler_entry, accumulator_.Get(),
        bytecode_offset, BytecodeArrayTaggedPointer(), DispatchTablePointer());
  }

  void StarDispatchLookahead(V<WordPtr> target_bytecode) { UNIMPLEMENTED(); }

  template <typename... Args>
  void TailCallBytecodeDispatch(const CallInterfaceDescriptor& descriptor,
                                V<WordPtr> target, Args... args) {
    DCHECK_EQ(descriptor.GetParameterCount(), sizeof...(Args));
    auto call_descriptor = compiler::Linkage::GetBytecodeDispatchCallDescriptor(
        graph_zone_, descriptor, descriptor.GetStackParameterCount());
    auto ts_call_descriptor =
        TSCallDescriptor::Create(call_descriptor, compiler::CanThrow::kNo,
                                 compiler::LazyDeoptOnThrow::kNo, graph_zone_);

    std::initializer_list<const OpIndex> arguments{args...};
    __ TailCall(target, base::VectorOf(arguments), ts_call_descriptor);
  }

  V<WordPtr> Advance(ConstOrV<WordPtr> delta) {
    V<WordPtr> next_offset = __ WordPtrAdd(BytecodeOffset(), delta);
    bytecode_offset_ = next_offset;
    return next_offset;
  }

  V<Object> LoadRegister(Register reg) {
    const int offset = reg.ToOperand() * kSystemPointerSize;
    return __ LoadOffHeap(GetInterpretedFramePointer(), offset,
                          MemoryRepresentation::AnyTagged());
  }

  V<WordPtr> GetInterpretedFramePointer() {
    if (!interpreted_frame_pointer_.Get().valid()) {
      interpreted_frame_pointer_ = __ ParentFramePointer();
    } else if (Bytecodes::MakesCallAlongCriticalPath(data_.bytecode) &&
               data_.made_call && data_.reloaded_frame_ptr) {
      interpreted_frame_pointer_ = __ ParentFramePointer();
      data_.reloaded_frame_ptr = true;
    }
    return interpreted_frame_pointer_;
  }

  V<WordPtr> BytecodeOffset() {
    if (Bytecodes::MakesCallAlongCriticalPath(data_.bytecode) &&
        data_.made_call && (bytecode_offset_ == bytecode_offset_parameter_)) {
      bytecode_offset_ = ReloadBytecodeOffset();
    }
    return bytecode_offset_;
  }

  V<WordPtr> ReloadBytecodeOffset() {
    V<WordPtr> offset = LoadAndUntagRegister(Register::bytecode_offset());
    if (operand_scale() == OperandScale::kSingle) {
      return offset;
    }

    // Add one to the offset such that it points to the actual bytecode rather
    // than the Wide / ExtraWide prefix bytecode.
    return __ WordPtrAdd(offset, 1);
  }

  V<Word32> LoadFromBytecodeArrayAt(MemoryRepresentation loaded_rep,
                                    V<WordPtr> bytecode_offset,
                                    int additional_offset = 0) {
    return __ Load(BytecodeArrayTaggedPointer(), bytecode_offset,
                   LoadOp::Kind::TaggedBase(), loaded_rep,
                   additional_offset + kHeapObjectTag);
  }

  V<WordPtr> LoadBytecode(V<WordPtr> bytecode_offset) {
    V<Word32> bytecode = __ Load(BytecodeArrayTaggedPointer(), bytecode_offset,
                                 LoadOp::Kind::TaggedBase(),
                                 MemoryRepresentation::Uint8(), kHeapObjectTag);
    return __ ChangeUint32ToUintPtr(bytecode);
  }

  V<WordPtr> LoadAndUntagRegister(Register reg) {
    V<WordPtr> base = GetInterpretedFramePointer();
    int index = reg.ToOperand() * kSystemPointerSize;
    if (SmiValuesAre32Bits()) {
#if V8_TARGET_LITTLE_ENDIAN
      index += 4;
#endif
      return __ ChangeInt32ToIntPtr(
          __ LoadOffHeap(base, index, MemoryRepresentation::Int32()));
    } else {
      return __ ChangeInt32ToIntPtr(__ UntagSmi(
          __ LoadOffHeap(base, index, MemoryRepresentation::TaggedSigned())));
    }
  }

  // TODO(nicohartmann): Consider providing a V<ExternalReference>.
  V<WordPtr> DispatchTablePointer() {
    if (Bytecodes::MakesCallAlongCriticalPath(data_.bytecode) &&
        data_.made_call && (dispatch_table_ == dispatch_table_parameter_)) {
      dispatch_table_ = __ ExternalConstant(
          ExternalReference::interpreter_dispatch_table_address(isolate_));
    }
    return dispatch_table_;
  }

  V<BytecodeArray> BytecodeArrayTaggedPointer() {
    // Force a re-load of the bytecode array after every call in case the
    // debugger has been activated.
    if (!data_.bytecode_array_valid) {
      bytecode_array_ = LoadRegister(Register::bytecode_array());
      data_.bytecode_array_valid = true;
    }
    return V<BytecodeArray>::Cast(bytecode_array_);
  }

  V<Word32> BytecodeOperandIdxInt32(int operand_index) {
    DCHECK_EQ(OperandType::kIdx,
              Bytecodes::GetOperandType(data_.bytecode, operand_index));
    OperandSize operand_size = Bytecodes::GetOperandSize(
        data_.bytecode, operand_index, operand_scale());
    return BytecodeUnsignedOperand(operand_index, operand_size);
  }

  V<Word32> BytecodeUnsignedOperand(int operand_index,
                                    OperandSize operand_size) {
    return BytecodeOperand(operand_index, operand_size);
  }

  V<Word32> BytecodeOperand(int operand_index, OperandSize operand_size) {
    DCHECK_LT(operand_index, Bytecodes::NumberOfOperands(bytecode()));
    DCHECK_EQ(operand_size, Bytecodes::GetOperandSize(bytecode(), operand_index,
                                                      operand_scale()));
    MemoryRepresentation loaded_rep;
    switch (operand_size) {
      case OperandSize::kByte:
        loaded_rep = MemoryRepresentation::Uint8();
        break;
      case OperandSize::kShort:
        loaded_rep = MemoryRepresentation::Uint16();
        break;
      case OperandSize::kQuad:
        loaded_rep = MemoryRepresentation::Uint32();
        break;
      case OperandSize::kNone:
        UNREACHABLE();
    }
    return LoadFromBytecodeArrayAt(loaded_rep, BytecodeOffset(),
                                   OperandOffset(operand_index));
  }

  int OperandOffset(int operand_index) const {
    return Bytecodes::GetOperandOffset(bytecode(), operand_index,
                                       operand_scale());
  }

 private:
  Bytecode bytecode() const { return data_.bytecode; }
  OperandScale operand_scale() const { return data_.operand_scale; }

  int CurrentBytecodeSize() const {
    return Bytecodes::Size(data_.bytecode, data_.operand_scale);
  }

  void TrackRegisterUse(ImplicitRegisterUse use) {
    data_.implicit_register_use = data_.implicit_register_use | use;
  }

  Isolate* isolate_ = __ data() -> isolate();
  ZoneWithName<compiler::kGraphZoneName>& graph_zone_ =
      __ data() -> graph_zone();
  BytecodeHandlerData& data_ = *__ data() -> bytecode_handler_data();
  // TODO(nicohartmann): Replace with Var<T>s.
  OpIndex bytecode_offset_parameter_;
  OpIndex dispatch_table_parameter_;
  template <typename T>
  using Var = compiler::turboshaft::Var<T, assembler_t>;
  Var<Object> accumulator_{this};
  Var<WordPtr> interpreted_frame_pointer_{this};
  Var<WordPtr> bytecode_offset_{this};
  Var<Object> bytecode_array_{this};
  Var<WordPtr> dispatch_table_{this};
};

template <template <typename> typename Reducer>
class TurboshaftBytecodeHandlerAssembler
    : public compiler::turboshaft::TSAssembler<
          Reducer, BytecodeHandlerReducer, BuiltinsReducer,
          FeedbackCollectorReducer,
          compiler::turboshaft::MachineLoweringReducer,
          compiler::turboshaft::VariableReducer> {
 public:
  using Base = compiler::turboshaft::TSAssembler<
      Reducer, BytecodeHandlerReducer, BuiltinsReducer,
      FeedbackCollectorReducer, compiler::turboshaft::MachineLoweringReducer,
      compiler::turboshaft::VariableReducer>;
  TurboshaftBytecodeHandlerAssembler(compiler::turboshaft::PipelineData* data,
                                     compiler::turboshaft::Graph& graph,
                                     Zone* phase_zone)
      : Base(data, graph, graph, phase_zone) {}

  using Base::Asm;

  void EmitBytecodeHandlerProlog() {
    // Bind an entry block.
    __ Bind(__ NewBlock());
    // Initialize parameters.
    V<Object> acc = __ template Parameter<Object>(
        InterpreterDispatchDescriptor::kAccumulator);
    V<WordPtr> bytecode_offset = __ template Parameter<WordPtr>(
        InterpreterDispatchDescriptor::kBytecodeOffset);
    V<BytecodeArray> bytecode_array = __ template Parameter<BytecodeArray>(
        InterpreterDispatchDescriptor::kBytecodeArray);
    V<WordPtr> dispatch_table = __ template Parameter<WordPtr>(
        InterpreterDispatchDescriptor::kDispatchTable);
    __ InitializeParameters(acc, bytecode_array, bytecode_offset,
                            dispatch_table);
  }
};

using NumberBuiltinsBytecodeHandlerAssembler =
    TurboshaftBytecodeHandlerAssembler<NumberBuiltinsReducer>;

IGNITION_HANDLER_TS(BitwiseNot, NumberBuiltinsBytecodeHandlerAssembler) {
  V<Object> value = GetAccumulator();
  V<Context> context = GetContext();

  constexpr int kSlotIndex = 0;
  SetFeedbackSlot(
      __ ChangeUint32ToUintPtr(__ BytecodeOperandIdxInt32(kSlotIndex)));
  LoadFeedbackVectorOrUndefinedIfJitless();

  V<Object> result = BitwiseNot(context, value);
  SetAccumulator(result);

  UpdateFeedback();
  Dispatch();
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::interpreter
