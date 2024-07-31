// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/compiler/access-info.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/representation-change.h"
#include "src/compiler/type-cache.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/graph-and-builders.h"
#include "test/cctest/compiler/js-heap-broker-base.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {

class RepresentationChangerTester : public HandleAndZoneScope,
                                    public GraphAndBuilders,
                                    public JSHeapBrokerTestBase {
 public:
  explicit RepresentationChangerTester(int num_parameters = 0)
      : HandleAndZoneScope(kCompressGraphZone),
        GraphAndBuilders(main_zone()),
        JSHeapBrokerTestBase(main_isolate(), main_zone()),
        javascript_(main_zone()),
        jsgraph_(main_isolate(), main_graph_, &main_common_, &javascript_,
                 &main_simplified_, &main_machine_),
        changer_(&jsgraph_, broker(), nullptr) {
    Node* s = graph()->NewNode(common()->Start(num_parameters));
    graph()->SetStart(s);
  }

  JSOperatorBuilder javascript_;
  JSGraph jsgraph_;
  RepresentationChanger changer_;

  Isolate* isolate() { return main_isolate(); }
  Graph* graph() { return main_graph_; }
  CommonOperatorBuilder* common() { return &main_common_; }
  JSGraph* jsgraph() { return &jsgraph_; }
  RepresentationChanger* changer() { return &changer_; }

  // TODO(titzer): use ValueChecker / ValueUtil
  void CheckInt32Constant(Node* n, int32_t expected) {
    Int32Matcher m(n);
    CHECK(m.HasResolvedValue());
    CHECK_EQ(expected, m.ResolvedValue());
  }

  void CheckInt64Constant(Node* n, int64_t expected) {
    Int64Matcher m(n);
    CHECK(m.HasResolvedValue());
    CHECK_EQ(expected, m.ResolvedValue());
  }

  void CheckUint32Constant(Node* n, uint32_t expected) {
    Uint32Matcher m(n);
    CHECK(m.HasResolvedValue());
    CHECK_EQ(static_cast<int>(expected), static_cast<int>(m.ResolvedValue()));
  }

  void CheckFloat64Constant(Node* n, double expected) {
    Float64Matcher m(n);
    CHECK(m.HasResolvedValue());
    CHECK_DOUBLE_EQ(expected, m.ResolvedValue());
  }

  void CheckFloat32Constant(Node* n, float expected) {
    CHECK_EQ(IrOpcode::kFloat32Constant, n->opcode());
    float fval = OpParameter<float>(n->op());
    CHECK_FLOAT_EQ(expected, fval);
  }

  void CheckHeapConstant(Node* n, Tagged<HeapObject> expected) {
    HeapObjectMatcher m(n);
    CHECK(m.HasResolvedValue());
    CHECK_EQ(expected, *m.ResolvedValue());
  }

  void CheckNumberConstant(Node* n, double expected) {
    NumberMatcher m(n);
    CHECK_EQ(IrOpcode::kNumberConstant, n->opcode());
    CHECK(m.HasResolvedValue());
    CHECK_DOUBLE_EQ(expected, m.ResolvedValue());
  }

  Node* Parameter(int index = 0) {
    Node* n = graph()->NewNode(common()->Parameter(index), graph()->start());
    NodeProperties::SetType(n, Type::Any());
    return n;
  }

  Node* Return(Node* input) {
    Node* n = graph()->NewNode(common()->Return(), jsgraph()->Int32Constant(0),
                               input, graph()->start(), graph()->start());
    return n;
  }

  void CheckTypeError(MachineRepresentation from, Type from_type,
                      MachineRepresentation to) {
    changer()->testing_type_errors_ = true;
    changer()->type_error_ = false;
    Node* n = Parameter(0);
    Node* use = Return(n);
    Node* c = changer()->GetRepresentationFor(n, from, from_type, use,
                                              UseInfo(to, Truncation::None()));
    CHECK(changer()->type_error_);
    CHECK_EQ(n, c);
  }

  void CheckNop(MachineRepresentation from, Type from_type,
                MachineRepresentation to) {
    Node* n = Parameter(0);
    Node* use = Return(n);
    Node* c = changer()->GetRepresentationFor(n, from, from_type, use,
                                              UseInfo(to, Truncation::None()));
    CHECK_EQ(n, c);
  }
};

