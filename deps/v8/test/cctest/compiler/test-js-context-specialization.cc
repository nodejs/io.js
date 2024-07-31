// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/tick-counter.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/js-context-specialization.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/heap/factory.h"
#include "src/objects/contexts.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/function-tester.h"
#include "test/cctest/compiler/js-heap-broker-base.h"

namespace v8 {
namespace internal {
namespace compiler {

class ContextSpecializationTester : public HandleAndZoneScope,
                                    public JSHeapBrokerTestBase {
 public:
  explicit ContextSpecializationTester(Maybe<OuterContext> context)
      : HandleAndZoneScope(kCompressGraphZone),
        JSHeapBrokerTestBase(main_isolate(), main_zone()),
        dependencies_(broker(), main_zone()),
        graph_(main_zone()->New<Graph>(main_zone())),
        common_(main_zone()),
        javascript_(main_zone()),
        machine_(main_zone()),
        simplified_(main_zone()),
        jsgraph_(main_isolate(), graph(), common(), &javascript_, &simplified_,
                 &machine_),
        reducer_(main_zone(), graph(), &tick_counter_, broker()),
        spec_(&reducer_, jsgraph(), broker(), context,
              MaybeHandle<JSFunction>()) {}
  ContextSpecializationTester(Maybe<OuterContext> context,
                              CanonicalHandles&& handles)
      : HandleAndZoneScope(kCompressGraphZone),
        JSHeapBrokerTestBase(main_isolate(), main_zone(), std::move(handles)),
        dependencies_(broker(), main_zone()),
        graph_(main_zone()->New<Graph>(main_zone())),
        common_(main_zone()),
        javascript_(main_zone()),
        machine_(main_zone()),
        simplified_(main_zone()),
        jsgraph_(main_isolate(), graph(), common(), &javascript_, &simplified_,
                 &machine_),
        reducer_(main_zone(), graph(), &tick_counter_, broker()),
        spec_(&reducer_, jsgraph(), broker(), context,
              MaybeHandle<JSFunction>()) {}

  JSContextSpecialization* spec() { return &spec_; }
  Isolate* isolate() { return main_isolate(); }
  Factory* factory() { return main_isolate()->factory(); }
  CommonOperatorBuilder* common() { return &common_; }
  JSOperatorBuilder* javascript() { return &javascript_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }
  JSGraph* jsgraph() { return &jsgraph_; }
  Graph* graph() { return graph_; }

  void CheckChangesToValue(Node* node, Handle<HeapObject> expected_value);
  void CheckContextInputAndDepthChanges(
      Node* node, Handle<Context> expected_new_context_object,
      size_t expected_new_depth);
  void CheckContextInputAndDepthChanges(Node* node, Node* expected_new_context,
                                        size_t expected_new_depth);

 private:
  CompilationDependencies dependencies_;
  TickCounter tick_counter_;
  Graph* graph_;
  CommonOperatorBuilder common_;
  JSOperatorBuilder javascript_;
  MachineOperatorBuilder machine_;
  SimplifiedOperatorBuilder simplified_;
  JSGraph jsgraph_;
  GraphReducer reducer_;
  JSContextSpecialization spec_;
};

void ContextSpecializationTester::CheckChangesToValue(
    Node* node, Handle<HeapObject> expected_value) {
  Reduction r = spec()->Reduce(node);
  CHECK(r.Changed());
  HeapObjectMatcher match(r.replacement());
  CHECK(match.HasResolvedValue());
  CHECK_EQ(*match.ResolvedValue(), *expected_value);
}

void ContextSpecializationTester::CheckContextInputAndDepthChanges(
    Node* node, Handle<Context> expected_new_context_object,
    size_t expected_new_depth) {
  ContextAccess access = ContextAccessOf(node->op());
  Reduction r = spec()->Reduce(node);
  CHECK(r.Changed());

  Node* new_context = NodeProperties::GetContextInput(r.replacement());
  CHECK_EQ(IrOpcode::kHeapConstant, new_context->opcode());
  HeapObjectMatcher match(new_context);
  CHECK_EQ(Context::cast(*match.ResolvedValue()), *expected_new_context_object);

  ContextAccess new_access = ContextAccessOf(r.replacement()->op());
  CHECK_EQ(new_access.depth(), expected_new_depth);
  CHECK_EQ(new_access.index(), access.index());
  CHECK_EQ(new_access.immutable(), access.immutable());
}

void ContextSpecializationTester::CheckContextInputAndDepthChanges(
    Node* node, Node* expected_new_context, size_t expected_new_depth) {
  ContextAccess access = ContextAccessOf(node->op());
  Reduction r = spec()->Reduce(node);
  CHECK(r.Changed());

  Node* new_context = NodeProperties::GetContextInput(r.replacement());
  CHECK_EQ(new_context, expected_new_context);

  ContextAccess new_access = ContextAccessOf(r.replacement()->op());
  CHECK_EQ(new_access.depth(), expected_new_depth);
  CHECK_EQ(new_access.index(), access.index());
  CHECK_EQ(new_access.immutable(), access.immutable());
}

namespace {

Handle<Context> NewContextForTesting(Isolate* isolate,
                                     Handle<Context> previous) {
  Handle<ScopeInfo> scope_info = ScopeInfo::CreateForWithScope(isolate, {});
  Handle<JSObject> extension = isolate->factory()->NewJSObjectWithNullProto();
  return isolate->factory()->NewWithContext(previous, scope_info, extension);
}

Handle<Context> NewCanonicalContextForTesting(ContextSpecializationTester& t,
                                              Handle<Context> previous) {
  Handle<ScopeInfo> scope_info =
      t.CanonicalHandle(*ScopeInfo::CreateForWithScope(t.isolate(), {}));
  Handle<JSObject> extension =
      t.CanonicalHandle(*t.isolate()->factory()->NewJSObjectWithNullProto());
  return t.CanonicalHandle(
      *t.isolate()->factory()->NewWithContext(previous, scope_info, extension));
}

}  // namespace

static const int slot_index = 5;

TEST(ReduceJSLoadContext0) {
  ContextSpecializationTester t(Nothing<OuterContext>());

  Node* start = t.graph()->NewNode(t.common()->Start(0));
  t.graph()->SetStart(start);

  // Make a context and initialize it a bit for this test.
  Handle<Context> native = t.CanonicalHandle(*t.factory()->NewNativeContext());
  Handle<Context> subcontext1 = NewCanonicalContextForTesting(t, native);
  Handle<Context> subcontext2 = NewCanonicalContextForTesting(t, subcontext1);
  Handle<Object> expected =
      t.CanonicalHandle(*t.factory()->InternalizeUtf8String("gboy!"));
  const int slot = 5;
  native->set(slot, *expected);

  Node* const_context =
      t.jsgraph()->ConstantNoHole(MakeRef(t.broker(), native), t.broker());
  Node* deep_const_context =
      t.jsgraph()->ConstantNoHole(MakeRef(t.broker(), subcontext2), t.broker());
  Node* param_context = t.graph()->NewNode(t.common()->Parameter(0), start);

  {
    // Mutable slot, constant context, depth = 0 => do nothing.
    Node* load = t.graph()->NewNode(t.javascript()->LoadContext(0, 0, false),
                                    const_context, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(!r.Changed());
  }

  {
    // Mutable slot, non-constant context, depth = 0 => do nothing.
    Node* load = t.graph()->NewNode(t.javascript()->LoadContext(0, 0, false),
                                    param_context, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(!r.Changed());
  }

  {
    // Mutable slot, constant context, depth > 0 => fold-in parent context.
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(2, Context::GLOBAL_EVAL_FUN_INDEX, false),
        deep_const_context, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(r.Changed());
    Node* new_context_input = NodeProperties::GetContextInput(r.replacement());
    CHECK_EQ(IrOpcode::kHeapConstant, new_context_input->opcode());
    HeapObjectMatcher match(new_context_input);
    CHECK_EQ(*native, Context::cast(*match.ResolvedValue()));
    ContextAccess access = ContextAccessOf(r.replacement()->op());
    CHECK_EQ(Context::GLOBAL_EVAL_FUN_INDEX, static_cast<int>(access.index()));
    CHECK_EQ(0, static_cast<int>(access.depth()));
    CHECK_EQ(false, access.immutable());
  }

  {
    // Immutable slot, constant context, depth = 0 => specialize.
    Node* load = t.graph()->NewNode(t.javascript()->LoadContext(0, slot, true),
                                    const_context, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(r.Changed());
    CHECK(r.replacement() != load);

    HeapObjectMatcher match(r.replacement());
    CHECK(match.HasResolvedValue());
    CHECK_EQ(*expected, *match.ResolvedValue());
  }

  // Clean up so that verifiers don't complain.
  native->set(slot, Smi::zero());
}

TEST(ReduceJSLoadContext1) {
  // The graph's context chain ends in the incoming context parameter:
  //
  //   context2 <-- context1 <-- context0 (= Parameter(0))

  ContextSpecializationTester t(Nothing<OuterContext>());

  Node* start = t.graph()->NewNode(t.common()->Start(0));
  t.graph()->SetStart(start);
  ScopeInfoRef empty = t.broker()->empty_scope_info();
  const i::compiler::Operator* create_function_context =
      t.javascript()->CreateFunctionContext(empty, 42, FUNCTION_SCOPE);

  Node* context0 = t.graph()->NewNode(t.common()->Parameter(0), start);
  Node* context1 =
      t.graph()->NewNode(create_function_context, context0, start, start);
  Node* context2 =
      t.graph()->NewNode(create_function_context, context1, start, start);

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(0, slot_index, false), context2, start);
    CHECK(!t.spec()->Reduce(load).Changed());
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(0, slot_index, true), context2, start);
    CHECK(!t.spec()->Reduce(load).Changed());
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(1, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context1, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(1, slot_index, true), context2, start);
    t.CheckContextInputAndDepthChanges(load, context1, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(2, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context0, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(2, slot_index, true), context2, start);
    t.CheckContextInputAndDepthChanges(load, context0, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(3, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context0, 1);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(3, slot_index, true), context2, start);
    t.CheckContextInputAndDepthChanges(load, context0, 1);
  }
}

TEST(ReduceJSLoadContext2) {
  // The graph's context chain ends in a constant context (context_object1),
  // which has another outer context (context_object0).
  //
  //   context2 <-- context1 <-- context0 (=
  //   HeapConstantNoHole(context_object1))
  //   context_object1 <~~ context_object0

  ContextSpecializationTester t(Nothing<OuterContext>());

  Node* start = t.graph()->NewNode(t.common()->Start(0));
  t.graph()->SetStart(start);
  ScopeInfoRef empty = t.broker()->empty_scope_info();
  const i::compiler::Operator* create_function_context =
      t.javascript()->CreateFunctionContext(empty, 42, FUNCTION_SCOPE);

  Handle<HeapObject> slot_value0 =
      t.CanonicalHandle(*t.factory()->InternalizeUtf8String("0"));
  Handle<HeapObject> slot_value1 =
      t.CanonicalHandle(*t.factory()->InternalizeUtf8String("1"));

  Handle<Context> context_object0 =
      t.CanonicalHandle(*t.factory()->NewNativeContext());
  Handle<Context> context_object1 =
      NewCanonicalContextForTesting(t, context_object0);
  context_object0->set_extension(*slot_value0);
  context_object1->set_extension(*slot_value1);

  Node* context0 = t.jsgraph()->ConstantNoHole(
      MakeRef(t.broker(), context_object1), t.broker());
  Node* context1 =
      t.graph()->NewNode(create_function_context, context0, start, start);
  Node* context2 =
      t.graph()->NewNode(create_function_context, context1, start, start);

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(0, slot_index, false), context2, start);
    CHECK(!t.spec()->Reduce(load).Changed());
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(0, slot_index, true), context2, start);
    CHECK(!t.spec()->Reduce(load).Changed());
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(1, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context1, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(1, slot_index, true), context2, start);
    t.CheckContextInputAndDepthChanges(load, context1, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(2, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context0, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(2, Context::EXTENSION_INDEX, true),
        context2, start);
    t.CheckChangesToValue(load, slot_value1);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(3, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context_object0, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(3, Context::EXTENSION_INDEX, true),
        context2, start);
    t.CheckChangesToValue(load, slot_value0);
  }
}

TEST(ReduceJSLoadContext3) {
  // Like in ReduceJSLoadContext1, the graph's context chain ends in the
  // incoming context parameter.  However, this time we provide a concrete
  // context for this parameter as the "specialization context".  We choose
  // context_object2 from ReduceJSLoadContext2 for this, so almost all test
  // expectations are the same as in ReduceJSLoadContext2.

  HandleAndZoneScope handle_zone_scope;
  auto isolate = handle_zone_scope.main_isolate();
  auto factory = isolate->factory();

  Handle<HeapObject> slot_value0 = factory->InternalizeUtf8String("0");
  Handle<HeapObject> slot_value1 = factory->InternalizeUtf8String("1");

  CanonicalHandles canonical_handles(isolate, handle_zone_scope.main_zone());

  Handle<Context> context_object0 =
      canonical_handles.Create(factory->NewNativeContext());
  Handle<Context> context_object1 =
      canonical_handles.Create(NewContextForTesting(isolate, context_object0));
  context_object0->set_extension(*slot_value0);
  context_object1->set_extension(*slot_value1);

  ContextSpecializationTester t(Just(OuterContext(context_object1, 0)),
                                std::move(canonical_handles));

  Node* start = t.graph()->NewNode(t.common()->Start(2));
  t.graph()->SetStart(start);
  ScopeInfoRef empty = t.broker()->empty_scope_info();
  const i::compiler::Operator* create_function_context =
      t.javascript()->CreateFunctionContext(empty, 42, FUNCTION_SCOPE);

  Node* context0 = t.graph()->NewNode(t.common()->Parameter(0), start);
  Node* context1 =
      t.graph()->NewNode(create_function_context, context0, start, start);
  Node* context2 =
      t.graph()->NewNode(create_function_context, context1, start, start);

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(0, slot_index, false), context2, start);
    CHECK(!t.spec()->Reduce(load).Changed());
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(0, slot_index, true), context2, start);
    CHECK(!t.spec()->Reduce(load).Changed());
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(1, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context1, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(1, slot_index, true), context2, start);
    t.CheckContextInputAndDepthChanges(load, context1, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(2, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context_object1, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(2, Context::EXTENSION_INDEX, true),
        context2, start);
    t.CheckChangesToValue(load, slot_value1);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(3, slot_index, false), context2, start);
    t.CheckContextInputAndDepthChanges(load, context_object0, 0);
  }

  {
    Node* load = t.graph()->NewNode(
        t.javascript()->LoadContext(3, Context::EXTENSION_INDEX, true),
        context2, start);
    t.CheckChangesToValue(load, slot_value0);
  }
}

