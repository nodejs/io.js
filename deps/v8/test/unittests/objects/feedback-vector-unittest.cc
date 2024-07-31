// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/execution/execution.h"
#include "src/heap/factory.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

class FeedbackVectorTest : public TestWithContext {
 protected:
  Handle<JSFunction> GetFunction(const char* name) {
    v8::MaybeLocal<v8::Value> v8_f =
        v8_context()->Global()->Get(v8_context(), NewString(name));
    Handle<JSFunction> f =
        Handle<JSFunction>::cast(v8::Utils::OpenHandle(*v8_f.ToLocalChecked()));
    return f;
  }
};

#define CHECK_SLOT_KIND(helper, index, expected_kind) \
  CHECK_EQ(expected_kind, helper.vector()->GetKind(helper.slot(index)));

TEST_F(FeedbackVectorTest, VectorStructure) {
  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();
  Zone zone(isolate->allocator(), ZONE_NAME);

  Handle<FeedbackVector> vector;

  {
    FeedbackVectorSpec one_slot(&zone);
    one_slot.AddForInSlot();
    vector = NewFeedbackVector(isolate, &one_slot);
    FeedbackVectorHelper helper(vector);
    CHECK_EQ(1, helper.slot_count());
  }

  {
    FeedbackVectorSpec one_icslot(&zone);
    one_icslot.AddCallICSlot();
    vector = NewFeedbackVector(isolate, &one_icslot);
    FeedbackVectorHelper helper(vector);
    CHECK_EQ(1, helper.slot_count());
  }

  {
    FeedbackVectorSpec spec(&zone);
    for (int i = 0; i < 3; i++) {
      spec.AddForInSlot();
    }
    for (int i = 0; i < 5; i++) {
      spec.AddCallICSlot();
    }
    vector = NewFeedbackVector(isolate, &spec);
    FeedbackVectorHelper helper(vector);
    CHECK_EQ(8, helper.slot_count());

    int index = vector->GetIndex(helper.slot(0));

    CHECK_EQ(helper.slot(0), vector->ToSlot(index));

    index = vector->GetIndex(helper.slot(3));
    CHECK_EQ(helper.slot(3), vector->ToSlot(index));

    index = vector->GetIndex(helper.slot(7));
    CHECK_EQ(3 + 4 * FeedbackMetadata::GetSlotSize(FeedbackSlotKind::kCall),
             index);
    CHECK_EQ(helper.slot(7), vector->ToSlot(index));

    CHECK_EQ(3 + 5 * FeedbackMetadata::GetSlotSize(FeedbackSlotKind::kCall),
             vector->length());
  }

  {
    FeedbackVectorSpec spec(&zone);
    spec.AddForInSlot();
    spec.AddCreateClosureSlot();
    spec.AddForInSlot();
    vector = NewFeedbackVector(isolate, &spec);
    FeedbackVectorHelper helper(vector);
    Tagged<FeedbackCell> cell = vector->closure_feedback_cell(0);
    CHECK_EQ(cell->value(), *factory->undefined_value());
  }
}

// IC slots need an encoding to recognize what is in there.
TEST_F(FeedbackVectorTest, VectorICMetadata) {
  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  Zone zone(isolate->allocator(), ZONE_NAME);

  FeedbackVectorSpec spec(&zone);
  // Set metadata.
  for (int i = 0; i < 40; i++) {
    switch (i % 4) {
      case 0:
        spec.AddForInSlot();
        break;
      case 1:
        spec.AddCallICSlot();
        break;
      case 2:
        spec.AddLoadICSlot();
        break;
      case 3:
        spec.AddKeyedLoadICSlot();
        break;
    }
  }

  Handle<FeedbackVector> vector = NewFeedbackVector(isolate, &spec);
  FeedbackVectorHelper helper(vector);
  CHECK_EQ(40, helper.slot_count());

  // Meanwhile set some feedback values and type feedback values to
  // verify the data structure remains intact.
  vector->SynchronizedSet(FeedbackSlot(0), *vector);

  // Verify the metadata is correctly set up from the spec.
  for (int i = 0; i < 40; i++) {
    FeedbackSlotKind kind = vector->GetKind(helper.slot(i));
    switch (i % 4) {
      case 0:
        CHECK_EQ(FeedbackSlotKind::kForIn, kind);
        break;
      case 1:
        CHECK_EQ(FeedbackSlotKind::kCall, kind);
        break;
      case 2:
        CHECK_EQ(FeedbackSlotKind::kLoadProperty, kind);
        break;
      case 3:
        CHECK_EQ(FeedbackSlotKind::kLoadKeyed, kind);
        break;
    }
  }
}