const MachineType kMachineTypes[] = {
    MachineType::Float32(), MachineType::Float64(),  MachineType::Int8(),
    MachineType::Uint8(),   MachineType::Int16(),    MachineType::Uint16(),
    MachineType::Int32(),   MachineType::Uint32(),   MachineType::Int64(),
    MachineType::Uint64(),  MachineType::AnyTagged()};

TEST(BoolToBit_constant) {
  RepresentationChangerTester r;

  Node* true_node = r.jsgraph()->TrueConstant();
  Node* true_use = r.Return(true_node);
  Node* true_bit = r.changer()->GetRepresentationFor(
      true_node, MachineRepresentation::kTagged, Type::None(), true_use,
      UseInfo(MachineRepresentation::kBit, Truncation::None()));
  r.CheckInt32Constant(true_bit, 1);

  Node* false_node = r.jsgraph()->FalseConstant();
  Node* false_use = r.Return(false_node);
  Node* false_bit = r.changer()->GetRepresentationFor(
      false_node, MachineRepresentation::kTagged, Type::None(), false_use,
      UseInfo(MachineRepresentation::kBit, Truncation::None()));
  r.CheckInt32Constant(false_bit, 0);
}

TEST(ToTagged_constant) {
  RepresentationChangerTester r;

  for (double i : ValueHelper::float64_vector()) {
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kFloat64, Type::None(), use,
        UseInfo(MachineRepresentation::kTagged, Truncation::None()));
    r.CheckNumberConstant(c, i);
  }

  for (int i : ValueHelper::int32_vector()) {
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kWord32, Type::Signed32(), use,
        UseInfo(MachineRepresentation::kTagged, Truncation::None()));
    r.CheckNumberConstant(c, i);
  }

  for (uint32_t i : ValueHelper::uint32_vector()) {
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kWord32, Type::Unsigned32(), use,
        UseInfo(MachineRepresentation::kTagged, Truncation::None()));
    r.CheckNumberConstant(c, i);
  }
}

TEST(ToFloat64_constant) {
  RepresentationChangerTester r;

  for (double i : ValueHelper::float64_vector()) {
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kTagged, Type::None(), use,
        UseInfo(MachineRepresentation::kFloat64, Truncation::None()));
    r.CheckFloat64Constant(c, i);
  }

  for (int i : ValueHelper::int32_vector()) {
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kWord32, Type::Signed32(), use,
        UseInfo(MachineRepresentation::kFloat64, Truncation::None()));
    r.CheckFloat64Constant(c, i);
  }

  for (uint32_t i : ValueHelper::uint32_vector()) {
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kWord32, Type::Unsigned32(), use,
        UseInfo(MachineRepresentation::kFloat64, Truncation::None()));
    r.CheckFloat64Constant(c, i);
  }

  {
    Node* n = r.jsgraph()->ConstantNoHole(0);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kWord64, Type::Range(0, 0, r.zone()), use,
        UseInfo(MachineRepresentation::kFloat64, Truncation::None()));
    r.CheckFloat64Constant(c, 0);
  }
}


static bool IsFloat32Int32(int32_t val) {
  return val >= -(1 << 23) && val <= (1 << 23);
}


static bool IsFloat32Uint32(uint32_t val) { return val <= (1 << 23); }


