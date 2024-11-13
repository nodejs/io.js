// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/machine-graph-verifier.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

class MachineRepresentationInferrer {
 public:
  MachineRepresentationInferrer(Schedule const* schedule, Graph const* graph,
                                Linkage* linkage, Zone* zone)
      : schedule_(schedule),
        linkage_(linkage),
        representation_vector_(graph->NodeCount(), MachineRepresentation::kNone,
                               zone) {
    Run();
  }

  CallDescriptor* call_descriptor() const {
    return linkage_->GetIncomingDescriptor();
  }

  MachineRepresentation GetRepresentation(Node const* node) const {
    return representation_vector_.at(node->id());
  }

 private:
  MachineRepresentation PromoteRepresentation(MachineRepresentation rep) {
    switch (rep) {
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        return MachineRepresentation::kWord32;
      case MachineRepresentation::kSandboxedPointer:
        // A sandboxed pointer is a Word64 that uses an encoded representation
        // when stored on the heap.
        return MachineRepresentation::kWord64;
      default:
        break;
    }
    return rep;
  }

  void Run() {
    auto blocks = schedule_->all_blocks();
    for (BasicBlock* block : *blocks) {
      current_block_ = block;
      for (size_t i = 0; i <= block->NodeCount(); ++i) {
        Node const* node =
            i < block->NodeCount() ? block->NodeAt(i) : block->control_input();
        if (node == nullptr) {
          DCHECK_EQ(block->NodeCount(), i);
          break;
        }
        switch (node->opcode()) {
          case IrOpcode::kParameter:
            representation_vector_[node->id()] =
                linkage_->GetParameterType(ParameterIndexOf(node->op()))
                    .representation();
            break;
          case IrOpcode::kReturn: {
            representation_vector_[node->id()] = PromoteRepresentation(
                linkage_->GetReturnType().representation());
            break;
          }
          case IrOpcode::kProjection: {
            representation_vector_[node->id()] =
                NodeProperties::GetProjectionType(node);
          } break;
          case IrOpcode::kTypedStateValues:
            representation_vector_[node->id()] = MachineRepresentation::kNone;
            break;
          case IrOpcode::kWord32AtomicLoad:
          case IrOpcode::kWord64AtomicLoad:
            representation_vector_[node->id()] =
                PromoteRepresentation(AtomicLoadParametersOf(node->op())
                                          .representation()
                                          .representation());
            break;
          case IrOpcode::kLoad:
          case IrOpcode::kLoadImmutable:
          case IrOpcode::kProtectedLoad:
          case IrOpcode::kLoadTrapOnNull:
            representation_vector_[node->id()] = PromoteRepresentation(
                LoadRepresentationOf(node->op()).representation());
            break;
          case IrOpcode::kLoadFramePointer:
          case IrOpcode::kLoadParentFramePointer:
          case IrOpcode::kStackSlot:
          case IrOpcode::kLoadRootRegister:
#if V8_ENABLE_WEBASSEMBLY
          case IrOpcode::kLoadStackPointer:
#endif  // V8_ENABLE_WEBASSEMBLY
            representation_vector_[node->id()] =
                MachineType::PointerRepresentation();
            break;
          case IrOpcode::kUnalignedLoad:
            representation_vector_[node->id()] = PromoteRepresentation(
                LoadRepresentationOf(node->op()).representation());
            break;
          case IrOpcode::kPhi:
            representation_vector_[node->id()] =
                PhiRepresentationOf(node->op());
            break;
          case IrOpcode::kCall: {
            auto call_descriptor = CallDescriptorOf(node->op());
            if (call_descriptor->ReturnCount() > 0) {
              representation_vector_[node->id()] =
                  call_descriptor->GetReturnType(0).representation();
            } else {
              representation_vector_[node->id()] =
                  MachineRepresentation::kTagged;
            }
            break;
          }
          case IrOpcode::kWord32AtomicStore:
          case IrOpcode::kWord64AtomicStore:
            representation_vector_[node->id()] = PromoteRepresentation(
                AtomicStoreParametersOf(node->op()).representation());
            break;
          case IrOpcode::kWord32AtomicPairLoad:
          case IrOpcode::kWord32AtomicPairStore:
          case IrOpcode::kWord32AtomicPairAdd:
          case IrOpcode::kWord32AtomicPairSub:
          case IrOpcode::kWord32AtomicPairAnd:
          case IrOpcode::kWord32AtomicPairOr:
          case IrOpcode::kWord32AtomicPairXor:
          case IrOpcode::kWord32AtomicPairExchange:
          case IrOpcode::kWord32AtomicPairCompareExchange:
            representation_vector_[node->id()] = MachineRepresentation::kWord32;
            break;
          case IrOpcode::kWord32AtomicExchange:
          case IrOpcode::kWord32AtomicCompareExchange:
          case IrOpcode::kWord32AtomicAdd:
          case IrOpcode::kWord32AtomicSub:
          case IrOpcode::kWord32AtomicAnd:
          case IrOpcode::kWord32AtomicOr:
          case IrOpcode::kWord32AtomicXor:
          case IrOpcode::kWord64AtomicExchange:
          case IrOpcode::kWord64AtomicCompareExchange:
          case IrOpcode::kWord64AtomicAdd:
          case IrOpcode::kWord64AtomicSub:
          case IrOpcode::kWord64AtomicAnd:
          case IrOpcode::kWord64AtomicOr:
          case IrOpcode::kWord64AtomicXor:
            representation_vector_[node->id()] = PromoteRepresentation(
                AtomicOpType(node->op()).representation());
            break;
          case IrOpcode::kStore:
          case IrOpcode::kProtectedStore:
          case IrOpcode::kStoreTrapOnNull:
          case IrOpcode::kStoreIndirectPointer:
            representation_vector_[node->id()] = PromoteRepresentation(
                StoreRepresentationOf(node->op()).representation());
            break;
          case IrOpcode::kUnalignedStore:
            representation_vector_[node->id()] = PromoteRepresentation(
                UnalignedStoreRepresentationOf(node->op()));
            break;
          case IrOpcode::kHeapConstant:
            representation_vector_[node->id()] =
                MachineRepresentation::kTaggedPointer;
            break;
          case IrOpcode::kNumberConstant:
          case IrOpcode::kChangeBitToTagged:
          case IrOpcode::kIfException:
          case IrOpcode::kOsrValue:
          case IrOpcode::kChangeInt32ToTagged:
          case IrOpcode::kChangeUint32ToTagged:
          case IrOpcode::kBitcastWordToTagged:
          case IrOpcode::kTaggedIndexConstant:
            representation_vector_[node->id()] = MachineRepresentation::kTagged;
            break;
          case IrOpcode::kCompressedHeapConstant:
            representation_vector_[node->id()] =
                MachineRepresentation::kCompressedPointer;
            break;
          case IrOpcode::kExternalConstant:
            representation_vector_[node->id()] =
                MachineType::PointerRepresentation();
            break;
          case IrOpcode::kBitcastTaggedToWord:
          case IrOpcode::kBitcastTaggedToWordForTagAndSmiBits:
            representation_vector_[node->id()] =
                MachineType::PointerRepresentation();
            break;
          case IrOpcode::kBitcastWordToTaggedSigned:
            representation_vector_[node->id()] =
                MachineRepresentation::kTaggedSigned;
            break;
          case IrOpcode::kWord32Equal:
          case IrOpcode::kInt32LessThan:
          case IrOpcode::kInt32LessThanOrEqual:
          case IrOpcode::kUint32LessThan:
          case IrOpcode::kUint32LessThanOrEqual:
          case IrOpcode::kWord64Equal:
          case IrOpcode::kInt64LessThan:
          case IrOpcode::kInt64LessThanOrEqual:
          case IrOpcode::kUint64LessThan:
          case IrOpcode::kUint64LessThanOrEqual:
          case IrOpcode::kFloat32Equal:
          case IrOpcode::kFloat32LessThan:
          case IrOpcode::kFloat32LessThanOrEqual:
          case IrOpcode::kFloat64Equal:
          case IrOpcode::kFloat64LessThan:
          case IrOpcode::kFloat64LessThanOrEqual:
          case IrOpcode::kChangeTaggedToBit:
          case IrOpcode::kStackPointerGreaterThan:
            representation_vector_[node->id()] = MachineRepresentation::kBit;
            break;
#define LABEL(opcode) case IrOpcode::k##opcode:
          case IrOpcode::kTruncateInt64ToInt32:
          case IrOpcode::kTruncateFloat32ToInt32:
          case IrOpcode::kTruncateFloat32ToUint32:
          case IrOpcode::kBitcastFloat32ToInt32:
#if V8_ENABLE_WEBASSEMBLY
          case IrOpcode::kI32x4ExtractLane:
          case IrOpcode::kI16x8ExtractLaneU:
          case IrOpcode::kI16x8ExtractLaneS:
          case IrOpcode::kI8x16ExtractLaneU:
          case IrOpcode::kI8x16ExtractLaneS:
          case IrOpcode::kI8x16BitMask:
#endif  // V8_ENABLE_WEBASSEMBLY
          case IrOpcode::kInt32Constant:
          case IrOpcode::kRelocatableInt32Constant:
          case IrOpcode::kTruncateFloat64ToWord32:
          case IrOpcode::kTruncateFloat64ToUint32:
          case IrOpcode::kChangeFloat64ToInt32:
          case IrOpcode::kChangeFloat64ToUint32:
          case IrOpcode::kRoundFloat64ToInt32:
          case IrOpcode::kFloat64ExtractLowWord32:
          case IrOpcode::kFloat64ExtractHighWord32:
          case IrOpcode::kWord32Popcnt:
            MACHINE_UNOP_32_LIST(LABEL)
            MACHINE_BINOP_32_LIST(LABEL) {
              representation_vector_[node->id()] =
                  MachineRepresentation::kWord32;
            }
            break;
          case IrOpcode::kChangeInt32ToInt64:
          case IrOpcode::kChangeUint32ToUint64:
          case IrOpcode::kBitcastWord32ToWord64:
          case IrOpcode::kInt64Constant:
          case IrOpcode::kRelocatableInt64Constant:
          case IrOpcode::kBitcastFloat64ToInt64:
          case IrOpcode::kChangeFloat64ToInt64:
          case IrOpcode::kChangeFloat64ToUint64:
          case IrOpcode::kTruncateFloat64ToInt64:
          case IrOpcode::kWord64Popcnt:
          case IrOpcode::kWord64Ctz:
          case IrOpcode::kWord64Clz:
            MACHINE_BINOP_64_LIST(LABEL) {
              representation_vector_[node->id()] =
                  MachineRepresentation::kWord64;
            }
            break;
          case IrOpcode::kRoundInt32ToFloat32:
          case IrOpcode::kRoundUint32ToFloat32:
          case IrOpcode::kRoundInt64ToFloat32:
          case IrOpcode::kRoundUint64ToFloat32:
          case IrOpcode::kBitcastInt32ToFloat32:
          case IrOpcode::kFloat32Constant:
          case IrOpcode::kTruncateFloat64ToFloat32:
            MACHINE_FLOAT32_BINOP_LIST(LABEL)
            MACHINE_FLOAT32_UNOP_LIST(LABEL) {
              representation_vector_[node->id()] =
                  MachineRepresentation::kFloat32;
            }
            break;
          case IrOpcode::kRoundInt64ToFloat64:
          case IrOpcode::kRoundUint64ToFloat64:
          case IrOpcode::kBitcastInt64ToFloat64:
          case IrOpcode::kChangeFloat32ToFloat64:
          case IrOpcode::kChangeInt32ToFloat64:
          case IrOpcode::kChangeUint32ToFloat64:
          case IrOpcode::kFloat64InsertLowWord32:
          case IrOpcode::kFloat64InsertHighWord32:
          case IrOpcode::kFloat64Constant:
          case IrOpcode::kFloat64SilenceNaN:
            MACHINE_FLOAT64_BINOP_LIST(LABEL)
            MACHINE_FLOAT64_UNOP_LIST(LABEL) {
              representation_vector_[node->id()] =
                  MachineRepresentation::kFloat64;
            }
            break;
#if V8_ENABLE_WEBASSEMBLY
          case IrOpcode::kI32x4ReplaceLane:
          case IrOpcode::kI32x4Splat:
          case IrOpcode::kI8x16Splat:
          case IrOpcode::kI8x16Eq:
            representation_vector_[node->id()] =
                MachineRepresentation::kSimd128;
            break;
#endif  // V8_ENABLE_WEBASSEMBLY
#undef LABEL
          default:
            break;
        }
      }
    }
  }

