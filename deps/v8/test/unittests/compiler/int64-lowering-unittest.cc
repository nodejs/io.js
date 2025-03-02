// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/int64-lowering.h"

#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/signature.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

#if V8_TARGET_ARCH_32_BIT

using testing::AllOf;
using testing::Capture;
using testing::CaptureEq;

namespace v8 {
namespace internal {
namespace compiler {

class Int64LoweringTest : public GraphTest {
 public:
  Int64LoweringTest()
      : GraphTest(),
        machine_(zone(), MachineRepresentation::kWord32,
                 MachineOperatorBuilder::Flag::kAllOptionalOps),
        simplified_(zone()) {
    value_[0] = 0x1234567890ABCDEF;
    value_[1] = 0x1EDCBA098765432F;
    value_[2] = 0x1133557799886644;
  }

  MachineOperatorBuilder* machine() { return &machine_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

  void LowerGraph(Node* node, Signature<MachineRepresentation>* signature) {
    Node* zero = graph()->NewNode(common()->Int32Constant(0));
    Node* ret = graph()->NewNode(common()->Return(), zero, node,
                                 graph()->start(), graph()->start());
    NodeProperties::MergeControlToEnd(graph(), common(), ret);

    Int64Lowering lowering(graph(), machine(), common(), simplified(), zone(),
                           signature);
    lowering.LowerGraph();
  }

  void LowerGraphWithSpecialCase(Node* node, MachineRepresentation rep) {
    Node* zero = graph()->NewNode(common()->Int32Constant(0));
    Node* ret = graph()->NewNode(common()->Return(), zero, node,
                                 graph()->start(), graph()->start());
    NodeProperties::MergeControlToEnd(graph(), common(), ret);

    // Create a signature for the outer wasm<>js call; for these tests we focus
    // on lowering the special cases rather than the wrapper node at the
    // JavaScript boundaries.
    Signature<MachineRepresentation>::Builder sig_builder(zone(), 1, 0);
    sig_builder.AddReturn(rep);

    Int64Lowering lowering(graph(), machine(), common(), simplified(), zone(),
                           sig_builder.Get());
    lowering.LowerGraph();
  }

  void LowerGraph(Node* node, MachineRepresentation return_type,
                  MachineRepresentation rep = MachineRepresentation::kWord32,
                  int num_params = 0) {
    Signature<MachineRepresentation>::Builder sig_builder(zone(), 1,
                                                          num_params);
    sig_builder.AddReturn(return_type);
    for (int i = 0; i < num_params; i++) {
      sig_builder.AddParam(rep);
    }
    LowerGraph(node, sig_builder.Get());
  }

  void CompareCallDescriptors(const CallDescriptor* lhs,
                              const CallDescriptor* rhs) {
    EXPECT_THAT(lhs->CalleeSavedFPRegisters(), rhs->CalleeSavedFPRegisters());
    EXPECT_THAT(lhs->CalleeSavedRegisters(), rhs->CalleeSavedRegisters());
    EXPECT_THAT(lhs->FrameStateCount(), rhs->FrameStateCount());
    EXPECT_THAT(lhs->InputCount(), rhs->InputCount());
    for (size_t i = 0; i < lhs->InputCount(); i++) {
      EXPECT_THAT(lhs->GetInputLocation(i), rhs->GetInputLocation(i));
      EXPECT_THAT(lhs->GetInputType(i), rhs->GetInputType(i));
    }
    EXPECT_THAT(lhs->ReturnCount(), rhs->ReturnCount());
    for (size_t i = 0; i < lhs->ReturnCount(); i++) {
      EXPECT_THAT(lhs->GetReturnLocation(i), rhs->GetReturnLocation(i));
      EXPECT_THAT(lhs->GetReturnType(i), rhs->GetReturnType(i));
    }
    EXPECT_THAT(lhs->flags(), rhs->flags());
    EXPECT_THAT(lhs->kind(), rhs->kind());
  }

  int64_t value(int i) { return value_[i]; }

  int32_t low_word_value(int i) {
    return static_cast<int32_t>(value_[i] & 0xFFFFFFFF);
  }

  int32_t high_word_value(int i) {
    return static_cast<int32_t>(value_[i] >> 32);
  }

  void TestComparison(
      const Operator* op,
      Matcher<Node*> (*high_word_matcher)(const Matcher<Node*>& lhs_matcher,
                                          const Matcher<Node*>& rhs_matcher),
      Matcher<Node*> (*low_word_matcher)(const Matcher<Node*>& lhs_matcher,
                                         const Matcher<Node*>& rhs_matcher)) {
    LowerGraph(
        graph()->NewNode(op, Int64Constant(value(0)), Int64Constant(value(1))),
        MachineRepresentation::kWord32);
    EXPECT_THAT(
        graph()->end()->InputAt(1),
        IsReturn(IsWord32Or(
                     high_word_matcher(IsInt32Constant(high_word_value(0)),
                                       IsInt32Constant(high_word_value(1))),
                     IsWord32And(
                         IsWord32Equal(IsInt32Constant(high_word_value(0)),
                                       IsInt32Constant(high_word_value(1))),
                         low_word_matcher(IsInt32Constant(low_word_value(0)),
                                          IsInt32Constant(low_word_value(1))))),
                 start(), start()));
  }

 private:
  MachineOperatorBuilder machine_;
  SimplifiedOperatorBuilder simplified_;
  int64_t value_[3];
};

TEST_F(Int64LoweringTest, Int64Constant) {
  LowerGraph(Int64Constant(value(0)), MachineRepresentation::kWord64);
  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsInt32Constant(low_word_value(0)),
                        IsInt32Constant(high_word_value(0)), start(), start()));
}

#if defined(V8_TARGET_LITTLE_ENDIAN)
#define LOAD_VERIFY(kLoad)                                                     \
  Matcher<Node*> high_word_load_matcher =                                      \
      Is##kLoad(MachineType::Int32(), IsInt32Constant(base),                   \
                IsInt32Constant(index + 4), start(), start());                 \
                                                                               \
  EXPECT_THAT(                                                                 \
      graph()->end()->InputAt(1),                                              \
      IsReturn2(                                                               \
          Is##kLoad(MachineType::Int32(), IsInt32Constant(base),               \
                    IsInt32Constant(index),                                    \
                    AllOf(CaptureEq(&high_word_load), high_word_load_matcher), \
                    start()),                                                  \
          AllOf(CaptureEq(&high_word_load), high_word_load_matcher), start(),  \
          start()));