TEST_F(FeedbackVectorTest, VectorCallICStates) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "function foo() { return 17; };"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(a) { a(); } f(foo);");
  Handle<JSFunction> f = GetFunction("f");
  // There should be one IC.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());

  TryRunJS("f(function() { return 16; })");
  CHECK_EQ(InlineCacheState::GENERIC, nexus.ic_state());

  // After a collection, state should remain GENERIC.
  InvokeMajorGC();
  CHECK_EQ(InlineCacheState::GENERIC, nexus.ic_state());
}

// Test the Call IC states transfer with Function.prototype.apply
TEST_F(FeedbackVectorTest, VectorCallICStateApply) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "var F;"
      "%EnsureFeedbackVectorForFunction(foo);"
      "function foo() { return F.apply(null, arguments); }"
      "F = Math.min;"
      "foo();");
  Handle<JSFunction> foo = GetFunction("foo");
  Handle<JSFunction> F = GetFunction("F");
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(foo->feedback_vector(), isolate);
  FeedbackSlot slot(4);
  FeedbackNexus nexus(feedback_vector, slot);
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  CHECK_EQ(CallFeedbackContent::kReceiver, nexus.GetCallFeedbackContent());
  Tagged<HeapObject> heap_object;
  CHECK(nexus.GetFeedback().GetHeapObjectIfWeak(&heap_object));
  CHECK_EQ(*F, heap_object);

  TryRunJS(
      "F = Math.max;"
      "foo();");
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  CHECK_EQ(CallFeedbackContent::kTarget, nexus.GetCallFeedbackContent());
  CHECK(nexus.GetFeedback().GetHeapObjectIfWeak(&heap_object));
  CHECK_EQ(*isolate->function_prototype_apply(), heap_object);

  TryRunJS(
      "F.apply = (function () { return; });"
      "foo();");
  CHECK_EQ(InlineCacheState::GENERIC, nexus.ic_state());
}

TEST_F(FeedbackVectorTest, VectorCallFeedback) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "function foo() { return 17; }"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(a) { a(); } f(foo);");
  Handle<JSFunction> f = GetFunction("f");
  Handle<JSFunction> foo = GetFunction("foo");
  // There should be one IC.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);

  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  Tagged<HeapObject> heap_object;
  CHECK(nexus.GetFeedback().GetHeapObjectIfWeak(&heap_object));
  CHECK_EQ(*foo, heap_object);

  InvokeMajorGC();
  // It should stay monomorphic even after a GC.
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
}

TEST_F(FeedbackVectorTest, VectorPolymorphicCallFeedback) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  v8_flags.lazy_feedback_allocation = false;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  // Make sure the call feedback of a() in f() becomes polymorphic.
  TryRunJS(
      "function foo_maker() { return () => { return 17; } }"
      "a_foo = foo_maker();"
      "function f(a) { a(); } f(foo_maker());"
      "f(foo_maker());");
  Handle<JSFunction> f = GetFunction("f");
  Handle<JSFunction> a_foo = GetFunction("a_foo");
  // There should be one IC.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);

  CHECK_EQ(InlineCacheState::POLYMORPHIC, nexus.ic_state());
  Tagged<HeapObject> heap_object;
  CHECK(nexus.GetFeedback().GetHeapObjectIfWeak(&heap_object));
  CHECK(IsFeedbackCell(heap_object, isolate));
  // Ensure this is the feedback cell for the closure returned by
  // foo_maker.
  CHECK_EQ(heap_object, a_foo->raw_feedback_cell());
}

