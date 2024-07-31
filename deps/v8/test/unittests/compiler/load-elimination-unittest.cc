// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/load-elimination.h"

#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"
#include "test/unittests/compiler/graph-reducer-unittest.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"

using testing::_;
using testing::StrictMock;

namespace v8 {
namespace internal {
namespace compiler {

class LoadEliminationTest : public TypedGraphTest {
 public:
  LoadEliminationTest()
      : TypedGraphTest(3),
        simplified_(zone()),
        jsgraph_(isolate(), graph(), common(), nullptr, simplified(), nullptr) {
  }
  ~LoadEliminationTest() override = default;

 protected:
  JSGraph* jsgraph() { return &jsgraph_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

 private:
  SimplifiedOperatorBuilder simplified_;
  JSGraph jsgraph_;
};

TEST_F(LoadEliminationTest, LoadElementAndLoadElement) {
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* load1 = effect = graph()->NewNode(simplified()->LoadElement(access),
                                          object, index, effect, control);
  load_elimination.Reduce(load1);

  Node* load2 = effect = graph()->NewNode(simplified()->LoadElement(access),
                                          object, index, effect, control);
  EXPECT_CALL(editor, ReplaceWithValue(load2, load1, load1, _));
  Reduction r = load_elimination.Reduce(load2);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load1, r.replacement());
}

TEST_F(LoadEliminationTest, StoreElementAndLoadElement) {
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  Node* value = Parameter(Type::Any(), 2);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* store = effect =
      graph()->NewNode(simplified()->StoreElement(access), object, index, value,
                       effect, control);
  load_elimination.Reduce(store);

  Node* load = effect = graph()->NewNode(simplified()->LoadElement(access),
                                         object, index, effect, control);
  EXPECT_CALL(editor, ReplaceWithValue(load, value, store, _));
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(value, r.replacement());
}

TEST_F(LoadEliminationTest, StoreElementAndStoreFieldAndLoadElement) {
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  Node* value = Parameter(Type::Any(), 2);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* store1 = effect =
      graph()->NewNode(simplified()->StoreElement(access), object, index, value,
                       effect, control);
  load_elimination.Reduce(store1);

  Node* store2 = effect =
      graph()->NewNode(simplified()->StoreField(AccessBuilder::ForMap()),
                       object, value, effect, control);
  load_elimination.Reduce(store2);

  Node* load = effect = graph()->NewNode(simplified()->LoadElement(access),
                                         object, index, effect, control);
  EXPECT_CALL(editor, ReplaceWithValue(load, value, store2, _));
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(value, r.replacement());
}

TEST_F(LoadEliminationTest, LoadFieldAndLoadField) {
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess const access = {kTaggedBase,         kTaggedSize,
                              MaybeHandle<Name>(), OptionalMapRef(),
                              Type::Any(),         MachineType::AnyTagged(),
                              kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* load1 = effect = graph()->NewNode(simplified()->LoadField(access),
                                          object, effect, control);
  load_elimination.Reduce(load1);

  Node* load2 = effect = graph()->NewNode(simplified()->LoadField(access),
                                          object, effect, control);
  EXPECT_CALL(editor, ReplaceWithValue(load2, load1, load1, _));
  Reduction r = load_elimination.Reduce(load2);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load1, r.replacement());
}

TEST_F(LoadEliminationTest, StoreFieldAndLoadField) {
  Node* object = Parameter(Type::Any(), 0);
  Node* value = Parameter(Type::Any(), 1);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess access = {kTaggedBase,      kTaggedSize, MaybeHandle<Name>(),
                        OptionalMapRef(), Type::Any(), MachineType::AnyTagged(),
                        kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* store = effect = graph()->NewNode(simplified()->StoreField(access),
                                          object, value, effect, control);
  load_elimination.Reduce(store);

  Node* load = effect = graph()->NewNode(simplified()->LoadField(access),
                                         object, effect, control);
  EXPECT_CALL(editor, ReplaceWithValue(load, value, store, _));
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(value, r.replacement());
}

TEST_F(LoadEliminationTest, StoreFieldAndKillFields) {
  Node* object = Parameter(Type::Any(), 0);
  Node* value = Parameter(Type::Any(), 1);
  Node* effect = graph()->start();
  Node* control = graph()->start();

  FieldAccess access1 = {kTaggedBase,         kTaggedSize,
                         MaybeHandle<Name>(), OptionalMapRef(),
                         Type::Any(),         MachineType::AnyTagged(),
                         kNoWriteBarrier};

  // Offset that out of field cache size.
  FieldAccess access2 = {kTaggedBase,         2048 * kTaggedSize,
                         MaybeHandle<Name>(), OptionalMapRef(),
                         Type::Any(),         MachineType::AnyTagged(),
                         kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* store1 = effect = graph()->NewNode(simplified()->StoreField(access1),
                                          object, value, effect, control);
  load_elimination.Reduce(store1);

  // Invalidate caches of object.
  Node* store2 = effect = graph()->NewNode(simplified()->StoreField(access2),
                                         object, value, effect, control);
  load_elimination.Reduce(store2);

  Node* store3 = graph()->NewNode(simplified()->StoreField(access1),
                                          object, value, effect, control);

  Reduction r = load_elimination.Reduce(store3);

  // store3 shall not be replaced, since caches were invalidated.
  EXPECT_EQ(store3, r.replacement());
}

TEST_F(LoadEliminationTest, StoreFieldAndStoreElementAndLoadField) {
  Node* object = Parameter(Type::Any(), 0);
  Node* value = Parameter(Type::Any(), 1);
  Node* index = Parameter(Type::UnsignedSmall(), 2);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess access = {kTaggedBase,      kTaggedSize, MaybeHandle<Name>(),
                        OptionalMapRef(), Type::Any(), MachineType::AnyTagged(),
                        kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* store1 = effect = graph()->NewNode(simplified()->StoreField(access),
                                           object, value, effect, control);
  load_elimination.Reduce(store1);

  Node* store2 = effect = graph()->NewNode(
      simplified()->StoreElement(AccessBuilder::ForFixedArrayElement()), object,
      index, object, effect, control);
  load_elimination.Reduce(store2);

  Node* load = effect = graph()->NewNode(simplified()->LoadField(access),
                                         object, effect, control);
  EXPECT_CALL(editor, ReplaceWithValue(load, value, store2, _));
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(value, r.replacement());
}

TEST_F(LoadEliminationTest, LoadElementOnTrueBranchOfDiamond) {
  Node* object = Parameter(Type::Any(), 0);
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  Node* check = Parameter(Type::Boolean(), 2);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* branch = graph()->NewNode(common()->Branch(), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = graph()->NewNode(simplified()->LoadElement(access), object,
                                 index, effect, if_true);
  load_elimination.Reduce(etrue);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  load_elimination.Reduce(effect);

  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(LoadEliminationTest, LoadElementOnFalseBranchOfDiamond) {
  Node* object = Parameter(Type::Any(), 0);
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  Node* check = Parameter(Type::Boolean(), 2);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* branch = graph()->NewNode(common()->Branch(), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = graph()->NewNode(simplified()->LoadElement(access), object,
                                  index, effect, if_false);
  load_elimination.Reduce(efalse);

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  load_elimination.Reduce(effect);

  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(LoadEliminationTest, LoadFieldOnFalseBranchOfDiamond) {
  Node* object = Parameter(Type::Any(), 0);
  Node* check = Parameter(Type::Boolean(), 1);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess const access = {kTaggedBase,         kTaggedSize,
                              MaybeHandle<Name>(), OptionalMapRef(),
                              Type::Any(),         MachineType::AnyTagged(),
                              kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* branch = graph()->NewNode(common()->Branch(), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = graph()->NewNode(simplified()->LoadField(access), object,
                                  effect, if_false);
  load_elimination.Reduce(efalse);

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  load_elimination.Reduce(effect);

  Node* load = graph()->NewNode(simplified()->LoadField(access), object, effect,
                                control);
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(LoadEliminationTest, LoadFieldOnTrueBranchOfDiamond) {
  Node* object = Parameter(Type::Any(), 0);
  Node* check = Parameter(Type::Boolean(), 1);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess const access = {kTaggedBase,         kTaggedSize,
                              MaybeHandle<Name>(), OptionalMapRef(),
                              Type::Any(),         MachineType::AnyTagged(),
                              kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  Node* branch = graph()->NewNode(common()->Branch(), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = graph()->NewNode(simplified()->LoadField(access), object,
                                 effect, if_true);
  load_elimination.Reduce(etrue);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  load_elimination.Reduce(effect);

  Node* load = graph()->NewNode(simplified()->LoadField(access), object, effect,
                                control);
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(LoadEliminationTest, LoadFieldWithTypeMismatch) {
  Node* object = Parameter(Type::Any(), 0);
  Node* value = Parameter(Type::Signed32(), 1);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess const access = {kTaggedBase,         kTaggedSize,
                              MaybeHandle<Name>(), OptionalMapRef(),
                              Type::Unsigned31(),  MachineType::AnyTagged(),
                              kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  effect = graph()->NewNode(simplified()->StoreField(access), object, value,
                            effect, control);
  load_elimination.Reduce(effect);

  Node* load = graph()->NewNode(simplified()->LoadField(access), object, effect,
                                control);
  EXPECT_CALL(editor, ReplaceWithValue(load, IsTypeGuard(value, _), _, _));
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsTypeGuard(value, _));
}

TEST_F(LoadEliminationTest, LoadElementWithTypeMismatch) {
  Node* object = Parameter(Type::Any(), 0);
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  Node* value = Parameter(Type::Signed32(), 2);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Unsigned31(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(graph()->start());

  effect = graph()->NewNode(simplified()->StoreElement(access), object, index,
                            value, effect, control);
  load_elimination.Reduce(effect);

  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(LoadEliminationTest, AliasAnalysisForFinishRegion) {
  Node* value0 = Parameter(Type::Signed32(), 0);
  Node* value1 = Parameter(Type::Signed32(), 1);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess const access = {kTaggedBase,         kTaggedSize,
                              MaybeHandle<Name>(), OptionalMapRef(),
                              Type::Signed32(),    MachineType::AnyTagged(),
                              kNoWriteBarrier};

  StrictMock<MockAdvancedReducerEditor> editor;
  LoadElimination load_elimination(&editor, broker(), jsgraph(), zone());

  load_elimination.Reduce(effect);

  effect = graph()->NewNode(
      common()->BeginRegion(RegionObservability::kNotObservable), effect);
  load_elimination.Reduce(effect);

  Node* object0 = effect = graph()->NewNode(
      simplified()->Allocate(Type::Any(), AllocationType::kYoung),
      jsgraph()->ConstantNoHole(16), effect, control);
  load_elimination.Reduce(effect);

  Node* region0 = effect =
      graph()->NewNode(common()->FinishRegion(), object0, effect);
  load_elimination.Reduce(effect);

  effect = graph()->NewNode(
      common()->BeginRegion(RegionObservability::kNotObservable), effect);
  load_elimination.Reduce(effect);

  Node* object1 = effect = graph()->NewNode(
      simplified()->Allocate(Type::Any(), AllocationType::kYoung),
      jsgraph()->ConstantNoHole(16), effect, control);
  load_elimination.Reduce(effect);

  Node* region1 = effect =
      graph()->NewNode(common()->FinishRegion(), object1, effect);
  load_elimination.Reduce(effect);

  effect = graph()->NewNode(simplified()->StoreField(access), region0, value0,
                            effect, control);
  load_elimination.Reduce(effect);

  effect = graph()->NewNode(simplified()->StoreField(access), region1, value1,
                            effect, control);
  load_elimination.Reduce(effect);

  Node* load = graph()->NewNode(simplified()->LoadField(access), region0,
                                effect, control);
  EXPECT_CALL(editor, ReplaceWithValue(load, value0, effect, _));
  Reduction r = load_elimination.Reduce(load);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(value0, r.replacement());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