#elif defined(V8_TARGET_BIG_ENDIAN)
#define LOAD_VERIFY(kLoad)                                                     \
  Matcher<Node*> high_word_load_matcher =                                      \
      Is##kLoad(MachineType::Int32(), IsInt32Constant(base),                   \
                IsInt32Constant(index), start(), start());                     \
                                                                               \
  EXPECT_THAT(                                                                 \
      graph()->end()->InputAt(1),                                              \
      IsReturn2(                                                               \
          Is##kLoad(MachineType::Int32(), IsInt32Constant(base),               \
                    IsInt32Constant(index + 4),                                \
                    AllOf(CaptureEq(&high_word_load), high_word_load_matcher), \
                    start()),                                                  \
          AllOf(CaptureEq(&high_word_load), high_word_load_matcher), start(),  \
          start()));
#endif

#define INT64_LOAD_LOWERING(kLoad, param, builder)                          \
  int32_t base = 0x1234;                                                    \
  int32_t index = 0x5678;                                                   \
                                                                            \
  LowerGraph(graph()->NewNode(builder()->kLoad(param), Int32Constant(base), \
                              Int32Constant(index), start(), start()),      \
             MachineRepresentation::kWord64);                               \
                                                                            \
  Capture<Node*> high_word_load;                                            \
  LOAD_VERIFY(kLoad)

TEST_F(Int64LoweringTest, Int64Load) {
  INT64_LOAD_LOWERING(Load, MachineType::Int64(), machine);
}

TEST_F(Int64LoweringTest, UnalignedInt64Load) {
  INT64_LOAD_LOWERING(UnalignedLoad, MachineType::Int64(), machine);
}

TEST_F(Int64LoweringTest, Int64LoadFromObject) {
  INT64_LOAD_LOWERING(LoadFromObject,
                      ObjectAccess(MachineType::Int64(), kNoWriteBarrier),
                      simplified);
}

TEST_F(Int64LoweringTest, Int64LoadImmutable) {
  int32_t base = 0x1234;
  int32_t index = 0x5678;

  LowerGraph(graph()->NewNode(machine()->LoadImmutable(MachineType::Int64()),
                              Int32Constant(base), Int32Constant(index)),
             MachineRepresentation::kWord64);

  Capture<Node*> high_word_load;

#if defined(V8_TARGET_LITTLE_ENDIAN)
  Matcher<Node*> high_word_load_matcher = IsLoadImmutable(
      MachineType::Int32(), IsInt32Constant(base), IsInt32Constant(index + 4));

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsLoadImmutable(MachineType::Int32(), IsInt32Constant(base),
                                IsInt32Constant(index)),
                AllOf(CaptureEq(&high_word_load), high_word_load_matcher),
                start(), start()));
#elif defined(V8_TARGET_BIG_ENDIAN)
  Matcher<Node*> high_word_load_matcher = IsLoadImmutable(
      MachineType::Int32(), IsInt32Constant(base), IsInt32Constant(index));

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsLoadImmutable(MachineType::Int32(), IsInt32Constant(base),
                                IsInt32Constant(index + 4)),
                AllOf(CaptureEq(&high_word_load), high_word_load_matcher),
                start(), start()));
#endif
}

#if defined(V8_TARGET_LITTLE_ENDIAN)
#define STORE_VERIFY(kStore, kRep)                                             \
  EXPECT_THAT(                                                                 \
      graph()->end()->InputAt(1),                                              \
      IsReturn(IsInt32Constant(return_value),                                  \
               Is##kStore(kRep, IsInt32Constant(base), IsInt32Constant(index), \
                          IsInt32Constant(low_word_value(0)),                  \
                          Is##kStore(kRep, IsInt32Constant(base),              \
                                     IsInt32Constant(index + 4),               \
                                     IsInt32Constant(high_word_value(0)),      \
                                     start(), start()),                        \
                          start()),                                            \
               start()));