TEST(ReduceJSStoreContext0) {
  ContextSpecializationTester t(Nothing<OuterContext>());

  Node* start = t.graph()->NewNode(t.common()->Start(0));
  t.graph()->SetStart(start);

  // Make a context and initialize it a bit for this test.
  Handle<Context> native = t.CanonicalHandle(*t.factory()->NewNativeContext());
  Handle<Context> subcontext1 = NewCanonicalContextForTesting(t, native);
  Handle<Context> subcontext2 = NewCanonicalContextForTesting(t, subcontext1);
  Handle<Object> expected =
      t.CanonicalHandle(*t.factory()->InternalizeUtf8String("gboy!"));
  const int slot = 5;
  native->set(slot, *expected);

  Node* const_context =
      t.jsgraph()->ConstantNoHole(MakeRef(t.broker(), native), t.broker());
  Node* deep_const_context =
      t.jsgraph()->ConstantNoHole(MakeRef(t.broker(), subcontext2), t.broker());
  Node* param_context = t.graph()->NewNode(t.common()->Parameter(0), start);

  {
    // Mutable slot, constant context, depth = 0 => do nothing.
    Node* load = t.graph()->NewNode(t.javascript()->StoreContext(0, 0),
                                    const_context, const_context, start, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(!r.Changed());
  }

  {
    // Mutable slot, non-constant context, depth = 0 => do nothing.
    Node* load = t.graph()->NewNode(t.javascript()->StoreContext(0, 0),
                                    param_context, param_context, start, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(!r.Changed());
  }

  {
    // Immutable slot, constant context, depth = 0 => do nothing.
    Node* load = t.graph()->NewNode(t.javascript()->StoreContext(0, slot),
                                    const_context, const_context, start, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(!r.Changed());
  }

  {
    // Mutable slot, constant context, depth > 0 => fold-in parent context.
    Node* load = t.graph()->NewNode(
        t.javascript()->StoreContext(2, Context::GLOBAL_EVAL_FUN_INDEX),
        deep_const_context, deep_const_context, start, start);
    Reduction r = t.spec()->Reduce(load);
    CHECK(r.Changed());
    Node* new_context_input = NodeProperties::GetContextInput(r.replacement());
    CHECK_EQ(IrOpcode::kHeapConstant, new_context_input->opcode());
    HeapObjectMatcher match(new_context_input);
    CHECK_EQ(*native, Context::cast(*match.ResolvedValue()));
    ContextAccess access = ContextAccessOf(r.replacement()->op());
    CHECK_EQ(Context::GLOBAL_EVAL_FUN_INDEX, static_cast<int>(access.index()));
    CHECK_EQ(0, static_cast<int>(access.depth()));
    CHECK_EQ(false, access.immutable());
  }

  // Clean up so that verifiers don't complain.
  native->set(slot, Smi::zero());
}

TEST(ReduceJSStoreContext1) {
  ContextSpecializationTester t(Nothing<OuterContext>());

  Node* start = t.graph()->NewNode(t.common()->Start(0));
  t.graph()->SetStart(start);
  ScopeInfoRef empty = t.broker()->empty_scope_info();
  const i::compiler::Operator* create_function_context =
      t.javascript()->CreateFunctionContext(empty, 42, FUNCTION_SCOPE);

  Node* context0 = t.graph()->NewNode(t.common()->Parameter(0), start);
  Node* context1 =
      t.graph()->NewNode(create_function_context, context0, start, start);
  Node* context2 =
      t.graph()->NewNode(create_function_context, context1, start, start);

  {
    Node* store =
        t.graph()->NewNode(t.javascript()->StoreContext(0, slot_index),
                           context2, context2, start, start);
    CHECK(!t.spec()->Reduce(store).Changed());
  }

  {
    Node* store =
        t.graph()->NewNode(t.javascript()->StoreContext(1, slot_index),
                           context2, context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context1, 0);
  }

  {
    Node* store =
        t.graph()->NewNode(t.javascript()->StoreContext(2, slot_index),
                           context2, context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context0, 0);
  }

  {
    Node* store =
        t.graph()->NewNode(t.javascript()->StoreContext(3, slot_index),
                           context2, context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context0, 1);
  }
}

TEST(ReduceJSStoreContext2) {
  ContextSpecializationTester t(Nothing<OuterContext>());

  Node* start = t.graph()->NewNode(t.common()->Start(0));
  t.graph()->SetStart(start);
  ScopeInfoRef empty = t.broker()->empty_scope_info();
  const i::compiler::Operator* create_function_context =
      t.javascript()->CreateFunctionContext(empty, 42, FUNCTION_SCOPE);

  Handle<HeapObject> slot_value0 =
      t.CanonicalHandle(*t.factory()->InternalizeUtf8String("0"));
  Handle<HeapObject> slot_value1 =
      t.CanonicalHandle(*t.factory()->InternalizeUtf8String("1"));

  Handle<Context> context_object0 =
      t.CanonicalHandle(*t.factory()->NewNativeContext());
  Handle<Context> context_object1 =
      NewCanonicalContextForTesting(t, context_object0);
  context_object0->set_extension(*slot_value0);
  context_object1->set_extension(*slot_value1);

  Node* context0 = t.jsgraph()->ConstantNoHole(
      MakeRef(t.broker(), context_object1), t.broker());
  Node* context1 =
      t.graph()->NewNode(create_function_context, context0, start, start);
  Node* context2 =
      t.graph()->NewNode(create_function_context, context1, start, start);

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(0, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    CHECK(!t.spec()->Reduce(store).Changed());
  }

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(1, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context1, 0);
  }

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(2, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context0, 0);
  }

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(3, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context_object0, 0);
  }
}