TEST(ToFloat32_constant) {
  RepresentationChangerTester r;

  for (double i : ValueHelper::float32_vector()) {
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kTagged, Type::None(), use,
        UseInfo(MachineRepresentation::kFloat32, Truncation::None()));
    r.CheckFloat32Constant(c, i);
  }

  for (int i : ValueHelper::int32_vector()) {
    if (!IsFloat32Int32(i)) continue;
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kWord32, Type::Signed32(), use,
        UseInfo(MachineRepresentation::kFloat32, Truncation::None()));
    r.CheckFloat32Constant(c, static_cast<float>(i));
  }

  for (uint32_t i : ValueHelper::uint32_vector()) {
    if (!IsFloat32Uint32(i)) continue;
    Node* n = r.jsgraph()->ConstantNoHole(i);
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kWord32, Type::Unsigned32(), use,
        UseInfo(MachineRepresentation::kFloat32, Truncation::None()));
    r.CheckFloat32Constant(c, static_cast<float>(i));
  }
}

TEST(ToInt32_constant) {
  RepresentationChangerTester r;
  {
    FOR_INT32_INPUTS(i) {
      const double value = static_cast<double>(i);
      Node* n = r.jsgraph()->ConstantNoHole(value);
      NodeProperties::SetType(n, Type::Constant(value, r.zone()));
      Node* use = r.Return(n);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kTagged, Type::Signed32(), use,
          UseInfo(MachineRepresentation::kWord32, Truncation::None()));
      r.CheckInt32Constant(c, i);
    }
  }
}

TEST(ToUint32_constant) {
  RepresentationChangerTester r;
  FOR_UINT32_INPUTS(i) {
    const double value = static_cast<double>(i);
    Node* n = r.jsgraph()->ConstantNoHole(value);
    NodeProperties::SetType(n, Type::Constant(value, r.zone()));
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kTagged, Type::Unsigned32(), use,
        UseInfo(MachineRepresentation::kWord32, Truncation::None()));
    r.CheckUint32Constant(c, i);
  }
}

TEST(ToInt64_constant) {
  RepresentationChangerTester r;
  FOR_INT32_INPUTS(i) {
    const double value = static_cast<double>(i);
    Node* n = r.jsgraph()->ConstantNoHole(value);
    NodeProperties::SetType(n, Type::Constant(value, r.zone()));
    Node* use = r.Return(n);
    Node* c = r.changer()->GetRepresentationFor(
        n, MachineRepresentation::kTagged, TypeCache::Get()->kSafeInteger, use,
        UseInfo(MachineRepresentation::kWord64, Truncation::None()));
    r.CheckInt64Constant(c, i);
  }
}

static void CheckChange(IrOpcode::Value expected, MachineRepresentation from,
                        Type from_type, UseInfo use_info) {
  RepresentationChangerTester r;

  Node* n = r.Parameter();
  Node* use = r.Return(n);
  Node* c =
      r.changer()->GetRepresentationFor(n, from, from_type, use, use_info);

  CHECK_NE(c, n);
  CHECK_EQ(expected, c->opcode());
  CHECK_EQ(n, c->InputAt(0));

  if (expected == IrOpcode::kCheckedFloat64ToInt32 ||
      expected == IrOpcode::kCheckedFloat64ToInt64) {
    CheckForMinusZeroMode mode =
        from_type.Maybe(Type::MinusZero())
            ? use_info.minus_zero_check()
            : CheckForMinusZeroMode::kDontCheckForMinusZero;
    CHECK_EQ(mode, CheckMinusZeroParametersOf(c->op()).mode());
  }
}

static void CheckChange(IrOpcode::Value expected, MachineRepresentation from,
                        Type from_type, MachineRepresentation to) {
  CheckChange(expected, from, from_type, UseInfo(to, Truncation::Any()));
}

static void CheckTwoChanges(IrOpcode::Value expected2,
                            IrOpcode::Value expected1,
                            MachineRepresentation from, Type from_type,
                            MachineRepresentation to, UseInfo use_info) {
  RepresentationChangerTester r;

  Node* n = r.Parameter();
  Node* use = r.Return(n);
  Node* c1 =
      r.changer()->GetRepresentationFor(n, from, from_type, use, use_info);

  CHECK_NE(c1, n);
  CHECK_EQ(expected1, c1->opcode());
  Node* c2 = c1->InputAt(0);
  CHECK_NE(c2, n);
  CHECK_EQ(expected2, c2->opcode());
  CHECK_EQ(n, c2->InputAt(0));
}