#elif defined(V8_TARGET_BIG_ENDIAN)
#define STORE_VERIFY(kStore, kRep)                                             \
  EXPECT_THAT(                                                                 \
      graph()->end()->InputAt(1),                                              \
      IsReturn(IsInt32Constant(return_value),                                  \
               Is##kStore(                                                     \
                   kRep, IsInt32Constant(base), IsInt32Constant(index + 4),    \
                   IsInt32Constant(low_word_value(0)),                         \
                   Is##kStore(                                                 \
                       kRep, IsInt32Constant(base), IsInt32Constant(index),    \
                       IsInt32Constant(high_word_value(0)), start(), start()), \
                   start()),                                                   \
               start()));
#endif

#define INT64_STORE_LOWERING(kStore, kRep32, kRep64, builder)                \
  int32_t base = 1111;                                                       \
  int32_t index = 2222;                                                      \
  int32_t return_value = 0x5555;                                             \
                                                                             \
  Signature<MachineRepresentation>::Builder sig_builder(zone(), 1, 0);       \
  sig_builder.AddReturn(MachineRepresentation::kWord32);                     \
                                                                             \
  Node* store = graph()->NewNode(builder()->kStore(kRep64),                  \
                                 Int32Constant(base), Int32Constant(index),  \
                                 Int64Constant(value(0)), start(), start()); \
                                                                             \
  Node* zero = graph()->NewNode(common()->Int32Constant(0));                 \
  Node* ret = graph()->NewNode(common()->Return(), zero,                     \
                               Int32Constant(return_value), store, start()); \
                                                                             \
  NodeProperties::MergeControlToEnd(graph(), common(), ret);                 \
                                                                             \
  Int64Lowering lowering(graph(), machine(), common(), simplified(), zone(), \
                         sig_builder.Get());                                 \
  lowering.LowerGraph();                                                     \
                                                                             \
  STORE_VERIFY(kStore, kRep32)

TEST_F(Int64LoweringTest, Int64Store) {
  const StoreRepresentation rep64(MachineRepresentation::kWord64,
                                  WriteBarrierKind::kNoWriteBarrier);
  const StoreRepresentation rep32(MachineRepresentation::kWord32,
                                  WriteBarrierKind::kNoWriteBarrier);
  INT64_STORE_LOWERING(Store, rep32, rep64, machine);
}

TEST_F(Int64LoweringTest, Int32Store) {
  const StoreRepresentation rep32(MachineRepresentation::kWord32,
                                  WriteBarrierKind::kNoWriteBarrier);
  int32_t base = 1111;
  int32_t index = 2222;
  int32_t return_value = 0x5555;

  Signature<MachineRepresentation>::Builder sig_builder(zone(), 1, 0);
  sig_builder.AddReturn(MachineRepresentation::kWord32);

  Node* store = graph()->NewNode(machine()->Store(rep32), Int32Constant(base),
                                 Int32Constant(index), Int64Constant(value(0)),
                                 start(), start());

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero,
                               Int32Constant(return_value), store, start());

  NodeProperties::MergeControlToEnd(graph(), common(), ret);

  Int64Lowering lowering(graph(), machine(), common(), simplified(), zone(),
                         sig_builder.Get());
  lowering.LowerGraph();

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn(IsInt32Constant(return_value),
               IsStore(rep32, IsInt32Constant(base), IsInt32Constant(index),
                       IsInt32Constant(low_word_value(0)), start(), start()),
               start()));
}

TEST_F(Int64LoweringTest, Int64UnalignedStore) {
  const UnalignedStoreRepresentation rep64(MachineRepresentation::kWord64);
  const UnalignedStoreRepresentation rep32(MachineRepresentation::kWord32);
  INT64_STORE_LOWERING(UnalignedStore, rep32, rep64, machine);
}

TEST_F(Int64LoweringTest, Int64StoreToObject) {
  const ObjectAccess access64(MachineType::Int64(), kNoWriteBarrier);
  const ObjectAccess access32(MachineType::Int32(), kNoWriteBarrier);
  INT64_STORE_LOWERING(StoreToObject, access32, access64, simplified);
}

TEST_F(Int64LoweringTest, Int64And) {
  LowerGraph(graph()->NewNode(machine()->Word64And(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);
  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsWord32And(IsInt32Constant(low_word_value(0)),
                                    IsInt32Constant(low_word_value(1))),
                        IsWord32And(IsInt32Constant(high_word_value(0)),
                                    IsInt32Constant(high_word_value(1))),
                        start(), start()));
}

TEST_F(Int64LoweringTest, TruncateInt64ToInt32) {
  LowerGraph(graph()->NewNode(machine()->TruncateInt64ToInt32(),
                              Int64Constant(value(0))),
             MachineRepresentation::kWord32);
  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn(IsInt32Constant(low_word_value(0)), start(), start()));
}

TEST_F(Int64LoweringTest, Parameter) {
  LowerGraph(Parameter(1), MachineRepresentation::kWord64,
             MachineRepresentation::kWord64, 1);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsParameter(1), IsParameter(2), start(), start()));
}

TEST_F(Int64LoweringTest, Parameter2) {
  Signature<MachineRepresentation>::Builder sig_builder(zone(), 1, 5);
  sig_builder.AddReturn(MachineRepresentation::kWord32);

  sig_builder.AddParam(MachineRepresentation::kWord32);
  sig_builder.AddParam(MachineRepresentation::kWord64);
  sig_builder.AddParam(MachineRepresentation::kFloat64);
  sig_builder.AddParam(MachineRepresentation::kWord64);
  sig_builder.AddParam(MachineRepresentation::kWord32);

  int start_parameter = start()->op()->ValueOutputCount();
  LowerGraph(Parameter(5), sig_builder.Get());

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn(IsParameter(7), start(), start()));
  // The parameter of the start node should increase by 2, because we lowered
  // two parameter nodes.
  EXPECT_THAT(start()->op()->ValueOutputCount(), start_parameter + 2);
}

