// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_UNITTEST_H_
#define V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_UNITTEST_H_

#include <deque>
#include <set>
#include <type_traits>

#include "src/base/utils/random-number-generator.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::compiler::turboshaft {

#define BINOP_LIST(V)           \
  V(Word32BitwiseAnd)           \
  V(Word64BitwiseAnd)           \
  V(Word32BitwiseOr)            \
  V(Word64BitwiseOr)            \
  V(Word32BitwiseXor)           \
  V(Word64BitwiseXor)           \
  V(Word32Add)                  \
  V(Word64Add)                  \
  V(Word32Sub)                  \
  V(Word64Sub)                  \
  V(Word32Mul)                  \
  V(Word64Mul)                  \
  V(Int32Div)                   \
  V(Int64Div)                   \
  V(Int32Mod)                   \
  V(Int64Mod)                   \
  V(Uint32Div)                  \
  V(Uint64Div)                  \
  V(Uint32Mod)                  \
  V(Uint64Mod)                  \
  V(Word32ShiftLeft)            \
  V(Word64ShiftLeft)            \
  V(Word32ShiftRightLogical)    \
  V(Word64ShiftRightLogical)    \
  V(Word32ShiftRightArithmetic) \
  V(Word64ShiftRightArithmetic) \
  V(Word32RotateRight)          \
  V(Word64RotateRight)          \
  V(Int32AddCheckOverflow)      \
  V(Int64AddCheckOverflow)      \
  V(Int32SubCheckOverflow)      \
  V(Int64SubCheckOverflow)      \
  V(Word32Equal)                \
  V(Int32LessThan)              \
  V(Int32LessThanOrEqual)       \
  V(Uint32LessThan)             \
  V(Uint32LessThanOrEqual)

#define UNOP_LIST(V)          \
  V(ChangeFloat32ToFloat64)   \
  V(TruncateFloat64ToFloat32) \
  V(ChangeInt32ToInt64)       \
  V(ChangeUint32ToUint64)     \
  V(TruncateWord64ToWord32)   \
  V(ChangeInt32ToFloat64)     \
  V(ChangeUint32ToFloat64)    \
  V(ReversibleFloat64ToInt32) \
  V(ReversibleFloat64ToUint32)

#define DECL(Op) k##Op,

enum class TSBinop { BINOP_LIST(DECL) };
enum class TSUnop { UNOP_LIST(DECL) };

#undef DECL

class TurboshaftInstructionSelectorTest : public TestWithNativeContextAndZone {
 public:
  TurboshaftInstructionSelectorTest();
  ~TurboshaftInstructionSelectorTest() override;

  void SetUp() override {
    pipeline_data_.emplace(TurboshaftPipelineKind::kJS, info_, schedule_,
                           graph_zone_, this->zone(), broker_, isolate_,
                           source_positions_, node_origins_, sequence_, frame_,
                           assembler_options_, &max_unoptimized_frame_height_,
                           &max_pushed_argument_count_, instruction_zone_);
  }
  void TearDown() override { pipeline_data_.reset(); }

  base::RandomNumberGenerator* rng() { return &rng_; }

  class Stream;

  enum StreamBuilderMode {
    kAllInstructions,
    kTargetInstructions,
    kAllExceptNopInstructions
  };

  class StreamBuilder final : public TSAssembler<> {
    using Assembler = TSAssembler<>;

   public:
    StreamBuilder(TurboshaftInstructionSelectorTest* test,
                  MachineType return_type)
        : Assembler(test->graph(), test->graph(), test->zone()),
          test_(test),
          call_descriptor_(MakeCallDescriptor(test->zone(), return_type)) {
      Init();
    }

    StreamBuilder(TurboshaftInstructionSelectorTest* test,
                  MachineType return_type, MachineType parameter0_type)
        : Assembler(test->graph(), test->graph(), test->zone()),
          test_(test),
          call_descriptor_(
              MakeCallDescriptor(test->zone(), return_type, parameter0_type)) {
      Init();
    }