TEST_F(FeedbackVectorTest, VectorCallFeedbackForArray) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "function f(a) { a(); };"
      "%EnsureFeedbackVectorForFunction(f);"
      "f(Array);");
  Handle<JSFunction> f = GetFunction("f");
  // There should be one IC.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);

  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  Tagged<HeapObject> heap_object;
  CHECK(nexus.GetFeedback().GetHeapObjectIfWeak(&heap_object));
  CHECK_EQ(*isolate->array_function(), heap_object);

  InvokeMajorGC();
  // It should stay monomorphic even after a GC.
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
}

TEST_F(FeedbackVectorTest, VectorCallCounts) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();

  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "function foo() { return 17; }"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(a) { a(); } f(foo);");
  Handle<JSFunction> f = GetFunction("f");
  // There should be one IC.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());

  TryRunJS("f(foo); f(foo);");
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  CHECK_EQ(3, nexus.GetCallCount());

  // Send the IC megamorphic, but we should still have incrementing counts.
  TryRunJS("f(function() { return 12; });");
  CHECK_EQ(InlineCacheState::GENERIC, nexus.ic_state());
  CHECK_EQ(4, nexus.GetCallCount());
}

TEST_F(FeedbackVectorTest, VectorConstructCounts) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();

  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "function Foo() {}"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(a) { new a(); } f(Foo);");
  Handle<JSFunction> f = GetFunction("f");
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);

  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());

  CHECK(feedback_vector->Get(slot).IsWeak());

  TryRunJS("f(Foo); f(Foo);");
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  CHECK_EQ(3, nexus.GetCallCount());

  // Send the IC megamorphic, but we should still have incrementing counts.
  TryRunJS("f(function() {});");
  CHECK_EQ(InlineCacheState::GENERIC, nexus.ic_state());
  CHECK_EQ(4, nexus.GetCallCount());
}

TEST_F(FeedbackVectorTest, VectorSpeculationMode) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();

  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "function Foo() {}"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(a) { new a(); } f(Foo);");
  Handle<JSFunction> f = GetFunction("f");
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);

  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);
  CHECK_EQ(SpeculationMode::kAllowSpeculation, nexus.GetSpeculationMode());

  TryRunJS("f(Foo); f(Foo);");
  CHECK_EQ(3, nexus.GetCallCount());
  CHECK_EQ(SpeculationMode::kAllowSpeculation, nexus.GetSpeculationMode());

  nexus.SetSpeculationMode(SpeculationMode::kDisallowSpeculation);
  CHECK_EQ(SpeculationMode::kDisallowSpeculation, nexus.GetSpeculationMode());
  CHECK_EQ(3, nexus.GetCallCount());

  nexus.SetSpeculationMode(SpeculationMode::kAllowSpeculation);
  CHECK_EQ(SpeculationMode::kAllowSpeculation, nexus.GetSpeculationMode());
  CHECK_EQ(3, nexus.GetCallCount());
}

TEST_F(FeedbackVectorTest, VectorCallSpeculationModeAndFeedbackContent) {
  if (!i::v8_flags.use_ic) return;
  if (!i::v8_flags.turbofan) return;
  if (i::v8_flags.always_turbofan) return;
  if (i::v8_flags.jitless) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();

  TryRunJS(
      "function min() { return Math.min.apply(null, arguments); }"
      "function f(x) { return min(x, 0); }"
      "%PrepareFunctionForOptimization(min);"
      "%PrepareFunctionForOptimization(f);"
      "f(1);");
  Handle<JSFunction> min = GetFunction("min");
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(min->feedback_vector(), isolate);
  FeedbackSlot slot(6);
  FeedbackNexus nexus(feedback_vector, slot);

  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  CHECK_EQ(SpeculationMode::kAllowSpeculation, nexus.GetSpeculationMode());
  CHECK_EQ(CallFeedbackContent::kReceiver, nexus.GetCallFeedbackContent());
  TryRunJS("%OptimizeFunctionOnNextCall(f); f(1);");
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  CHECK_EQ(SpeculationMode::kAllowSpeculation, nexus.GetSpeculationMode());
  CHECK_EQ(CallFeedbackContent::kReceiver, nexus.GetCallFeedbackContent());
  TryRunJS("f({});");  // Deoptimizes.
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  CHECK_EQ(SpeculationMode::kDisallowSpeculation, nexus.GetSpeculationMode());
  CHECK_EQ(CallFeedbackContent::kReceiver, nexus.GetCallFeedbackContent());
}