static void CheckTwoChanges(IrOpcode::Value expected2,
                            IrOpcode::Value expected1,
                            MachineRepresentation from, Type from_type,
                            MachineRepresentation to) {
  CheckTwoChanges(expected2, expected1, from, from_type, to,
                  UseInfo(to, Truncation::None()));
}

static void CheckChange(IrOpcode::Value expected, MachineRepresentation from,
                        Type from_type, MachineRepresentation to,
                        UseInfo use_info) {
  RepresentationChangerTester r;

  Node* n = r.Parameter();
  Node* use = r.Return(n);
  Node* c =
      r.changer()->GetRepresentationFor(n, from, from_type, use, use_info);

  CHECK_NE(c, n);
  CHECK_EQ(expected, c->opcode());
  CHECK_EQ(n, c->InputAt(0));
}

TEST(Word64) {
  CheckChange(IrOpcode::kChangeInt32ToInt64, MachineRepresentation::kWord8,
              TypeCache::Get()->kInt8, MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeUint32ToUint64, MachineRepresentation::kWord8,
              TypeCache::Get()->kUint8, MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeInt32ToInt64, MachineRepresentation::kWord16,
              TypeCache::Get()->kInt16, MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeUint32ToUint64, MachineRepresentation::kWord16,
              TypeCache::Get()->kUint16, MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeInt32ToInt64, MachineRepresentation::kWord32,
              Type::Signed32(), MachineRepresentation::kWord64);
  CheckChange(
      IrOpcode::kChangeInt32ToInt64, MachineRepresentation::kWord32,
      Type::Signed32OrMinusZero(), MachineRepresentation::kWord64,
      UseInfo(MachineRepresentation::kWord64, Truncation::Any(kIdentifyZeros)));
  CheckChange(IrOpcode::kChangeUint32ToUint64, MachineRepresentation::kWord32,
              Type::Unsigned32(), MachineRepresentation::kWord64);
  CheckChange(
      IrOpcode::kChangeUint32ToUint64, MachineRepresentation::kWord32,
      Type::Unsigned32OrMinusZero(), MachineRepresentation::kWord64,
      UseInfo(MachineRepresentation::kWord64, Truncation::Any(kIdentifyZeros)));

  CheckChange(IrOpcode::kTruncateInt64ToInt32, MachineRepresentation::kWord64,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kTruncateInt64ToInt32, MachineRepresentation::kWord64,
              Type::Unsigned32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kTruncateInt64ToInt32, MachineRepresentation::kWord64,
              TypeCache::Get()->kSafeInteger, MachineRepresentation::kWord32,
              UseInfo::TruncatingWord32());
  CheckChange(
      IrOpcode::kCheckedInt64ToInt32, MachineRepresentation::kWord64,
      TypeCache::Get()->kSafeInteger, MachineRepresentation::kWord32,
      UseInfo::CheckedSigned32AsWord32(kIdentifyZeros, FeedbackSource()));
  CheckChange(
      IrOpcode::kCheckedUint64ToInt32, MachineRepresentation::kWord64,
      TypeCache::Get()->kPositiveSafeInteger, MachineRepresentation::kWord32,
      UseInfo::CheckedSigned32AsWord32(kIdentifyZeros, FeedbackSource()));

  CheckChange(IrOpcode::kChangeFloat64ToInt64, MachineRepresentation::kFloat64,
              Type::Signed32(), MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeFloat64ToInt64, MachineRepresentation::kFloat64,
              Type::Unsigned32(), MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeFloat64ToInt64, MachineRepresentation::kFloat64,
              TypeCache::Get()->kSafeInteger, MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeFloat64ToInt64, MachineRepresentation::kFloat64,
              TypeCache::Get()->kDoubleRepresentableInt64,
              MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeFloat64ToUint64, MachineRepresentation::kFloat64,
              TypeCache::Get()->kDoubleRepresentableUint64,
              MachineRepresentation::kWord64);
  CheckChange(
      IrOpcode::kCheckedFloat64ToInt64, MachineRepresentation::kFloat64,
      Type::Number(), MachineRepresentation::kWord64,
      UseInfo::CheckedSigned64AsWord64(kIdentifyZeros, FeedbackSource()));

  CheckChange(IrOpcode::kChangeInt64ToFloat64, MachineRepresentation::kWord64,
              Type::Signed32(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeInt64ToFloat64, MachineRepresentation::kWord64,
              Type::Unsigned32(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeInt64ToFloat64, MachineRepresentation::kWord64,
              TypeCache::Get()->kSafeInteger, MachineRepresentation::kFloat64);

  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToInt64,
                  MachineRepresentation::kFloat32, Type::Signed32(),
                  MachineRepresentation::kWord64);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToInt64,
                  MachineRepresentation::kFloat32, Type::Unsigned32(),
                  MachineRepresentation::kWord64);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToInt64,
                  MachineRepresentation::kFloat32,
                  TypeCache::Get()->kDoubleRepresentableInt64,
                  MachineRepresentation::kWord64);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToUint64,
                  MachineRepresentation::kFloat32,
                  TypeCache::Get()->kDoubleRepresentableUint64,
                  MachineRepresentation::kWord64);
  CheckTwoChanges(
      IrOpcode::kChangeFloat32ToFloat64, IrOpcode::kCheckedFloat64ToInt64,
      MachineRepresentation::kFloat32, Type::Number(),
      MachineRepresentation::kWord64,
      UseInfo::CheckedSigned64AsWord64(kIdentifyZeros, FeedbackSource()));

  CheckTwoChanges(IrOpcode::kChangeInt64ToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kWord64, Type::Signed32(),
                  MachineRepresentation::kFloat32);

  CheckChange(IrOpcode::kChangeTaggedToInt64, MachineRepresentation::kTagged,
              Type::Signed32(), MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeTaggedToInt64, MachineRepresentation::kTagged,
              Type::Unsigned32(), MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeTaggedToInt64, MachineRepresentation::kTagged,
              TypeCache::Get()->kSafeInteger, MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeTaggedToInt64, MachineRepresentation::kTagged,
              TypeCache::Get()->kDoubleRepresentableInt64,
              MachineRepresentation::kWord64);
  CheckChange(IrOpcode::kChangeTaggedSignedToInt64,
              MachineRepresentation::kTaggedSigned, Type::SignedSmall(),
              MachineRepresentation::kWord64);
  CheckChange(
      IrOpcode::kCheckedTaggedToInt64, MachineRepresentation::kTagged,
      Type::Number(), MachineRepresentation::kWord64,
      UseInfo::CheckedSigned64AsWord64(kIdentifyZeros, FeedbackSource()));
  CheckChange(
      IrOpcode::kCheckedTaggedToInt64, MachineRepresentation::kTaggedPointer,
      Type::Number(), MachineRepresentation::kWord64,
      UseInfo::CheckedSigned64AsWord64(kIdentifyZeros, FeedbackSource()));

  CheckTwoChanges(IrOpcode::kTruncateInt64ToInt32,
                  IrOpcode::kChangeInt31ToTaggedSigned,
                  MachineRepresentation::kWord64, Type::Signed31(),
                  MachineRepresentation::kTagged);
  CheckTwoChanges(IrOpcode::kTruncateInt64ToInt32,
                  IrOpcode::kChangeInt32ToTagged,
                  MachineRepresentation::kWord64, Type::Signed32(),
                  MachineRepresentation::kTagged);
  CheckTwoChanges(IrOpcode::kTruncateInt64ToInt32,
                  IrOpcode::kChangeUint32ToTagged,
                  MachineRepresentation::kWord64, Type::Unsigned32(),
                  MachineRepresentation::kTagged);
  CheckChange(IrOpcode::kChangeInt64ToTagged, MachineRepresentation::kWord64,
              TypeCache::Get()->kSafeInteger, MachineRepresentation::kTagged);
  CheckChange(IrOpcode::kChangeUint64ToTagged, MachineRepresentation::kWord64,
              TypeCache::Get()->kPositiveSafeInteger,
              MachineRepresentation::kTagged);

  CheckTwoChanges(IrOpcode::kTruncateInt64ToInt32,
                  IrOpcode::kChangeInt31ToTaggedSigned,
                  MachineRepresentation::kWord64, Type::Signed31(),
                  MachineRepresentation::kTaggedSigned);
  if (SmiValuesAre32Bits()) {
    CheckTwoChanges(IrOpcode::kTruncateInt64ToInt32,
                    IrOpcode::kChangeInt32ToTagged,
                    MachineRepresentation::kWord64, Type::Signed32(),
                    MachineRepresentation::kTaggedSigned);
  }
  CheckChange(IrOpcode::kCheckedInt64ToTaggedSigned,
              MachineRepresentation::kWord64, TypeCache::Get()->kSafeInteger,
              MachineRepresentation::kTaggedSigned,
              UseInfo::CheckedSignedSmallAsTaggedSigned(FeedbackSource()));
  CheckChange(IrOpcode::kCheckedUint64ToTaggedSigned,
              MachineRepresentation::kWord64,
              TypeCache::Get()->kPositiveSafeInteger,
              MachineRepresentation::kTaggedSigned,
              UseInfo::CheckedSignedSmallAsTaggedSigned(FeedbackSource()));

  CheckTwoChanges(
      IrOpcode::kChangeInt64ToFloat64, IrOpcode::kChangeFloat64ToTaggedPointer,
      MachineRepresentation::kWord64, TypeCache::Get()->kSafeInteger,
      MachineRepresentation::kTaggedPointer);
}