TEST(ReduceJSStoreContext3) {
  HandleAndZoneScope handle_zone_scope;
  auto isolate = handle_zone_scope.main_isolate();
  auto factory = isolate->factory();

  CanonicalHandles canonical_handles(isolate, handle_zone_scope.main_zone());

  Handle<HeapObject> slot_value0 =
      canonical_handles.Create(factory->InternalizeUtf8String("0"));
  Handle<HeapObject> slot_value1 =
      canonical_handles.Create(factory->InternalizeUtf8String("1"));

  Handle<Context> context_object0 =
      canonical_handles.Create(factory->NewNativeContext());
  Handle<Context> context_object1 =
      canonical_handles.Create(NewContextForTesting(isolate, context_object0));
  context_object0->set_extension(*slot_value0);
  context_object1->set_extension(*slot_value1);

  ContextSpecializationTester t(Just(OuterContext(context_object1, 0)),
                                std::move(canonical_handles));

  Node* start = t.graph()->NewNode(t.common()->Start(2));
  t.graph()->SetStart(start);
  ScopeInfoRef empty = t.broker()->empty_scope_info();
  const i::compiler::Operator* create_function_context =
      t.javascript()->CreateFunctionContext(empty, 42, FUNCTION_SCOPE);

  Node* context0 = t.graph()->NewNode(t.common()->Parameter(0), start);
  Node* context1 =
      t.graph()->NewNode(create_function_context, context0, start, start);
  Node* context2 =
      t.graph()->NewNode(create_function_context, context1, start, start);

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(0, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    CHECK(!t.spec()->Reduce(store).Changed());
  }

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(1, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context1, 0);
  }

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(2, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context_object1, 0);
  }

  {
    Node* store = t.graph()->NewNode(
        t.javascript()->StoreContext(3, Context::EXTENSION_INDEX), context2,
        context2, start, start);
    t.CheckContextInputAndDepthChanges(store, context_object0, 0);
  }
}