TEST_F(FeedbackVectorTest, VectorLoadICStates) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();

  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "var o = { foo: 3 };"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(a) { return a.foo; } f(o);");
  Handle<JSFunction> f = GetFunction("f");
  // There should be one IC.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);

  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  // Verify that the monomorphic map is the one we expect.
  v8::MaybeLocal<v8::Value> v8_o =
      v8_context()->Global()->Get(v8_context(), NewString("o"));
  Handle<JSObject> o =
      Handle<JSObject>::cast(v8::Utils::OpenHandle(*v8_o.ToLocalChecked()));
  CHECK_EQ(o->map(), nexus.GetFirstMap());

  // Now go polymorphic.
  TryRunJS("f({ blarg: 3, foo: 2 })");
  CHECK_EQ(InlineCacheState::POLYMORPHIC, nexus.ic_state());

  TryRunJS(
      "delete o.foo;"
      "f(o)");
  CHECK_EQ(InlineCacheState::POLYMORPHIC, nexus.ic_state());

  TryRunJS("f({ blarg: 3, torino: 10, foo: 2 })");
  CHECK_EQ(InlineCacheState::POLYMORPHIC, nexus.ic_state());
  MapHandles maps;
  nexus.ExtractMaps(&maps);
  CHECK_EQ(4, maps.size());

  // Finally driven megamorphic.
  TryRunJS("f({ blarg: 3, gran: 3, torino: 10, foo: 2 })");
  CHECK_EQ(InlineCacheState::MEGAMORPHIC, nexus.ic_state());
  CHECK(nexus.GetFirstMap().is_null());

  // After a collection, state should not be reset to PREMONOMORPHIC.
  InvokeMajorGC();
  CHECK_EQ(InlineCacheState::MEGAMORPHIC, nexus.ic_state());
}

TEST_F(FeedbackVectorTest, VectorLoadGlobalICSlotSharing) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();

  // Function f has 5 LoadGlobalICs: 3 for {o} references outside of "typeof"
  // operator and 2 for {o} references inside "typeof" operator.
  TryRunJS(
      "o = 10;"
      "function f() {"
      "  var x = o || 10;"
      "  var y = typeof o;"
      "  return o , typeof o, x , y, o;"
      "}"
      "%EnsureFeedbackVectorForFunction(f);"
      "f();");
  Handle<JSFunction> f = GetFunction("f");
  // There should be two IC slots for {o} references outside and inside
  // typeof operator respectively.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackVectorHelper helper(feedback_vector);
  CHECK_EQ(2, helper.slot_count());
  CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
  CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kLoadGlobalInsideTypeof);
  FeedbackSlot slot1 = helper.slot(0);
  FeedbackSlot slot2 = helper.slot(1);
  CHECK_EQ(InlineCacheState::MONOMORPHIC,
           FeedbackNexus(feedback_vector, slot1).ic_state());
  CHECK_EQ(InlineCacheState::MONOMORPHIC,
           FeedbackNexus(feedback_vector, slot2).ic_state());
}