TEST_F(Int64LoweringTest, ParameterWithJSContextParam) {
  Signature<MachineRepresentation>::Builder sig_builder(zone(), 0, 2);
  sig_builder.AddParam(MachineRepresentation::kWord64);
  sig_builder.AddParam(MachineRepresentation::kWord64);

  auto sig = sig_builder.Get();

  Node* js_context = graph()->NewNode(
      common()->Parameter(Linkage::GetJSCallContextParamIndex(
                              static_cast<int>(sig->parameter_count()) + 1),
                          "%context"),
      start());
  LowerGraph(js_context, sig);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn(js_context, start(), start()));
}

TEST_F(Int64LoweringTest, ParameterWithJSClosureParam) {
  Signature<MachineRepresentation>::Builder sig_builder(zone(), 0, 2);
  sig_builder.AddParam(MachineRepresentation::kWord64);
  sig_builder.AddParam(MachineRepresentation::kWord64);

  auto sig = sig_builder.Get();

  Node* js_closure = graph()->NewNode(
      common()->Parameter(Linkage::kJSCallClosureParamIndex, "%closure"),
      start());
  LowerGraph(js_closure, sig);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn(js_closure, start(), start()));
}

// The following tests are only valid in 32-bit platforms, due to one of these
// two assumptions:
// - Pointers are 32 bit and therefore pointers do not get lowered.
// - 64-bit rol/ror/clz/ctz instructions have a control input.
TEST_F(Int64LoweringTest, CallI64Return) {
  int32_t function = 0x9999;
  Node* context_address = Int32Constant(0);

  wasm::FunctionSig::Builder sig_builder(zone(), 1, 0);
  sig_builder.AddReturn(wasm::kWasmI64);

  auto call_descriptor =
      compiler::GetWasmCallDescriptor(zone(), sig_builder.Get());

  LowerGraph(
      graph()->NewNode(common()->Call(call_descriptor), Int32Constant(function),
                       context_address, start(), start()),
      MachineRepresentation::kWord64);

  Capture<Node*> call;
  Matcher<Node*> call_matcher =
      IsCall(testing::_, IsInt32Constant(function), start(), start());

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsProjection(0, AllOf(CaptureEq(&call), call_matcher)),
                        IsProjection(1, AllOf(CaptureEq(&call), call_matcher)),
                        start(), start()));

  CompareCallDescriptors(
      CallDescriptorOf(
          graph()->end()->InputAt(1)->InputAt(1)->InputAt(0)->op()),
      compiler::GetI32WasmCallDescriptor(zone(), call_descriptor));
}

TEST_F(Int64LoweringTest, CallI64Parameter) {
  int32_t function = 0x9999;
  Node* context_address = Int32Constant(0);

  wasm::FunctionSig::Builder sig_builder(zone(), 1, 3);
  sig_builder.AddReturn(wasm::kWasmI32);
  sig_builder.AddParam(wasm::kWasmI64);
  sig_builder.AddParam(wasm::kWasmI32);
  sig_builder.AddParam(wasm::kWasmI64);

  auto call_descriptor =
      compiler::GetWasmCallDescriptor(zone(), sig_builder.Get());

  LowerGraph(
      graph()->NewNode(common()->Call(call_descriptor), Int32Constant(function),
                       context_address, Int64Constant(value(0)),
                       Int32Constant(low_word_value(1)),
                       Int64Constant(value(2)), start(), start()),
      MachineRepresentation::kWord32);

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn(IsCall(testing::_, IsInt32Constant(function), context_address,
                      IsInt32Constant(low_word_value(0)),
                      IsInt32Constant(high_word_value(0)),
                      IsInt32Constant(low_word_value(1)),
                      IsInt32Constant(low_word_value(2)),
                      IsInt32Constant(high_word_value(2)), start(), start()),
               start(), start()));

  CompareCallDescriptors(
      CallDescriptorOf(graph()->end()->InputAt(1)->InputAt(1)->op()),
      compiler::GetI32WasmCallDescriptor(zone(), call_descriptor));
}

TEST_F(Int64LoweringTest, Int64Add) {
  LowerGraph(graph()->NewNode(machine()->Int64Add(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);

  Capture<Node*> add;
  Matcher<Node*> add_matcher = IsInt32PairAdd(
      IsInt32Constant(low_word_value(0)), IsInt32Constant(high_word_value(0)),
      IsInt32Constant(low_word_value(1)), IsInt32Constant(high_word_value(1)));

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsProjection(0, AllOf(CaptureEq(&add), add_matcher)),
                        IsProjection(1, AllOf(CaptureEq(&add), add_matcher)),
                        start(), start()));
}

