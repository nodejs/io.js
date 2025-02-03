// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_UNITTEST_H_
#define V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_UNITTEST_H_

#include <deque>
#include <set>

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/macro-assembler.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/raw-machine-assembler.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

class InstructionSelectorTest : public TestWithNativeContextAndZone {
 public:
  InstructionSelectorTest();
  ~InstructionSelectorTest() override;

  base::RandomNumberGenerator* rng() { return &rng_; }

  class Stream;

  enum StreamBuilderMode {
    kAllInstructions,
    kTargetInstructions,
    kAllExceptNopInstructions
  };

  class StreamBuilder final : public RawMachineAssembler {
   public:
    StreamBuilder(InstructionSelectorTest* test, MachineType return_type)
        : RawMachineAssembler(test->isolate(),
                              test->zone()->New<Graph>(test->zone()),
                              MakeCallDescriptor(test->zone(), return_type),
                              MachineType::PointerRepresentation(),
                              MachineOperatorBuilder::kAllOptionalOps),
          test_(test) {}
    StreamBuilder(InstructionSelectorTest* test, MachineType return_type,
                  MachineType parameter0_type)
        : RawMachineAssembler(
              test->isolate(), test->zone()->New<Graph>(test->zone()),
              MakeCallDescriptor(test->zone(), return_type, parameter0_type),
              MachineType::PointerRepresentation(),
              MachineOperatorBuilder::kAllOptionalOps,
              InstructionSelector::AlignmentRequirements()),
          test_(test) {}
    StreamBuilder(InstructionSelectorTest* test, MachineType return_type,
                  MachineType parameter0_type, MachineType parameter1_type)
        : RawMachineAssembler(
              test->isolate(), test->zone()->New<Graph>(test->zone()),
              MakeCallDescriptor(test->zone(), return_type, parameter0_type,
                                 parameter1_type),
              MachineType::PointerRepresentation(),
              MachineOperatorBuilder::kAllOptionalOps),
          test_(test) {}
    StreamBuilder(InstructionSelectorTest* test, MachineType return_type,
                  MachineType parameter0_type, MachineType parameter1_type,
                  MachineType parameter2_type)
        : RawMachineAssembler(
              test->isolate(), test->zone()->New<Graph>(test->zone()),
              MakeCallDescriptor(test->zone(), return_type, parameter0_type,
                                 parameter1_type, parameter2_type),
              MachineType::PointerRepresentation(),
              MachineOperatorBuilder::kAllOptionalOps),
          test_(test) {}

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

    const FrameStateFunctionInfo* GetFrameStateFunctionInfo(
        uint16_t parameter_count, int local_count);

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

   private:
    CallDescriptor* MakeCallDescriptor(Zone* zone, MachineType return_type) {
      MachineSignature::Builder builder(zone, 1, 0);
      builder.AddReturn(return_type);
      return MakeSimpleCallDescriptor(zone, builder.Build());
    }

    CallDescriptor* MakeCallDescriptor(Zone* zone, MachineType return_type,
                                       MachineType parameter0_type) {
      MachineSignature::Builder builder(zone, 1, 1);
      builder.AddReturn(return_type);
      builder.AddParam(parameter0_type);
      return MakeSimpleCallDescriptor(zone, builder.Build());
    }

    CallDescriptor* MakeCallDescriptor(Zone* zone, MachineType return_type,
                                       MachineType parameter0_type,
                                       MachineType parameter1_type) {
      MachineSignature::Builder builder(zone, 1, 2);
      builder.AddReturn(return_type);
      builder.AddParam(parameter0_type);
      builder.AddParam(parameter1_type);
      return MakeSimpleCallDescriptor(zone, builder.Build());
    }

    CallDescriptor* MakeCallDescriptor(Zone* zone, MachineType return_type,
                                       MachineType parameter0_type,
                                       MachineType parameter1_type,
                                       MachineType parameter2_type) {
      MachineSignature::Builder builder(zone, 1, 3);
      builder.AddReturn(return_type);
      builder.AddParam(parameter0_type);
      builder.AddParam(parameter1_type);
      builder.AddParam(parameter2_type);
      return MakeSimpleCallDescriptor(zone, builder.Build());
    }

    InstructionSelectorTest* test_;
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

    bool IsDouble(const Node* node) const { return IsDouble(ToVreg(node)); }

    bool IsInteger(const InstructionOperand* operand) const {
      return IsInteger(ToVreg(operand));
    }

    bool IsInteger(const Node* node) const { return IsInteger(ToVreg(node)); }

    bool IsReference(const InstructionOperand* operand) const {
      return IsReference(ToVreg(operand));
    }

    bool IsReference(const Node* node) const {
      return IsReference(ToVreg(node));
    }

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

    int ToVreg(const Node* node) const;

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
    using VirtualRegisters = std::map<NodeId, int>;

    ConstantMap constants_;
    ConstantMap immediates_;
    std::deque<Instruction*> instructions_;
    std::set<int> doubles_;
    std::set<int> references_;
    VirtualRegisters virtual_registers_;
    std::deque<FrameStateDescriptor*> deoptimization_entries_;
  };

  base::RandomNumberGenerator rng_;
};

template <typename T>
class InstructionSelectorTestWithParam
    : public InstructionSelectorTest,
      public ::testing::WithParamInterface<T> {};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_COMPILER_INSTRUCTION_SELECTOR_UNITTEST_H_