TEST(SingleChanges) {
  CheckChange(IrOpcode::kChangeTaggedToBit, MachineRepresentation::kTagged,
              Type::Boolean(), MachineRepresentation::kBit);
  CheckChange(IrOpcode::kChangeBitToTagged, MachineRepresentation::kBit,
              Type::Boolean(), MachineRepresentation::kTagged);

  CheckChange(IrOpcode::kChangeInt31ToTaggedSigned,
              MachineRepresentation::kWord32, Type::Signed31(),
              MachineRepresentation::kTagged);
  CheckChange(IrOpcode::kChangeInt32ToTagged, MachineRepresentation::kWord32,
              Type::Signed32(), MachineRepresentation::kTagged);
  CheckChange(IrOpcode::kChangeUint32ToTagged, MachineRepresentation::kWord32,
              Type::Unsigned32(), MachineRepresentation::kTagged);
  CheckChange(IrOpcode::kChangeFloat64ToTagged, MachineRepresentation::kFloat64,
              Type::Number(), MachineRepresentation::kTagged);
  CheckTwoChanges(IrOpcode::kChangeFloat64ToInt32,
                  IrOpcode::kChangeInt31ToTaggedSigned,
                  MachineRepresentation::kFloat64, Type::Signed31(),
                  MachineRepresentation::kTagged);
  CheckTwoChanges(IrOpcode::kChangeFloat64ToInt32,
                  IrOpcode::kChangeInt32ToTagged,
                  MachineRepresentation::kFloat64, Type::Signed32(),
                  MachineRepresentation::kTagged);
  CheckTwoChanges(IrOpcode::kChangeFloat64ToUint32,
                  IrOpcode::kChangeUint32ToTagged,
                  MachineRepresentation::kFloat64, Type::Unsigned32(),
                  MachineRepresentation::kTagged);

  CheckChange(IrOpcode::kChangeTaggedToInt32, MachineRepresentation::kTagged,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeTaggedToUint32, MachineRepresentation::kTagged,
              Type::Unsigned32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeTaggedToFloat64, MachineRepresentation::kTagged,
              Type::Number(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kTruncateTaggedToFloat64,
              MachineRepresentation::kTagged, Type::NumberOrUndefined(),
              UseInfo(MachineRepresentation::kFloat64,
                      Truncation::OddballAndBigIntToNumber()));
  CheckChange(IrOpcode::kChangeTaggedToFloat64, MachineRepresentation::kTagged,
              Type::Signed31(), MachineRepresentation::kFloat64);

  // Int32,Uint32 <-> Float64 are actually machine conversions.
  CheckChange(IrOpcode::kChangeInt32ToFloat64, MachineRepresentation::kWord32,
              Type::Signed32(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeInt32ToFloat64, MachineRepresentation::kWord32,
              Type::Signed32OrMinusZero(), MachineRepresentation::kFloat64,
              UseInfo(MachineRepresentation::kFloat64,
                      Truncation::Any(kIdentifyZeros)));
  CheckChange(IrOpcode::kChangeUint32ToFloat64, MachineRepresentation::kWord32,
              Type::Unsigned32(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeFloat64ToInt32, MachineRepresentation::kFloat64,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeFloat64ToUint32, MachineRepresentation::kFloat64,
              Type::Unsigned32(), MachineRepresentation::kWord32);

  CheckChange(IrOpcode::kTruncateFloat64ToFloat32,
              MachineRepresentation::kFloat64, Type::Number(),
              MachineRepresentation::kFloat32);

  // Int32,Uint32 <-> Float32 require two changes.
  CheckTwoChanges(IrOpcode::kChangeInt32ToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kWord32, Type::Signed32(),
                  MachineRepresentation::kFloat32);
  CheckTwoChanges(IrOpcode::kChangeUint32ToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kWord32, Type::Unsigned32(),
                  MachineRepresentation::kFloat32);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToInt32,
                  MachineRepresentation::kFloat32, Type::Signed32(),
                  MachineRepresentation::kWord32);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToUint32,
                  MachineRepresentation::kFloat32, Type::Unsigned32(),
                  MachineRepresentation::kWord32);

  // Float32 <-> Tagged require two changes.
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToTagged,
                  MachineRepresentation::kFloat32, Type::Number(),
                  MachineRepresentation::kTagged);
  CheckTwoChanges(IrOpcode::kChangeTaggedToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kTagged, Type::Number(),
                  MachineRepresentation::kFloat32);
}