TEST_F(Int64LoweringTest, I64Clz) {
  LowerGraph(graph()->NewNode(machine()->Word64ClzLowerable(),
                              Int64Constant(value(0)), graph()->start()),
             MachineRepresentation::kWord64);

  Capture<Node*> branch_capture;
  Matcher<Node*> branch_matcher = IsBranch(
      IsWord32Equal(IsInt32Constant(high_word_value(0)), IsInt32Constant(0)),
      start());

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(
          IsPhi(MachineRepresentation::kWord32,
                IsInt32Add(IsWord32Clz(IsInt32Constant(low_word_value(0))),
                           IsInt32Constant(32)),
                IsWord32Clz(IsInt32Constant(high_word_value(0))),
                IsMerge(
                    IsIfTrue(AllOf(CaptureEq(&branch_capture), branch_matcher)),
                    IsIfFalse(
                        AllOf(CaptureEq(&branch_capture), branch_matcher)))),
          IsInt32Constant(0), start(), start()));
}

TEST_F(Int64LoweringTest, I64Ctz) {
  LowerGraph(graph()->NewNode(machine()->Word64CtzLowerable().placeholder(),
                              Int64Constant(value(0)), graph()->start()),
             MachineRepresentation::kWord64);
  Capture<Node*> branch_capture;
  Matcher<Node*> branch_matcher = IsBranch(
      IsWord32Equal(IsInt32Constant(low_word_value(0)), IsInt32Constant(0)),
      start());
  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(
          IsPhi(MachineRepresentation::kWord32,
                IsInt32Add(IsWord32Ctz(IsInt32Constant(high_word_value(0))),
                           IsInt32Constant(32)),
                IsWord32Ctz(IsInt32Constant(low_word_value(0))),
                IsMerge(
                    IsIfTrue(AllOf(CaptureEq(&branch_capture), branch_matcher)),
                    IsIfFalse(
                        AllOf(CaptureEq(&branch_capture), branch_matcher)))),
          IsInt32Constant(0), start(), start()));
}

TEST_F(Int64LoweringTest, I64Ror) {
  LowerGraph(
      graph()->NewNode(machine()->Word64RorLowerable(), Int64Constant(value(0)),
                       Parameter(0), graph()->start()),
      MachineRepresentation::kWord64, MachineRepresentation::kWord64, 1);

  Matcher<Node*> branch_lt32_matcher =
      IsBranch(IsInt32LessThan(IsParameter(0), IsInt32Constant(32)), start());

  Matcher<Node*> low_input_matcher = IsPhi(
      MachineRepresentation::kWord32, IsInt32Constant(low_word_value(0)),
      IsInt32Constant(high_word_value(0)),
      IsMerge(IsIfTrue(branch_lt32_matcher), IsIfFalse(branch_lt32_matcher)));

  Matcher<Node*> high_input_matcher = IsPhi(
      MachineRepresentation::kWord32, IsInt32Constant(high_word_value(0)),
      IsInt32Constant(low_word_value(0)),
      IsMerge(IsIfTrue(branch_lt32_matcher), IsIfFalse(branch_lt32_matcher)));

  Matcher<Node*> shift_matcher =
      IsWord32And(IsParameter(0), IsInt32Constant(0x1F));

  Matcher<Node*> bit_mask_matcher = IsWord32Xor(
      IsWord32Shr(IsInt32Constant(-1), shift_matcher), IsInt32Constant(-1));

  Matcher<Node*> inv_mask_matcher =
      IsWord32Xor(bit_mask_matcher, IsInt32Constant(-1));

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(
          IsWord32Or(IsWord32And(IsWord32Ror(low_input_matcher, shift_matcher),
                                 inv_mask_matcher),
                     IsWord32And(IsWord32Ror(high_input_matcher, shift_matcher),
                                 bit_mask_matcher)),
          IsWord32Or(IsWord32And(IsWord32Ror(high_input_matcher, shift_matcher),
                                 inv_mask_matcher),
                     IsWord32And(IsWord32Ror(low_input_matcher, shift_matcher),
                                 bit_mask_matcher)),
          start(), start()));
}

TEST_F(Int64LoweringTest, I64Ror_0) {
  LowerGraph(
      graph()->NewNode(machine()->Word64RorLowerable(), Int64Constant(value(0)),
                       Int32Constant(0), graph()->start()),
      MachineRepresentation::kWord64);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsInt32Constant(low_word_value(0)),
                        IsInt32Constant(high_word_value(0)), start(), start()));
}

TEST_F(Int64LoweringTest, I64Ror_32) {
  LowerGraph(
      graph()->NewNode(machine()->Word64RorLowerable(), Int64Constant(value(0)),
                       Int32Constant(32), graph()->start()),
      MachineRepresentation::kWord64);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsInt32Constant(high_word_value(0)),
                        IsInt32Constant(low_word_value(0)), start(), start()));
}

TEST_F(Int64LoweringTest, I64Ror_11) {
  LowerGraph(
      graph()->NewNode(machine()->Word64RorLowerable(), Int64Constant(value(0)),
                       Int32Constant(11), graph()->start()),
      MachineRepresentation::kWord64);

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsWord32Or(IsWord32Shr(IsInt32Constant(low_word_value(0)),
                                       IsInt32Constant(11)),
                           IsWord32Shl(IsInt32Constant(high_word_value(0)),
                                       IsInt32Constant(21))),
                IsWord32Or(IsWord32Shr(IsInt32Constant(high_word_value(0)),
                                       IsInt32Constant(11)),
                           IsWord32Shl(IsInt32Constant(low_word_value(0)),
                                       IsInt32Constant(21))),
                start(), start()));
}