  Schedule const* const schedule_;
  Linkage const* const linkage_;
  ZoneVector<MachineRepresentation> representation_vector_;
  BasicBlock* current_block_;
};

class MachineRepresentationChecker {
 public:
  MachineRepresentationChecker(
      Schedule const* const schedule,
      MachineRepresentationInferrer const* const inferrer, bool is_stub,
      const char* name)
      : schedule_(schedule),
        inferrer_(inferrer),
        is_stub_(is_stub),
        name_(name),
        current_block_(nullptr) {}

  void Run() {
    BasicBlockVector const* blocks = schedule_->all_blocks();
    for (BasicBlock* block : *blocks) {
      current_block_ = block;
      for (size_t i = 0; i <= block->NodeCount(); ++i) {
        Node const* node =
            i < block->NodeCount() ? block->NodeAt(i) : block->control_input();
        if (node == nullptr) {
          DCHECK_EQ(block->NodeCount(), i);
          break;
        }
        switch (node->opcode()) {
          case IrOpcode::kCall:
          case IrOpcode::kTailCall:
            CheckCallInputs(node);
            break;
          case IrOpcode::kChangeBitToTagged:
            CHECK_EQ(MachineRepresentation::kBit,
                     inferrer_->GetRepresentation(node->InputAt(0)));
            break;
          case IrOpcode::kChangeTaggedToBit:
            CHECK_EQ(MachineRepresentation::kTagged,
                     inferrer_->GetRepresentation(node->InputAt(0)));
            break;
          case IrOpcode::kRoundInt64ToFloat64:
          case IrOpcode::kRoundUint64ToFloat64:
          case IrOpcode::kRoundInt64ToFloat32:
          case IrOpcode::kRoundUint64ToFloat32:
          case IrOpcode::kTruncateInt64ToInt32:
          case IrOpcode::kBitcastInt64ToFloat64:
          case IrOpcode::kWord64Ctz:
          case IrOpcode::kWord64Clz:
          case IrOpcode::kWord64Popcnt:
            CheckValueInputForInt64Op(node, 0);
            break;
          case IrOpcode::kBitcastWordToTagged:
          case IrOpcode::kBitcastWordToTaggedSigned:
            CheckValueInputRepresentationIs(
                node, 0, MachineType::PointerRepresentation());
            break;
          case IrOpcode::kBitcastTaggedToWord:
          case IrOpcode::kBitcastTaggedToWordForTagAndSmiBits:
            if (COMPRESS_POINTERS_BOOL) {
              CheckValueInputIsCompressedOrTagged(node, 0);
            } else {
              CheckValueInputIsTagged(node, 0);
            }
            break;
          case IrOpcode::kTruncateFloat64ToWord32:
          case IrOpcode::kTruncateFloat64ToUint32:
          case IrOpcode::kTruncateFloat64ToFloat32:
          case IrOpcode::kChangeFloat64ToInt32:
          case IrOpcode::kChangeFloat64ToUint32:
          case IrOpcode::kRoundFloat64ToInt32:
          case IrOpcode::kFloat64ExtractLowWord32:
          case IrOpcode::kFloat64ExtractHighWord32:
          case IrOpcode::kBitcastFloat64ToInt64:
          case IrOpcode::kTryTruncateFloat64ToInt64:
          case IrOpcode::kTryTruncateFloat64ToInt32:
          case IrOpcode::kTryTruncateFloat64ToUint32:
            CheckValueInputForFloat64Op(node, 0);
            break;
          case IrOpcode::kWord64Equal:
            if (Is64() && !COMPRESS_POINTERS_BOOL) {
              CheckValueInputIsTaggedOrPointer(node, 0);
              CheckValueInputIsTaggedOrPointer(node, 1);
              if (!is_stub_) {
                CheckValueInputRepresentationIs(
                    node, 1, inferrer_->GetRepresentation(node->InputAt(0)));
              }
            } else {
              CheckValueInputForInt64Op(node, 0);
              CheckValueInputForInt64Op(node, 1);
            }
            break;
          case IrOpcode::kInt64LessThan:
          case IrOpcode::kInt64LessThanOrEqual:
          case IrOpcode::kUint64LessThan:
          case IrOpcode::kUint64LessThanOrEqual:
            CheckValueInputForInt64Op(node, 0);
            CheckValueInputForInt64Op(node, 1);
            break;
#if V8_ENABLE_WEBASSEMBLY
          case IrOpcode::kI32x4ExtractLane:
          case IrOpcode::kI16x8ExtractLaneU:
          case IrOpcode::kI16x8ExtractLaneS:
          case IrOpcode::kI8x16BitMask:
          case IrOpcode::kI8x16ExtractLaneU:
          case IrOpcode::kI8x16ExtractLaneS:
            CheckValueInputRepresentationIs(node, 0,
                                            MachineRepresentation::kSimd128);
            break;
          case IrOpcode::kI32x4ReplaceLane:
            CheckValueInputRepresentationIs(node, 0,
                                            MachineRepresentation::kSimd128);
            CheckValueInputForInt32Op(node, 1);
            break;
          case IrOpcode::kI32x4Splat:
          case IrOpcode::kI8x16Splat:
            CheckValueInputForInt32Op(node, 0);
            break;
          case IrOpcode::kI8x16Eq:
            CheckValueInputRepresentationIs(node, 0,
                                            MachineRepresentation::kSimd128);
            CheckValueInputRepresentationIs(node, 1,
                                            MachineRepresentation::kSimd128);
            break;
#endif  // V8_ENABLE_WEBASSEMBLY

#define LABEL(opcode) case IrOpcode::k##opcode:
          case IrOpcode::kChangeInt32ToTagged:
          case IrOpcode::kChangeUint32ToTagged:
          case IrOpcode::kChangeInt32ToFloat64:
          case IrOpcode::kChangeUint32ToFloat64:
          case IrOpcode::kRoundInt32ToFloat32:
          case IrOpcode::kRoundUint32ToFloat32:
          case IrOpcode::kBitcastInt32ToFloat32:
          case IrOpcode::kBitcastWord32ToWord64:
          case IrOpcode::kChangeInt32ToInt64:
          case IrOpcode::kChangeUint32ToUint64:
          case IrOpcode::kWord32Popcnt:
            MACHINE_UNOP_32_LIST(LABEL) { CheckValueInputForInt32Op(node, 0); }
            break;
          // Allow tagged pointers to be compared directly, and range checked.
          case IrOpcode::kWord32Equal:
          case IrOpcode::kUint32LessThan:
          case IrOpcode::kUint32LessThanOrEqual:
            if (Is32()) {
              CheckValueInputIsTaggedOrPointer(node, 0);
              CheckValueInputIsTaggedOrPointer(node, 1);
              if (!is_stub_) {
                CheckValueInputRepresentationIs(
                    node, 1, inferrer_->GetRepresentation(node->InputAt(0)));
              }
            } else {
              if (COMPRESS_POINTERS_BOOL) {
                CheckValueInputIsCompressedOrTaggedOrInt32(node, 0);
                CheckValueInputIsCompressedOrTaggedOrInt32(node, 1);
              } else {
                CheckValueIsTaggedOrInt32(node, 0);
                CheckValueIsTaggedOrInt32(node, 1);
              }
            }
            break;

          case IrOpcode::kInt32LessThan:
          case IrOpcode::kInt32LessThanOrEqual:
            MACHINE_BINOP_32_LIST(LABEL) {
              CheckValueInputForInt32Op(node, 0);
              CheckValueInputForInt32Op(node, 1);
            }
            break;
            MACHINE_BINOP_64_LIST(LABEL) {
              CheckValueInputForInt64Op(node, 0);
              CheckValueInputForInt64Op(node, 1);
            }
            break;
          case IrOpcode::kFloat32Equal:
          case IrOpcode::kFloat32LessThan:
          case IrOpcode::kFloat32LessThanOrEqual:
            MACHINE_FLOAT32_BINOP_LIST(LABEL) {
              CheckValueInputForFloat32Op(node, 0);
              CheckValueInputForFloat32Op(node, 1);
            }
            break;
          case IrOpcode::kChangeFloat32ToFloat64:
          case IrOpcode::kTruncateFloat32ToInt32:
          case IrOpcode::kTruncateFloat32ToUint32:
          case IrOpcode::kBitcastFloat32ToInt32:
            MACHINE_FLOAT32_UNOP_LIST(LABEL) {
              CheckValueInputForFloat32Op(node, 0);
            }
            break;
          case IrOpcode::kFloat64Equal:
          case IrOpcode::kFloat64LessThan:
          case IrOpcode::kFloat64LessThanOrEqual:
            MACHINE_FLOAT64_BINOP_LIST(LABEL) {
              CheckValueInputForFloat64Op(node, 0);
              CheckValueInputForFloat64Op(node, 1);
            }
            break;
          case IrOpcode::kFloat64SilenceNaN:
          case IrOpcode::kChangeFloat64ToInt64:
          case IrOpcode::kChangeFloat64ToUint64:
          case IrOpcode::kTruncateFloat64ToInt64:
            MACHINE_FLOAT64_UNOP_LIST(LABEL) {
              CheckValueInputForFloat64Op(node, 0);
            }
            break;
#undef LABEL
          case IrOpcode::kFloat64InsertLowWord32:
          case IrOpcode::kFloat64InsertHighWord32:
            CheckValueInputForFloat64Op(node, 0);
            CheckValueInputForInt32Op(node, 1);
            break;
          case IrOpcode::kInt32PairAdd:
          case IrOpcode::kInt32PairSub:
            for (int j = 0; j < node->op()->ValueInputCount(); ++j) {
              CheckValueInputForInt32Op(node, j);
            }
            break;
          case IrOpcode::kParameter:
          case IrOpcode::kProjection:
            break;
          case IrOpcode::kAbortCSADcheck:
            CheckValueInputIsTagged(node, 0);
            break;
          case IrOpcode::kLoad:
          case IrOpcode::kUnalignedLoad:
          case IrOpcode::kLoadImmutable:
          case IrOpcode::kWord32AtomicLoad:
          case IrOpcode::kWord32AtomicPairLoad:
          case IrOpcode::kWord64AtomicLoad:
            CheckValueInputIsTaggedOrPointer(node, 0);
            CheckValueInputRepresentationIs(
                node, 1, MachineType::PointerRepresentation());
            break;
          case IrOpcode::kWord32AtomicPairAdd:
          case IrOpcode::kWord32AtomicPairSub:
          case IrOpcode::kWord32AtomicPairAnd:
          case IrOpcode::kWord32AtomicPairOr:
          case IrOpcode::kWord32AtomicPairXor:
          case IrOpcode::kWord32AtomicPairStore:
          case IrOpcode::kWord32AtomicPairExchange:
            CheckValueInputRepresentationIs(node, 3,
                                            MachineRepresentation::kWord32);
            [[fallthrough]];
          case IrOpcode::kStore:
          case IrOpcode::kStoreIndirectPointer:
          case IrOpcode::kUnalignedStore:
          case IrOpcode::kWord32AtomicStore:
          case IrOpcode::kWord32AtomicExchange:
          case IrOpcode::kWord32AtomicAdd:
          case IrOpcode::kWord32AtomicSub:
          case IrOpcode::kWord32AtomicAnd:
          case IrOpcode::kWord32AtomicOr:
          case IrOpcode::kWord32AtomicXor:
          case IrOpcode::kWord64AtomicStore:
          case IrOpcode::kWord64AtomicExchange:
          case IrOpcode::kWord64AtomicAdd:
          case IrOpcode::kWord64AtomicSub:
          case IrOpcode::kWord64AtomicAnd:
          case IrOpcode::kWord64AtomicOr:
          case IrOpcode::kWord64AtomicXor:
            CheckValueInputIsTaggedOrPointer(node, 0);
            CheckValueInputRepresentationIs(
                node, 1, MachineType::PointerRepresentation());
            switch (inferrer_->GetRepresentation(node)) {
              case MachineRepresentation::kTagged:
              case MachineRepresentation::kTaggedPointer:
              case MachineRepresentation::kTaggedSigned:
              case MachineRepresentation::kIndirectPointer:
                if (COMPRESS_POINTERS_BOOL &&
                    ((node->opcode() == IrOpcode::kStore &&
                      IsAnyTagged(StoreRepresentationOf(node->op())
                                      .representation())) ||
                     (node->opcode() == IrOpcode::kWord32AtomicStore &&
                      IsAnyTagged(AtomicStoreParametersOf(node->op())
                                      .representation())))) {
                  CheckValueInputIsCompressedOrTagged(node, 2);
                } else {
                  CheckValueInputIsTagged(node, 2);
                }
                break;
              default:
                CheckValueInputRepresentationIs(
                    node, 2, inferrer_->GetRepresentation(node));
            }
            break;
          case IrOpcode::kStorePair: {
            CheckValueInputIsTaggedOrPointer(node, 0);
            CheckValueInputRepresentationIs(
                node, 1, MachineType::PointerRepresentation());
            auto CheckInput = [&](MachineRepresentation rep, int input) {
              switch (rep) {
                case MachineRepresentation::kTagged:
                case MachineRepresentation::kTaggedPointer:
                case MachineRepresentation::kTaggedSigned:
                case MachineRepresentation::kIndirectPointer:
                  if (COMPRESS_POINTERS_BOOL) {
                    CheckValueInputIsCompressedOrTagged(node, input);
                  } else {
                    CheckValueInputIsTagged(node, input);
                  }
                  break;
                default:
                  CheckValueInputRepresentationIs(node, input, rep);
              }
            };
            auto rep = StorePairRepresentationOf(node->op());
            CHECK_GE(ElementSizeLog2Of(rep.first.representation()), 2);
            CHECK_EQ(ElementSizeLog2Of(rep.first.representation()),
                     ElementSizeLog2Of(rep.second.representation()));
            CheckInput(rep.first.representation(), 2);
            CheckInput(rep.second.representation(), 3);
            break;
          }
          case IrOpcode::kWord32AtomicPairCompareExchange:
            CheckValueInputRepresentationIs(node, 4,
                                            MachineRepresentation::kWord32);
            CheckValueInputRepresentationIs(node, 5,
                                            MachineRepresentation::kWord32);
            [[fallthrough]];
          case IrOpcode::kWord32AtomicCompareExchange:
          case IrOpcode::kWord64AtomicCompareExchange:
            CheckValueInputIsTaggedOrPointer(node, 0);
            CheckValueInputRepresentationIs(
                node, 1, MachineType::PointerRepresentation());
            switch (inferrer_->GetRepresentation(node)) {
              case MachineRepresentation::kTagged:
              case MachineRepresentation::kTaggedPointer:
              case MachineRepresentation::kTaggedSigned:
                CheckValueInputIsTagged(node, 2);
                CheckValueInputIsTagged(node, 3);
                break;
              default:
                CheckValueInputRepresentationIs(
                    node, 2, inferrer_->GetRepresentation(node));
                CheckValueInputRepresentationIs(
                    node, 3, inferrer_->GetRepresentation(node));
            }
            break;
          case IrOpcode::kPhi:
            switch (inferrer_->GetRepresentation(node)) {
              case MachineRepresentation::kTagged:
              case MachineRepresentation::kTaggedPointer:
                for (int j = 0; j < node->op()->ValueInputCount(); ++j) {
                  CheckValueInputIsTagged(node, j);
                }
                break;
              case MachineRepresentation::kTaggedSigned:
                for (int j = 0; j < node->op()->ValueInputCount(); ++j) {
                  if (COMPRESS_POINTERS_BOOL) {
                    CheckValueInputIsCompressedOrTagged(node, j);
                  } else {
                    CheckValueInputIsTagged(node, j);
                  }
                }
                break;
              case MachineRepresentation::kCompressed:
              case MachineRepresentation::kCompressedPointer:
                for (int j = 0; j < node->op()->ValueInputCount(); ++j) {
                  CheckValueInputIsCompressedOrTagged(node, j);
                }
                break;
              case MachineRepresentation::kWord32:
                for (int j = 0; j < node->op()->ValueInputCount(); ++j) {
                  CheckValueInputForInt32Op(node, j);
                }
                break;
              default:
                for (int j = 0; j < node->op()->ValueInputCount(); ++j) {
                  CheckValueInputRepresentationIs(
                      node, j, inferrer_->GetRepresentation(node));
                }
                break;
            }
            break;
          case IrOpcode::kBranch:
          case IrOpcode::kSwitch:
            CheckValueInputForInt32Op(node, 0);
            break;
          case IrOpcode::kReturn: {
            // TODO(ishell): enable once the pop count parameter type becomes
            // MachineType::PointerRepresentation(). Currently it's int32 or
            // word-size.
            // CheckValueInputRepresentationIs(
            //     node, 0, MachineType::PointerRepresentation());  // Pop count
            size_t return_count = inferrer_->call_descriptor()->ReturnCount();
            for (size_t j = 0; j < return_count; j++) {
              MachineType type = inferrer_->call_descriptor()->GetReturnType(j);
              int input_index = static_cast<int>(j + 1);
              switch (type.representation()) {
                case MachineRepresentation::kTagged:
                case MachineRepresentation::kTaggedPointer:
                case MachineRepresentation::kTaggedSigned:
                  CheckValueInputIsTagged(node, input_index);
                  break;
                case MachineRepresentation::kWord32:
                  CheckValueInputForInt32Op(node, input_index);
                  break;
                default:
                  CheckValueInputRepresentationIs(node, input_index,
                                                  type.representation());
                  break;
              }
            }
            break;
          }
#if V8_ENABLE_WEBASSEMBLY
          case IrOpcode::kSetStackPointer:
#endif  // V8_ENABLE_WEBASSEMBLY
          case IrOpcode::kStackPointerGreaterThan:
            CheckValueInputRepresentationIs(
                node, 0, MachineType::PointerRepresentation());
            break;
          case IrOpcode::kThrow:
          case IrOpcode::kTypedStateValues:
          case IrOpcode::kFrameState:
          case IrOpcode::kStaticAssert:
            break;
          default:
            if (node->op()->ValueInputCount() != 0) {
              std::stringstream str;
              str << "Node #" << node->id() << ":" << *node->op()
                  << " in the machine graph is not being checked.";
              PrintDebugHelp(str, node);
              FATAL("%s", str.str().c_str());
            }
            break;
        }
      }
    }
  }