    StreamBuilder(TurboshaftInstructionSelectorTest* test,
                  MachineType return_type, MachineType parameter0_type,
                  MachineType parameter1_type)
        : Assembler(test->graph(), test->graph(), test->zone()),
          test_(test),
          call_descriptor_(MakeCallDescriptor(
              test->zone(), return_type, parameter0_type, parameter1_type)) {
      Init();
    }

    StreamBuilder(TurboshaftInstructionSelectorTest* test,
                  MachineType return_type, MachineType parameter0_type,
                  MachineType parameter1_type, MachineType parameter2_type)
        : Assembler(test->graph(), test->graph(), test->zone()),
          test_(test),
          call_descriptor_(MakeCallDescriptor(test->zone(), return_type,
                                              parameter0_type, parameter1_type,
                                              parameter2_type)) {
      Init();
    }

    Stream Build(CpuFeature feature) {
      return Build(InstructionSelector::Features(feature));
    }
    Stream Build(CpuFeature feature1, CpuFeature feature2) {
      return Build(InstructionSelector::Features(feature1, feature2));
    }
    Stream Build(StreamBuilderMode mode = kTargetInstructions) {
      return Build(InstructionSelector::Features(), mode);
    }
    Stream Build(InstructionSelector::Features features,
                 StreamBuilderMode mode = kTargetInstructions,
                 InstructionSelector::SourcePositionMode source_position_mode =
                     InstructionSelector::kAllSourcePositions);

    const FrameStateFunctionInfo* GetFrameStateFunctionInfo(int parameter_count,
                                                            int local_count);

    // Create a simple call descriptor for testing.
    static CallDescriptor* MakeSimpleCallDescriptor(Zone* zone,
                                                    MachineSignature* msig) {
      LocationSignature::Builder locations(zone, msig->return_count(),
                                           msig->parameter_count());

      // Add return location(s).
      const int return_count = static_cast<int>(msig->return_count());
      for (int i = 0; i < return_count; i++) {
        locations.AddReturn(
            LinkageLocation::ForCallerFrameSlot(-1 - i, msig->GetReturn(i)));
      }

      // Just put all parameters on the stack.
      const int parameter_count = static_cast<int>(msig->parameter_count());
      unsigned slot_index = -1;
      for (int i = 0; i < parameter_count; i++) {
        locations.AddParam(
            LinkageLocation::ForCallerFrameSlot(slot_index, msig->GetParam(i)));

        // Slots are kSystemPointerSize sized. This reserves enough for space
        // for types that might be bigger, eg. Simd128.
        slot_index -=
            std::max(1, ElementSizeInBytes(msig->GetParam(i).representation()) /
                            kSystemPointerSize);
      }

      const RegList kCalleeSaveRegisters;
      const DoubleRegList kCalleeSaveFPRegisters;

      MachineType target_type = MachineType::Pointer();
      LinkageLocation target_loc = LinkageLocation::ForAnyRegister();

      return zone->New<CallDescriptor>(  // --
          CallDescriptor::kCallAddress,  // kind
          kDefaultCodeEntrypointTag,     // tag
          target_type,                   // target MachineType
          target_loc,                    // target location
          locations.Build(),             // location_sig
          0,                             // stack_parameter_count
          Operator::kNoProperties,       // properties
          kCalleeSaveRegisters,          // callee-saved registers
          kCalleeSaveFPRegisters,        // callee-saved fp regs
          CallDescriptor::kCanUseRoots,  // flags
          "iselect-test-call");
    }

    CallDescriptor* call_descriptor() { return call_descriptor_; }

    OpIndex Emit(TSUnop op, OpIndex input) {
      switch (op) {
#define CASE(Op)      \
  case TSUnop::k##Op: \
    return Op(input);
        UNOP_LIST(CASE)
#undef CASE
      }
    }

    OpIndex Emit(TSBinop op, OpIndex left, OpIndex right) {
      switch (op) {
#define CASE(Op)       \
  case TSBinop::k##Op: \
    return Op(left, right);
        BINOP_LIST(CASE)
#undef CASE
      }
    }