TEST_F(Int64LoweringTest, I64Ror_43) {
  LowerGraph(
      graph()->NewNode(machine()->Word64RorLowerable(), Int64Constant(value(0)),
                       Int32Constant(43), graph()->start()),
      MachineRepresentation::kWord64);

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsWord32Or(IsWord32Shr(IsInt32Constant(high_word_value(0)),
                                       IsInt32Constant(11)),
                           IsWord32Shl(IsInt32Constant(low_word_value(0)),
                                       IsInt32Constant(21))),
                IsWord32Or(IsWord32Shr(IsInt32Constant(low_word_value(0)),
                                       IsInt32Constant(11)),
                           IsWord32Shl(IsInt32Constant(high_word_value(0)),
                                       IsInt32Constant(21))),
                start(), start()));
}

TEST_F(Int64LoweringTest, Int64Sub) {
  LowerGraph(graph()->NewNode(machine()->Int64Sub(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);

  Capture<Node*> sub;
  Matcher<Node*> sub_matcher = IsInt32PairSub(
      IsInt32Constant(low_word_value(0)), IsInt32Constant(high_word_value(0)),
      IsInt32Constant(low_word_value(1)), IsInt32Constant(high_word_value(1)));

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsProjection(0, AllOf(CaptureEq(&sub), sub_matcher)),
                        IsProjection(1, AllOf(CaptureEq(&sub), sub_matcher)),
                        start(), start()));
}

TEST_F(Int64LoweringTest, Int64Mul) {
  LowerGraph(graph()->NewNode(machine()->Int64Mul(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);

  Capture<Node*> mul_capture;
  Matcher<Node*> mul_matcher = IsInt32PairMul(
      IsInt32Constant(low_word_value(0)), IsInt32Constant(high_word_value(0)),
      IsInt32Constant(low_word_value(1)), IsInt32Constant(high_word_value(1)));

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsProjection(0, AllOf(CaptureEq(&mul_capture), mul_matcher)),
                IsProjection(1, AllOf(CaptureEq(&mul_capture), mul_matcher)),
                start(), start()));
}

TEST_F(Int64LoweringTest, Int64Ior) {
  LowerGraph(graph()->NewNode(machine()->Word64Or(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);
  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsWord32Or(IsInt32Constant(low_word_value(0)),
                                   IsInt32Constant(low_word_value(1))),
                        IsWord32Or(IsInt32Constant(high_word_value(0)),
                                   IsInt32Constant(high_word_value(1))),
                        start(), start()));
}

TEST_F(Int64LoweringTest, Int64Xor) {
  LowerGraph(graph()->NewNode(machine()->Word64Xor(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);
  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsWord32Xor(IsInt32Constant(low_word_value(0)),
                                    IsInt32Constant(low_word_value(1))),
                        IsWord32Xor(IsInt32Constant(high_word_value(0)),
                                    IsInt32Constant(high_word_value(1))),
                        start(), start()));
}

TEST_F(Int64LoweringTest, Int64Shl) {
  LowerGraph(graph()->NewNode(machine()->Word64Shl(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);

  Capture<Node*> shl;
  Matcher<Node*> shl_matcher = IsWord32PairShl(
      IsInt32Constant(low_word_value(0)), IsInt32Constant(high_word_value(0)),
      IsInt32Constant(low_word_value(1)));

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsProjection(0, AllOf(CaptureEq(&shl), shl_matcher)),
                        IsProjection(1, AllOf(CaptureEq(&shl), shl_matcher)),
                        start(), start()));
}

TEST_F(Int64LoweringTest, Int64ShrU) {
  LowerGraph(graph()->NewNode(machine()->Word64Shr(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);

  Capture<Node*> shr;
  Matcher<Node*> shr_matcher = IsWord32PairShr(
      IsInt32Constant(low_word_value(0)), IsInt32Constant(high_word_value(0)),
      IsInt32Constant(low_word_value(1)));

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsProjection(0, AllOf(CaptureEq(&shr), shr_matcher)),
                        IsProjection(1, AllOf(CaptureEq(&shr), shr_matcher)),
                        start(), start()));
}

TEST_F(Int64LoweringTest, Int64ShrS) {
  LowerGraph(graph()->NewNode(machine()->Word64Sar(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord64);

  Capture<Node*> sar;
  Matcher<Node*> sar_matcher = IsWord32PairSar(
      IsInt32Constant(low_word_value(0)), IsInt32Constant(high_word_value(0)),
      IsInt32Constant(low_word_value(1)));

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsProjection(0, AllOf(CaptureEq(&sar), sar_matcher)),
                        IsProjection(1, AllOf(CaptureEq(&sar), sar_matcher)),
                        start(), start()));
}

TEST_F(Int64LoweringTest, Int64Eq) {
  LowerGraph(graph()->NewNode(machine()->Word64Equal(), Int64Constant(value(0)),
                              Int64Constant(value(1))),
             MachineRepresentation::kWord32);
  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn(IsWord32Equal(
                   IsWord32Or(IsWord32Xor(IsInt32Constant(low_word_value(0)),
                                          IsInt32Constant(low_word_value(1))),
                              IsWord32Xor(IsInt32Constant(high_word_value(0)),
                                          IsInt32Constant(high_word_value(1)))),
                   IsInt32Constant(0)),
               start(), start()));
}