TEST_F(FeedbackVectorTest, VectorLoadICOnSmi) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();
  Heap* heap = isolate->heap();

  // Make sure function f has a call that uses a type feedback slot.
  TryRunJS(
      "var o = { foo: 3 };"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(a) { return a.foo; } f(34);");
  Handle<JSFunction> f = GetFunction("f");
  // There should be one IC.
  Handle<FeedbackVector> feedback_vector =
      Handle<FeedbackVector>(f->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  // Verify that the monomorphic map is the one we expect.
  Tagged<Map> number_map = ReadOnlyRoots(heap).heap_number_map();
  CHECK_EQ(number_map, nexus.GetFirstMap());

  // Now go polymorphic on o.
  TryRunJS("f(o)");
  CHECK_EQ(InlineCacheState::POLYMORPHIC, nexus.ic_state());

  MapHandles maps;
  nexus.ExtractMaps(&maps);
  CHECK_EQ(2, maps.size());

  // One of the maps should be the o map.
  v8::MaybeLocal<v8::Value> v8_o =
      v8_context()->Global()->Get(v8_context(), NewString("o"));
  Handle<JSObject> o =
      Handle<JSObject>::cast(v8::Utils::OpenHandle(*v8_o.ToLocalChecked()));
  bool number_map_found = false;
  bool o_map_found = false;
  for (Handle<Map> current : maps) {
    if (*current == number_map)
      number_map_found = true;
    else if (*current == o->map())
      o_map_found = true;
  }
  CHECK(number_map_found && o_map_found);

  // The degree of polymorphism doesn't change.
  TryRunJS("f(100)");
  CHECK_EQ(InlineCacheState::POLYMORPHIC, nexus.ic_state());
  MapHandles maps2;
  nexus.ExtractMaps(&maps2);
  CHECK_EQ(2, maps2.size());
}

TEST_F(FeedbackVectorTest, ReferenceContextAllocatesNoSlots) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());
  Isolate* isolate = i_isolate();

  {
    TryRunJS(
        "function testvar(x) {"
        "  y = x;"
        "  y = a;"
        "  return y;"
        "}"
        "%EnsureFeedbackVectorForFunction(testvar);"
        "a = 3;"
        "testvar({});");

    Handle<JSFunction> f = GetFunction("testvar");

    // There should be two LOAD_ICs, one for a and one for y at the end.
    Handle<FeedbackVector> feedback_vector =
        handle(f->feedback_vector(), isolate);
    FeedbackVectorHelper helper(feedback_vector);
    CHECK_EQ(3, helper.slot_count());
    CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kStoreGlobalSloppy);
    CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
    CHECK_SLOT_KIND(helper, 2, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
  }

  {
    TryRunJS(
        "function testprop(x) {"
        "  'use strict';"
        "  x.blue = a;"
        "}"
        "%EnsureFeedbackVectorForFunction(testprop);"
        "testprop({ blue: 3 });");

    Handle<JSFunction> f = GetFunction("testprop");

    // There should be one LOAD_IC, for the load of a.
    Handle<FeedbackVector> feedback_vector(f->feedback_vector(), isolate);
    FeedbackVectorHelper helper(feedback_vector);
    CHECK_EQ(2, helper.slot_count());
    CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
    CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kSetNamedStrict);
  }

  {
    TryRunJS(
        "function testpropfunc(x) {"
        "  x().blue = a;"
        "  return x().blue;"
        "}"
        "%EnsureFeedbackVectorForFunction(testpropfunc);"
        "function makeresult() { return { blue: 3 }; }"
        "testpropfunc(makeresult);");

    Handle<JSFunction> f = GetFunction("testpropfunc");

    // There should be 1 LOAD_GLOBAL_IC to load x (in both cases), 2 CALL_ICs
    // to call x and a LOAD_IC to load blue.
    Handle<FeedbackVector> feedback_vector(f->feedback_vector(), isolate);
    FeedbackVectorHelper helper(feedback_vector);
    CHECK_EQ(5, helper.slot_count());
    CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kCall);
    CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
    CHECK_SLOT_KIND(helper, 2, FeedbackSlotKind::kSetNamedSloppy);
    CHECK_SLOT_KIND(helper, 3, FeedbackSlotKind::kCall);
    CHECK_SLOT_KIND(helper, 4, FeedbackSlotKind::kLoadProperty);
  }

  {
    TryRunJS(
        "function testkeyedprop(x) {"
        "  x[0] = a;"
        "  return x[0];"
        "}"
        "%EnsureFeedbackVectorForFunction(testkeyedprop);"
        "testkeyedprop([0, 1, 2]);");

    Handle<JSFunction> f = GetFunction("testkeyedprop");

    // There should be 1 LOAD_GLOBAL_ICs for the load of a, and one
    // KEYED_LOAD_IC for the load of x[0] in the return statement.
    Handle<FeedbackVector> feedback_vector(f->feedback_vector(), isolate);
    FeedbackVectorHelper helper(feedback_vector);
    CHECK_EQ(3, helper.slot_count());
    CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
    CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kSetKeyedSloppy);
    CHECK_SLOT_KIND(helper, 2, FeedbackSlotKind::kLoadKeyed);
  }

  {
    TryRunJS(
        "function testkeyedprop(x) {"
        "  'use strict';"
        "  x[0] = a;"
        "  return x[0];"
        "}"
        "%EnsureFeedbackVectorForFunction(testkeyedprop);"
        "testkeyedprop([0, 1, 2]);");

    Handle<JSFunction> f = GetFunction("testkeyedprop");

    // There should be 1 LOAD_GLOBAL_ICs for the load of a, and one
    // KEYED_LOAD_IC for the load of x[0] in the return statement.
    Handle<FeedbackVector> feedback_vector(f->feedback_vector(), isolate);
    FeedbackVectorHelper helper(feedback_vector);
    CHECK_EQ(3, helper.slot_count());
    CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
    CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kSetKeyedStrict);
    CHECK_SLOT_KIND(helper, 2, FeedbackSlotKind::kLoadKeyed);
  }

  {
    TryRunJS(
        "function testcompound(x) {"
        "  'use strict';"
        "  x.old = x.young = x.in_between = a;"
        "  return x.old + x.young;"
        "}"
        "%EnsureFeedbackVectorForFunction(testcompound);"
        "testcompound({ old: 3, young: 3, in_between: 3 });");

    Handle<JSFunction> f = GetFunction("testcompound");

    // There should be 1 LOAD_GLOBAL_IC for load of a and 2 LOAD_ICs, for load
    // of x.old and x.young.
    Handle<FeedbackVector> feedback_vector(f->feedback_vector(), isolate);
    FeedbackVectorHelper helper(feedback_vector);
    CHECK_EQ(7, helper.slot_count());
    CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
    CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kSetNamedStrict);
    CHECK_SLOT_KIND(helper, 2, FeedbackSlotKind::kSetNamedStrict);
    CHECK_SLOT_KIND(helper, 3, FeedbackSlotKind::kSetNamedStrict);
    CHECK_SLOT_KIND(helper, 4, FeedbackSlotKind::kBinaryOp);
    CHECK_SLOT_KIND(helper, 5, FeedbackSlotKind::kLoadProperty);
    CHECK_SLOT_KIND(helper, 6, FeedbackSlotKind::kLoadProperty);
  }
}