    // Some helpers to have the same interface as the Turbofan instruction
    // selector test had.
    V<Word32> Int32Constant(int32_t c) { return Word32Constant(c); }
    V<Word64> Int64Constant(int64_t c) { return Word64Constant(c); }
    using Assembler::Parameter;
    OpIndex Parameter(int index) {
      return Parameter(index, RegisterRepresentation::FromMachineType(
                                  call_descriptor()->GetParameterType(index)));
    }
    using Assembler::Phi;
    template <typename... Args,
              typename = std::enable_if_t<
                  (true && ... && std::is_convertible_v<Args, OpIndex>)>>
    OpIndex Phi(MachineRepresentation rep, Args... inputs) {
      return Phi({inputs...},
                 RegisterRepresentation::FromMachineRepresentation(rep));
    }
    using Assembler::Load;
    OpIndex Load(MachineType type, OpIndex base, OpIndex index) {
      MemoryRepresentation mem_rep =
          MemoryRepresentation::FromMachineType(type);
      return Load(base, index, LoadOp::Kind::RawAligned(), mem_rep,
                  mem_rep.ToRegisterRepresentation());
    }
    using Assembler::Projection;
    OpIndex Projection(OpIndex input, int index) {
      const Operation& input_op = output_graph().Get(input);
      if (const TupleOp* tuple = input_op.TryCast<TupleOp>()) {
        DCHECK_LT(index, tuple->input_count);
        return tuple->input(index);
      }
      DCHECK_LT(index, input_op.outputs_rep().size());
      return Projection(input, index, input_op.outputs_rep()[index]);
    }

   private:
    template <typename... ParamT>
    CallDescriptor* MakeCallDescriptor(Zone* zone, MachineType return_type,
                                       ParamT... parameter_type) {
      MachineSignature::Builder builder(zone, 1, sizeof...(ParamT));
      builder.AddReturn(return_type);
      (builder.AddParam(parameter_type), ...);
      return MakeSimpleCallDescriptor(zone, builder.Build());
    }

    void Init() {
      // We reset the graph since the StreamBuilder is meant to create a new
      // fresh graph.
      test_->graph().Reset();

      // We bind a block right at the start so that test can start emitting
      // operations without always needing to bind a block first.
      Block* start_block = NewBlock();
      Bind(start_block);
    }

    TurboshaftInstructionSelectorTest* test_;
    CallDescriptor* call_descriptor_;
  };

  class Stream final {
   public:
    size_t size() const { return instructions_.size(); }
    const Instruction* operator[](size_t index) const {
      EXPECT_LT(index, size());
      return instructions_[index];
    }

    bool IsDouble(const InstructionOperand* operand) const {
      return IsDouble(ToVreg(operand));
    }

    bool IsDouble(OpIndex index) const { return IsDouble(ToVreg(index)); }

    bool IsInteger(const InstructionOperand* operand) const {
      return IsInteger(ToVreg(operand));
    }

    bool IsInteger(OpIndex index) const { return IsInteger(ToVreg(index)); }

    bool IsReference(const InstructionOperand* operand) const {
      return IsReference(ToVreg(operand));
    }

    bool IsReference(OpIndex index) const { return IsReference(ToVreg(index)); }

    float ToFloat32(const InstructionOperand* operand) const {
      return ToConstant(operand).ToFloat32();
    }

    double ToFloat64(const InstructionOperand* operand) const {
      return ToConstant(operand).ToFloat64().value();
    }

    int32_t ToInt32(const InstructionOperand* operand) const {
      return ToConstant(operand).ToInt32();
    }

    int64_t ToInt64(const InstructionOperand* operand) const {
      return ToConstant(operand).ToInt64();
    }

    Handle<HeapObject> ToHeapObject(const InstructionOperand* operand) const {
      return ToConstant(operand).ToHeapObject();
    }