TEST(SpecializeJSFunction_ToConstant1) {
  FunctionTester T(
      "(function() { var x = 1; function inc(a)"
      " { return a + x; } return inc; })()");

  T.CheckCall(1.0, 0.0, 0.0);
  T.CheckCall(2.0, 1.0, 0.0);
  T.CheckCall(2.1, 1.1, 0.0);
}


TEST(SpecializeJSFunction_ToConstant2) {
  FunctionTester T(
      "(function() { var x = 1.5; var y = 2.25; var z = 3.75;"
      " function f(a) { return a - x + y - z; } return f; })()");

  T.CheckCall(-3.0, 0.0, 0.0);
  T.CheckCall(-2.0, 1.0, 0.0);
  T.CheckCall(-1.9, 1.1, 0.0);
}


TEST(SpecializeJSFunction_ToConstant3) {
  FunctionTester T(
      "(function() { var x = -11.5; function inc()"
      " { return (function(a) { return a + x; }); }"
      " return inc(); })()");

  T.CheckCall(-11.5, 0.0, 0.0);
  T.CheckCall(-10.5, 1.0, 0.0);
  T.CheckCall(-10.4, 1.1, 0.0);
}


TEST(SpecializeJSFunction_ToConstant_uninit) {
  {
    FunctionTester T(
        "(function() { if (false) { var x = 1; } function inc(a)"
        " { return x; } return inc; })()");  // x is undefined!
    i::Isolate* isolate = CcTest::i_isolate();
    CHECK(IsUndefined(*T.Call(T.Val(0.0), T.Val(0.0)).ToHandleChecked(),
                      isolate));
    CHECK(IsUndefined(*T.Call(T.Val(2.0), T.Val(0.0)).ToHandleChecked(),
                      isolate));
    CHECK(IsUndefined(*T.Call(T.Val(-2.1), T.Val(0.0)).ToHandleChecked(),
                      isolate));
  }

  {
    FunctionTester T(
        "(function() { if (false) { var x = 1; } function inc(a)"
        " { return a + x; } return inc; })()");  // x is undefined!

    CHECK(IsNaN(*T.Call(T.Val(0.0), T.Val(0.0)).ToHandleChecked()));
    CHECK(IsNaN(*T.Call(T.Val(2.0), T.Val(0.0)).ToHandleChecked()));
    CHECK(IsNaN(*T.Call(T.Val(-2.1), T.Val(0.0)).ToHandleChecked()));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