TEST_F(FeedbackVectorTest, VectorStoreICBasic) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());

  TryRunJS(
      "function f(a) {"
      "  a.foo = 5;"
      "};"
      "%EnsureFeedbackVectorForFunction(f);"
      "var a = { foo: 3 };"
      "f(a);"
      "f(a);"
      "f(a);");
  Handle<JSFunction> f = GetFunction("f");
  // There should be one IC slot.
  Handle<FeedbackVector> feedback_vector(f->feedback_vector(), f->GetIsolate());
  FeedbackVectorHelper helper(feedback_vector);
  CHECK_EQ(1, helper.slot_count());
  FeedbackSlot slot(0);
  FeedbackNexus nexus(feedback_vector, slot);
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
}

TEST_F(FeedbackVectorTest, DefineNamedOwnIC) {
  if (!i::v8_flags.use_ic) return;
  if (i::v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;

  v8::HandleScope scope(v8_isolate());

  TryRunJS(
      "function f(v) {"
      "  return {a: 0, b: v, c: 0};"
      "}"
      "%EnsureFeedbackVectorForFunction(f);"
      "f(1);"
      "f(2);"
      "f(3);");
  Handle<JSFunction> f = GetFunction("f");
  // There should be one IC slot.
  Handle<FeedbackVector> feedback_vector(f->feedback_vector(), f->GetIsolate());
  FeedbackVectorHelper helper(feedback_vector);
  CHECK_EQ(2, helper.slot_count());
  CHECK_SLOT_KIND(helper, 0, FeedbackSlotKind::kLiteral);
  CHECK_SLOT_KIND(helper, 1, FeedbackSlotKind::kDefineNamedOwn);
  FeedbackNexus nexus(feedback_vector, helper.slot(1));
  CHECK_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
}

}  // namespace internal
}  // namespace v8