TEST(SignednessInWord32) {
  RepresentationChangerTester r;

  CheckChange(IrOpcode::kChangeTaggedToInt32, MachineRepresentation::kTagged,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeTaggedToUint32, MachineRepresentation::kTagged,
              Type::Unsigned32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeInt32ToFloat64, MachineRepresentation::kWord32,
              Type::Signed32(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeFloat64ToInt32, MachineRepresentation::kFloat64,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kTruncateFloat64ToWord32,
              MachineRepresentation::kFloat64, Type::Number(),
              MachineRepresentation::kWord32,
              UseInfo(MachineRepresentation::kWord32, Truncation::Word32()));
  CheckChange(IrOpcode::kCheckedTruncateTaggedToWord32,
              MachineRepresentation::kTagged, Type::NonInternal(),
              MachineRepresentation::kWord32,
              UseInfo::CheckedNumberOrOddballAsWord32(FeedbackSource()));

  CheckTwoChanges(IrOpcode::kChangeInt32ToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kWord32, Type::Signed32(),
                  MachineRepresentation::kFloat32);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kTruncateFloat64ToWord32,
                  MachineRepresentation::kFloat32, Type::Number(),
                  MachineRepresentation::kWord32);

  CheckChange(
      IrOpcode::kCheckedUint32ToInt32, MachineRepresentation::kWord32,
      Type::Unsigned32(),
      UseInfo::CheckedSigned32AsWord32(kIdentifyZeros, FeedbackSource()));
}