TEST_F(Int64LoweringTest, Int64LtS) {
  TestComparison(machine()->Int64LessThan(), IsInt32LessThan, IsUint32LessThan);
}

TEST_F(Int64LoweringTest, Int64LeS) {
  TestComparison(machine()->Int64LessThanOrEqual(), IsInt32LessThan,
                 IsUint32LessThanOrEqual);
}

TEST_F(Int64LoweringTest, Int64LtU) {
  TestComparison(machine()->Uint64LessThan(), IsUint32LessThan,
                 IsUint32LessThan);
}

TEST_F(Int64LoweringTest, Int64LeU) {
  TestComparison(machine()->Uint64LessThanOrEqual(), IsUint32LessThan,
                 IsUint32LessThanOrEqual);
}

TEST_F(Int64LoweringTest, I32ConvertI64) {
  LowerGraph(graph()->NewNode(machine()->TruncateInt64ToInt32(),
                              Int64Constant(value(0))),
             MachineRepresentation::kWord32);
  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn(IsInt32Constant(low_word_value(0)), start(), start()));
}

TEST_F(Int64LoweringTest, I64SConvertI32) {
  LowerGraph(graph()->NewNode(machine()->ChangeInt32ToInt64(),
                              Int32Constant(low_word_value(0))),
             MachineRepresentation::kWord64);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsInt32Constant(low_word_value(0)),
                        IsWord32Sar(IsInt32Constant(low_word_value(0)),
                                    IsInt32Constant(31)),
                        start(), start()));
}

TEST_F(Int64LoweringTest, I64SConvertI32_2) {
  LowerGraph(
      graph()->NewNode(machine()->ChangeInt32ToInt64(),
                       graph()->NewNode(machine()->TruncateInt64ToInt32(),
                                        Int64Constant(value(0)))),
      MachineRepresentation::kWord64);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsInt32Constant(low_word_value(0)),
                        IsWord32Sar(IsInt32Constant(low_word_value(0)),
                                    IsInt32Constant(31)),
                        start(), start()));
}

TEST_F(Int64LoweringTest, I64UConvertI32) {
  LowerGraph(graph()->NewNode(machine()->ChangeUint32ToUint64(),
                              Int32Constant(low_word_value(0))),
             MachineRepresentation::kWord64);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsInt32Constant(low_word_value(0)), IsInt32Constant(0),
                        start(), start()));
}

TEST_F(Int64LoweringTest, I64UConvertI32_2) {
  LowerGraph(
      graph()->NewNode(machine()->ChangeUint32ToUint64(),
                       graph()->NewNode(machine()->TruncateInt64ToInt32(),
                                        Int64Constant(value(0)))),
      MachineRepresentation::kWord64);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsInt32Constant(low_word_value(0)), IsInt32Constant(0),
                        start(), start()));
}

TEST_F(Int64LoweringTest, F64ReinterpretI64) {
  int64_t value = 0x0123456789abcdef;
  LowerGraph(graph()->NewNode(machine()->BitcastInt64ToFloat64(),
                              Int64Constant(value)),
             MachineRepresentation::kFloat64);
  Node* ret = graph()->end()->InputAt(1);
  EXPECT_EQ(ret->opcode(), IrOpcode::kReturn);
  Node* ret_value = ret->InputAt(1);
  EXPECT_EQ(ret_value->opcode(), IrOpcode::kFloat64InsertLowWord32);
  Node* high_half = ret_value->InputAt(0);
  EXPECT_EQ(high_half->opcode(), IrOpcode::kFloat64InsertHighWord32);
  Node* low_half_bits = ret_value->InputAt(1);
  Int32Matcher m1(low_half_bits);
  EXPECT_TRUE(m1.Is(static_cast<int32_t>(value & 0xFFFFFFFF)));
  Node* high_half_bits = high_half->InputAt(1);
  Int32Matcher m2(high_half_bits);
  EXPECT_TRUE(m2.Is(static_cast<int32_t>(value >> 32)));
}

TEST_F(Int64LoweringTest, I64ReinterpretF64) {
  double value = 1234.5678;
  LowerGraph(graph()->NewNode(machine()->BitcastFloat64ToInt64(),
                              Float64Constant(value)),
             MachineRepresentation::kWord64);
  Node* ret = graph()->end()->InputAt(1);
  EXPECT_EQ(ret->opcode(), IrOpcode::kReturn);
  Node* ret_value_low = ret->InputAt(1);
  EXPECT_EQ(ret_value_low->opcode(), IrOpcode::kFloat64ExtractLowWord32);
  Node* ret_value_high = ret->InputAt(2);
  EXPECT_EQ(ret_value_high->opcode(), IrOpcode::kFloat64ExtractHighWord32);
}

TEST_F(Int64LoweringTest, Dfs) {
  Node* common = Int64Constant(value(0));
  LowerGraph(graph()->NewNode(machine()->Word64And(), common,
                              graph()->NewNode(machine()->Word64And(), common,
                                               Int64Constant(value(1)))),
             MachineRepresentation::kWord64);

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsWord32And(IsInt32Constant(low_word_value(0)),
                            IsWord32And(IsInt32Constant(low_word_value(0)),
                                        IsInt32Constant(low_word_value(1)))),
                IsWord32And(IsInt32Constant(high_word_value(0)),
                            IsWord32And(IsInt32Constant(high_word_value(0)),
                                        IsInt32Constant(high_word_value(1)))),
                start(), start()));
}