 private:
  static bool Is32() {
    return MachineType::PointerRepresentation() ==
           MachineRepresentation::kWord32;
  }
  static bool Is64() {
    return MachineType::PointerRepresentation() ==
           MachineRepresentation::kWord64;
  }

  void CheckValueInputRepresentationIs(Node const* node, int index,
                                       MachineRepresentation representation) {
    Node const* input = node->InputAt(index);
    MachineRepresentation input_representation =
        inferrer_->GetRepresentation(input);
    if (input_representation != representation) {
      std::stringstream str;
      str << "TypeError: node #" << node->id() << ":" << *node->op()
          << " uses node #" << input->id() << ":" << *input->op() << ":"
          << input_representation << " which doesn't have a " << representation
          << " representation.";
      PrintDebugHelp(str, node);
      FATAL("%s", str.str().c_str());
    }
  }

  void CheckValueInputIsTagged(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    switch (inferrer_->GetRepresentation(input)) {
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTaggedSigned:
        return;
      default:
        break;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op()
        << " which doesn't have a tagged representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckValueInputIsCompressedOrTagged(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    switch (inferrer_->GetRepresentation(input)) {
      case MachineRepresentation::kCompressed:
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTaggedSigned:
        return;
      default:
        break;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op()
        << " which doesn't have a compressed or tagged representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckValueInputIsCompressedOrTaggedOrInt32(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    switch (inferrer_->GetRepresentation(input)) {
      case MachineRepresentation::kCompressed:
      case MachineRepresentation::kCompressedPointer:
        return;
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTaggedSigned:
        return;
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        return;
      default:
        break;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op()
        << " which doesn't have a compressed, tagged, or int32 representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckValueInputIsTaggedOrPointer(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    MachineRepresentation rep = inferrer_->GetRepresentation(input);
    switch (rep) {
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTaggedSigned:
        return;
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        if (Is32()) {
          return;
        }
        break;
      case MachineRepresentation::kWord64:
        if (Is64()) {
          return;
        }
        break;
      default:
        break;
    }
    switch (node->opcode()) {
      case IrOpcode::kLoad:
      case IrOpcode::kProtectedLoad:
      case IrOpcode::kLoadTrapOnNull:
      case IrOpcode::kUnalignedLoad:
      case IrOpcode::kLoadImmutable:
        if (rep == MachineRepresentation::kCompressed ||
            rep == MachineRepresentation::kCompressedPointer) {
          if (DECOMPRESS_POINTER_BY_ADDRESSING_MODE && index == 0) {
            return;
          }
        }
        break;
      default:
        break;
    }
    if (inferrer_->GetRepresentation(input) !=
        MachineType::PointerRepresentation()) {
      std::ostringstream str;
      str << "TypeError: node #" << node->id() << ":" << *node->op()
          << " uses node #" << input->id() << ":" << *input->op()
          << " which doesn't have a tagged or pointer representation.";
      PrintDebugHelp(str, node);
      FATAL("%s", str.str().c_str());
    }
  }

  void CheckValueInputForInt32Op(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    switch (inferrer_->GetRepresentation(input)) {
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        return;
      case MachineRepresentation::kNone: {
        std::ostringstream str;
        str << "TypeError: node #" << input->id() << ":" << *input->op()
            << " is untyped.";
        PrintDebugHelp(str, node);
        FATAL("%s", str.str().c_str());
      }
      default:
        break;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op()
        << " which doesn't have an int32-compatible representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckValueIsTaggedOrInt32(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    switch (inferrer_->GetRepresentation(input)) {
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        return;
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kTaggedPointer:
        return;
      default:
        break;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op()
        << " which doesn't have a tagged or int32-compatible "
           "representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckValueInputForInt64Op(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    MachineRepresentation input_representation =
        inferrer_->GetRepresentation(input);
    switch (input_representation) {
      case MachineRepresentation::kWord64:
        return;
      case MachineRepresentation::kNone: {
        std::ostringstream str;
        str << "TypeError: node #" << input->id() << ":" << *input->op()
            << " is untyped.";
        PrintDebugHelp(str, node);
        FATAL("%s", str.str().c_str());
      }

      default:
        break;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op() << ":"
        << input_representation
        << " which doesn't have a kWord64 representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckValueInputForFloat32Op(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    if (MachineRepresentation::kFloat32 ==
        inferrer_->GetRepresentation(input)) {
      return;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op()
        << " which doesn't have a kFloat32 representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckValueInputForFloat64Op(Node const* node, int index) {
    Node const* input = node->InputAt(index);
    if (MachineRepresentation::kFloat64 ==
        inferrer_->GetRepresentation(input)) {
      return;
    }
    std::ostringstream str;
    str << "TypeError: node #" << node->id() << ":" << *node->op()
        << " uses node #" << input->id() << ":" << *input->op()
        << " which doesn't have a kFloat64 representation.";
    PrintDebugHelp(str, node);
    FATAL("%s", str.str().c_str());
  }

  void CheckCallInputs(Node const* node) {
    auto call_descriptor = CallDescriptorOf(node->op());
    std::ostringstream str;
    bool should_log_error = false;
    for (size_t i = 0; i < call_descriptor->InputCount(); ++i) {
      Node const* input = node->InputAt(static_cast<int>(i));
      MachineRepresentation const input_type =
          inferrer_->GetRepresentation(input);
      MachineRepresentation const expected_input_type =
          call_descriptor->GetInputType(i).representation();
      if (!IsCompatible(expected_input_type, input_type)) {
        if (!should_log_error) {
          should_log_error = true;
          str << "TypeError: node #" << node->id() << ":" << *node->op()
              << " has wrong type for:" << std::endl;
        } else {
          str << std::endl;
        }
        str << " * input " << i << " (" << input->id() << ":" << *input->op()
            << ") has a " << input_type
            << " representation (expected: " << expected_input_type << ").";
      }
    }
    if (should_log_error) {
      PrintDebugHelp(str, node);
      FATAL("%s", str.str().c_str());
    }
  }

  bool IsCompatible(MachineRepresentation expected,
                    MachineRepresentation actual) {
    switch (expected) {
      case MachineRepresentation::kTagged:
        return IsAnyTagged(actual);
      case MachineRepresentation::kCompressed:
        return IsAnyCompressed(actual);
      case MachineRepresentation::kMapWord:
      case MachineRepresentation::kTaggedSigned:
      case MachineRepresentation::kTaggedPointer:
        // TODO(turbofan): At the moment, the machine graph doesn't contain
        // reliable information if a node is kTaggedSigned, kTaggedPointer or
        // kTagged, and often this is context-dependent. We should at least
        // check for obvious violations: kTaggedSigned where we expect
        // kTaggedPointer and the other way around, but at the moment, this
        // happens in dead code.
        return IsAnyTagged(actual);
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kProtectedPointer:
      case MachineRepresentation::kIndirectPointer:
      case MachineRepresentation::kSandboxedPointer:
      case MachineRepresentation::kFloat16:
      case MachineRepresentation::kFloat32:
      case MachineRepresentation::kFloat64:
      case MachineRepresentation::kSimd128:
      case MachineRepresentation::kSimd256:
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord64:
        return expected == actual;
      case MachineRepresentation::kWord32:
        return (actual == MachineRepresentation::kBit ||
                actual == MachineRepresentation::kWord8 ||
                actual == MachineRepresentation::kWord16 ||
                actual == MachineRepresentation::kWord32);
      case MachineRepresentation::kNone:
        UNREACHABLE();
    }
    return false;
  }

  void PrintDebugHelp(std::ostream& out, Node const* node) {
    if (DEBUG_BOOL) {
      out << "\n#     Current block: " << *current_block_;
      out << "\n#\n#     Specify option --csa-trap-on-node=" << name_ << ","
          << node->id() << " for debugging.";
    }
  }

  Schedule const* const schedule_;
  MachineRepresentationInferrer const* const inferrer_;
  bool is_stub_;
  const char* name_;
  BasicBlock* current_block_;
};

}  // namespace

void MachineGraphVerifier::Run(Graph* graph, Schedule const* const schedule,
                               Linkage* linkage, bool is_stub, const char* name,
                               Zone* temp_zone) {
  MachineRepresentationInferrer representation_inferrer(schedule, graph,
                                                        linkage, temp_zone);
  MachineRepresentationChecker checker(schedule, &representation_inferrer,
                                       is_stub, name);
  checker.Run();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