static void TestMinusZeroCheck(IrOpcode::Value expected, Type from_type) {
  RepresentationChangerTester r;

  CheckChange(
      expected, MachineRepresentation::kFloat64, from_type,
      UseInfo::CheckedSignedSmallAsWord32(kDistinguishZeros, FeedbackSource()));

  CheckChange(
      expected, MachineRepresentation::kFloat64, from_type,
      UseInfo::CheckedSignedSmallAsWord32(kIdentifyZeros, FeedbackSource()));

  CheckChange(
      expected, MachineRepresentation::kFloat64, from_type,
      UseInfo::CheckedSigned32AsWord32(kDistinguishZeros, FeedbackSource()));

  CheckChange(
      expected, MachineRepresentation::kFloat64, from_type,
      UseInfo::CheckedSigned32AsWord32(kDistinguishZeros, FeedbackSource()));
}

TEST(MinusZeroCheck) {
  TestMinusZeroCheck(IrOpcode::kCheckedFloat64ToInt32, Type::NumberOrOddball());
  // PlainNumber cannot be minus zero so the minus zero check should be
  // eliminated.
  TestMinusZeroCheck(IrOpcode::kCheckedFloat64ToInt32, Type::PlainNumber());
}

TEST(Nops) {
  RepresentationChangerTester r;

  // X -> X is always a nop for any single representation X.
  for (size_t i = 0; i < arraysize(kMachineTypes); i++) {
    r.CheckNop(kMachineTypes[i].representation(), Type::Number(),
               kMachineTypes[i].representation());
  }

  // 32-bit floats.
  r.CheckNop(MachineRepresentation::kFloat32, Type::Number(),
             MachineRepresentation::kFloat32);

  // 32-bit words can be used as smaller word sizes and vice versa, because
  // loads from memory implicitly sign or zero extend the value to the
  // full machine word size, and stores implicitly truncate.
  r.CheckNop(MachineRepresentation::kWord32, Type::Signed32(),
             MachineRepresentation::kWord8);
  r.CheckNop(MachineRepresentation::kWord32, Type::Signed32(),
             MachineRepresentation::kWord16);
  r.CheckNop(MachineRepresentation::kWord32, Type::Signed32(),
             MachineRepresentation::kWord32);
  r.CheckNop(MachineRepresentation::kWord8, Type::Signed32(),
             MachineRepresentation::kWord32);
  r.CheckNop(MachineRepresentation::kWord16, Type::Signed32(),
             MachineRepresentation::kWord32);

  // kRepBit (result of comparison) is implicitly a wordish thing.
  r.CheckNop(MachineRepresentation::kBit, Type::Boolean(),
             MachineRepresentation::kWord8);
  r.CheckNop(MachineRepresentation::kBit, Type::Boolean(),
             MachineRepresentation::kWord16);
  r.CheckNop(MachineRepresentation::kBit, Type::Boolean(),
             MachineRepresentation::kWord32);
}


TEST(TypeErrors) {
  RepresentationChangerTester r;

  // Floats cannot be implicitly converted to/from comparison conditions.
  r.CheckTypeError(MachineRepresentation::kBit, Type::Number(),
                   MachineRepresentation::kFloat32);
  r.CheckTypeError(MachineRepresentation::kBit, Type::Boolean(),
                   MachineRepresentation::kFloat32);

  // Word64 is internal and shouldn't be implicitly converted.
  r.CheckTypeError(MachineRepresentation::kWord64, Type::Internal(),
                   MachineRepresentation::kTagged);
  r.CheckTypeError(MachineRepresentation::kTagged, Type::Number(),
                   MachineRepresentation::kWord64);
  r.CheckTypeError(MachineRepresentation::kTagged, Type::Boolean(),
                   MachineRepresentation::kWord64);
  r.CheckTypeError(MachineRepresentation::kWord64, Type::Internal(),
                   MachineRepresentation::kWord32);
  r.CheckTypeError(MachineRepresentation::kWord32, Type::Number(),
                   MachineRepresentation::kWord64);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