TEST_F(Int64LoweringTest, I64Popcnt) {
  LowerGraph(graph()->NewNode(machine()->Word64Popcnt().placeholder(),
                              Int64Constant(value(0))),
             MachineRepresentation::kWord64);

  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsInt32Add(IsWord32Popcnt(IsInt32Constant(low_word_value(0))),
                           IsWord32Popcnt(IsInt32Constant(high_word_value(0)))),
                IsInt32Constant(0), start(), start()));
}

TEST_F(Int64LoweringTest, I64PhiWord64) {
  LowerGraph(graph()->NewNode(common()->Phi(MachineRepresentation::kWord64, 2),
                              Int64Constant(value(0)), Int64Constant(value(1)),
                              start()),
             MachineRepresentation::kWord64);

  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsPhi(MachineRepresentation::kWord32,
                              IsInt32Constant(low_word_value(0)),
                              IsInt32Constant(low_word_value(1)), start()),
                        IsPhi(MachineRepresentation::kWord32,
                              IsInt32Constant(high_word_value(0)),
                              IsInt32Constant(high_word_value(1)), start()),
                        start(), start()));
}

void TestPhi(Int64LoweringTest* test, MachineRepresentation rep, Node* v1,
             Node* v2) {
  test->LowerGraph(test->graph()->NewNode(test->common()->Phi(rep, 2), v1, v2,
                                          test->start()),
                   rep);

  EXPECT_THAT(test->graph()->end()->InputAt(1),
              IsReturn(IsPhi(rep, v1, v2, test->start()), test->start(),
                       test->start()));
}

TEST_F(Int64LoweringTest, I64PhiFloat32) {
  TestPhi(this, MachineRepresentation::kFloat32, Float32Constant(1.5),
          Float32Constant(2.5));
}

TEST_F(Int64LoweringTest, I64PhiFloat64) {
  TestPhi(this, MachineRepresentation::kFloat64, Float32Constant(1.5),
          Float32Constant(2.5));
}

TEST_F(Int64LoweringTest, I64PhiWord32) {
  TestPhi(this, MachineRepresentation::kWord32, Float32Constant(1),
          Float32Constant(2));
}

TEST_F(Int64LoweringTest, I64ReverseBytes) {
  LowerGraph(graph()->NewNode(machine()->Word64ReverseBytes(),
                              Int64Constant(value(0))),
             MachineRepresentation::kWord64);
  EXPECT_THAT(
      graph()->end()->InputAt(1),
      IsReturn2(IsWord32ReverseBytes(IsInt32Constant(high_word_value(0))),
                IsWord32ReverseBytes(IsInt32Constant(low_word_value(0))),
                start(), start()));
}

TEST_F(Int64LoweringTest, EffectPhiLoop) {
  // Construct a cycle consisting of an EffectPhi, a Store, and a Load.
  Node* eff_phi = graph()->NewNode(common()->EffectPhi(1), graph()->start(),
                                   graph()->start());

  StoreRepresentation store_rep(MachineRepresentation::kWord64,
                                WriteBarrierKind::kNoWriteBarrier);
  LoadRepresentation load_rep(MachineType::Int64());

  Node* load =
      graph()->NewNode(machine()->Load(load_rep), Int64Constant(value(0)),
                       Int64Constant(value(1)), eff_phi, graph()->start());

  Node* store =
      graph()->NewNode(machine()->Store(store_rep), Int64Constant(value(0)),
                       Int64Constant(value(1)), load, load, graph()->start());

  eff_phi->InsertInput(zone(), 1, store);
  NodeProperties::ChangeOp(eff_phi,
                           common()->ResizeMergeOrPhi(eff_phi->op(), 2));

  LowerGraph(load, MachineRepresentation::kWord64);
}

TEST_F(Int64LoweringTest, LoopCycle) {
  // New node with two placeholders.
  Node* compare = graph()->NewNode(machine()->Word64Equal(), Int64Constant(0),
                                   Int64Constant(value(0)));

  Node* load = graph()->NewNode(
      machine()->Load(MachineType::Int64()), Int64Constant(value(1)),
      Int64Constant(value(2)), graph()->start(),
      graph()->NewNode(
          common()->Loop(2), graph()->start(),
          graph()->NewNode(common()->IfFalse(),
                           graph()->NewNode(common()->Branch(), compare,
                                            graph()->start()))));

  NodeProperties::ReplaceValueInput(compare, load, 0);

  LowerGraph(load, MachineRepresentation::kWord64);
}

TEST_F(Int64LoweringTest, LoopExitValue) {
  Node* loop_header = graph()->NewNode(common()->Loop(1), graph()->start());
  Node* loop_exit =
      graph()->NewNode(common()->LoopExit(), loop_header, loop_header);
  Node* exit =
      graph()->NewNode(common()->LoopExitValue(MachineRepresentation::kWord64),
                       Int64Constant(value(2)), loop_exit);
  LowerGraph(exit, MachineRepresentation::kWord64);
  EXPECT_THAT(graph()->end()->InputAt(1),
              IsReturn2(IsLoopExitValue(MachineRepresentation::kWord32,
                                        IsInt32Constant(low_word_value(2))),
                        IsLoopExitValue(MachineRepresentation::kWord32,
                                        IsInt32Constant(high_word_value(2))),
                        start(), start()));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_32_BIT