    int ToVreg(const InstructionOperand* operand) const {
      if (operand->IsConstant()) {
        return ConstantOperand::cast(operand)->virtual_register();
      }
      EXPECT_EQ(InstructionOperand::UNALLOCATED, operand->kind());
      return UnallocatedOperand::cast(operand)->virtual_register();
    }

    int ToVreg(OpIndex index) const;

    bool IsFixed(const InstructionOperand* operand, Register reg) const;
    bool IsSameAsFirst(const InstructionOperand* operand) const;
    bool IsSameAsInput(const InstructionOperand* operand,
                       int input_index) const;
    bool IsUsedAtStart(const InstructionOperand* operand) const;

    FrameStateDescriptor* GetFrameStateDescriptor(int deoptimization_id) {
      EXPECT_LT(deoptimization_id, GetFrameStateDescriptorCount());
      return deoptimization_entries_[deoptimization_id];
    }

    int GetFrameStateDescriptorCount() {
      return static_cast<int>(deoptimization_entries_.size());
    }

   private:
    bool IsDouble(int virtual_register) const {
      return doubles_.find(virtual_register) != doubles_.end();
    }

    bool IsInteger(int virtual_register) const {
      return !IsDouble(virtual_register) && !IsReference(virtual_register);
    }

    bool IsReference(int virtual_register) const {
      return references_.find(virtual_register) != references_.end();
    }

    Constant ToConstant(const InstructionOperand* operand) const {
      ConstantMap::const_iterator i;
      if (operand->IsConstant()) {
        i = constants_.find(ConstantOperand::cast(operand)->virtual_register());
        EXPECT_EQ(ConstantOperand::cast(operand)->virtual_register(), i->first);
        EXPECT_FALSE(constants_.end() == i);
      } else {
        EXPECT_EQ(InstructionOperand::IMMEDIATE, operand->kind());
        auto imm = ImmediateOperand::cast(operand);
        if (imm->type() == ImmediateOperand::INLINE_INT32) {
          return Constant(imm->inline_int32_value());
        } else if (imm->type() == ImmediateOperand::INLINE_INT64) {
          return Constant(imm->inline_int64_value());
        }
        i = immediates_.find(imm->indexed_value());
        EXPECT_EQ(imm->indexed_value(), i->first);
        EXPECT_FALSE(immediates_.end() == i);
      }
      return i->second;
    }

    friend class StreamBuilder;

    using ConstantMap = std::map<int, Constant>;
    using VirtualRegisters = std::map<uint32_t, int>;

    ConstantMap constants_;
    ConstantMap immediates_;
    std::deque<Instruction*> instructions_;
    std::set<int> doubles_;
    std::set<int> references_;
    VirtualRegisters virtual_registers_;
    std::deque<FrameStateDescriptor*> deoptimization_entries_;
  };

  base::RandomNumberGenerator rng_;

  Graph& graph() { return PipelineData::Get().graph(); }

  // We use some dummy data to initialize the PipelineData::Scope.
  // TODO(nicohartmann@): Clean this up once PipelineData is reorganized.
  OptimizedCompilationInfo* info_ = nullptr;
  Schedule* schedule_ = nullptr;
  Zone* graph_zone_ = this->zone();
  JSHeapBroker* broker_ = nullptr;
  Isolate* isolate_ = this->isolate();
  SourcePositionTable* source_positions_ = nullptr;
  NodeOriginTable* node_origins_ = nullptr;
  InstructionSequence* sequence_ = nullptr;
  Frame* frame_ = nullptr;
  AssemblerOptions assembler_options_;
  size_t max_unoptimized_frame_height_ = 0;
  size_t max_pushed_argument_count_ = 0;
  Zone* instruction_zone_ = this->zone();

  base::Optional<turboshaft::PipelineData::Scope> pipeline_data_;
};

template <typename T>
class TurboshaftInstructionSelectorTestWithParam
    : public TurboshaftInstructionSelectorTest,
      public ::testing::WithParamInterface<T> {};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_UNITTEST_H_
