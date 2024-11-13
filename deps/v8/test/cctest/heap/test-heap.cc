// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include <utility>

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/base/strings.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/compiler.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/script-details.h"
#include "src/common/globals.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/execution.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/combined-heap.h"
#include "src/heap/factory.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-verifier.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/large-page-metadata-inl.h"
#include "src/heap/large-spaces.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-barrier.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/memory-reducer.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/parked-scope.h"
#include "src/heap/remembered-set-inl.h"
#include "src/heap/safepoint.h"
#include "src/ic/ic.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/elements.h"
#include "src/objects/field-type.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/objects/transitions.h"
#include "src/regexp/regexp.h"
#include "src/snapshot/snapshot.h"
#include "src/tracing/tracing-category-observer.h"
#include "src/utils/ostreams.h"
#include "test/cctest/cctest.h"
#include "test/cctest/feedback-vector-helper.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/cctest/test-transitions.h"

namespace v8 {
namespace internal {
namespace heap {

// We only start allocation-site tracking with the second instantiation.
static const int kPretenureCreationCount =
    PretenuringHandler::GetMinMementoCountForTesting() + 1;

static void CheckMap(Tagged<Map> map, int type, int instance_size) {
  CHECK(IsHeapObject(map));
  DCHECK(IsValidHeapObject(CcTest::heap(), map));
  CHECK_EQ(ReadOnlyRoots(CcTest::heap()).meta_map(), map->map());
  CHECK_EQ(type, map->instance_type());
  CHECK_EQ(instance_size, map->instance_size());
}

TEST(HeapMaps) {
  CcTest::InitializeVM();
  ReadOnlyRoots roots(CcTest::heap());
  CheckMap(roots.meta_map(), MAP_TYPE, Map::kSize);
  CheckMap(roots.heap_number_map(), HEAP_NUMBER_TYPE, sizeof(HeapNumber));
  CheckMap(roots.fixed_array_map(), FIXED_ARRAY_TYPE, kVariableSizeSentinel);
  CheckMap(roots.hash_table_map(), HASH_TABLE_TYPE, kVariableSizeSentinel);
  CheckMap(roots.seq_two_byte_string_map(), SEQ_TWO_BYTE_STRING_TYPE,
           kVariableSizeSentinel);
}

static void VerifyStoredPrototypeMap(Isolate* isolate,
                                     int stored_map_context_index,
                                     int stored_ctor_context_index) {
  DirectHandle<Context> context = isolate->native_context();

  DirectHandle<Map> this_map(Cast<Map>(context->get(stored_map_context_index)),
                             isolate);

  DirectHandle<JSFunction> fun(
      Cast<JSFunction>(context->get(stored_ctor_context_index)), isolate);
  DirectHandle<JSObject> proto(Cast<JSObject>(fun->initial_map()->prototype()),
                               isolate);
  DirectHandle<Map> that_map(proto->map(), isolate);

  CHECK(proto->HasFastProperties());
  CHECK_EQ(*this_map, *that_map);
}

// Checks that critical maps stored on the context (mostly used for fast-path
// checks) are unchanged after initialization.
TEST(ContextMaps) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);

  VerifyStoredPrototypeMap(isolate,
                           Context::STRING_FUNCTION_PROTOTYPE_MAP_INDEX,
                           Context::STRING_FUNCTION_INDEX);
  VerifyStoredPrototypeMap(isolate, Context::REGEXP_PROTOTYPE_MAP_INDEX,
                           Context::REGEXP_FUNCTION_INDEX);
}

TEST(InitialObjects) {
  LocalContext env;
  HandleScope scope(CcTest::i_isolate());
  DirectHandle<Context> context = v8::Utils::OpenDirectHandle(*env);
  // Initial ArrayIterator prototype.
  CHECK_EQ(context->initial_array_iterator_prototype(),
           *v8::Utils::OpenDirectHandle(
               *CompileRun("[][Symbol.iterator]().__proto__")));
  // Initial Array prototype.
  CHECK_EQ(context->initial_array_prototype(),
           *v8::Utils::OpenDirectHandle(*CompileRun("Array.prototype")));
  // Initial Generator prototype.
  CHECK_EQ(context->initial_generator_prototype(),
           *v8::Utils::OpenDirectHandle(
               *CompileRun("(function*(){}).__proto__.prototype")));
  // Initial Iterator prototype.
  CHECK_EQ(context->initial_iterator_prototype(),
           *v8::Utils::OpenDirectHandle(
               *CompileRun("[][Symbol.iterator]().__proto__.__proto__")));
  // Initial Object prototype.
  CHECK_EQ(context->initial_object_prototype(),
           *v8::Utils::OpenDirectHandle(*CompileRun("Object.prototype")));
}

static void CheckOddball(Isolate* isolate, Tagged<Object> obj,
                         const char* string) {
  CHECK(IsOddball(obj));
  Handle<Object> handle(obj, isolate);
  Tagged<Object> print_string =
      *Object::ToString(isolate, handle).ToHandleChecked();
  CHECK(Cast<String>(print_string)->IsOneByteEqualTo(base::CStrVector(string)));
}

static void CheckSmi(Isolate* isolate, int value, const char* string) {
  Handle<Object> handle(Smi::FromInt(value), isolate);
  Tagged<Object> print_string =
      *Object::ToString(isolate, handle).ToHandleChecked();
  CHECK(Cast<String>(print_string)->IsOneByteEqualTo(base::CStrVector(string)));
}

static void CheckNumber(Isolate* isolate, double value, const char* string) {
  Handle<Object> number = isolate->factory()->NewNumber(value);
  CHECK(IsNumber(*number));
  DirectHandle<Object> print_string =
      Object::ToString(isolate, number).ToHandleChecked();
  CHECK(
      Cast<String>(*print_string)->IsOneByteEqualTo(base::CStrVector(string)));
}

void CheckEmbeddedObjectsAreEqual(Isolate* isolate, DirectHandle<Code> lhs,
                                  DirectHandle<Code> rhs) {
  int mode_mask = RelocInfo::ModeMask(RelocInfo::FULL_EMBEDDED_OBJECT);
  PtrComprCageBase cage_base(isolate);
  RelocIterator lhs_it(*lhs, mode_mask);
  RelocIterator rhs_it(*rhs, mode_mask);
  while (!lhs_it.done() && !rhs_it.done()) {
    CHECK_EQ(lhs_it.rinfo()->target_object(cage_base),
             rhs_it.rinfo()->target_object(cage_base));

    lhs_it.next();
    rhs_it.next();
  }
  CHECK(lhs_it.done() == rhs_it.done());
}

static void CheckGcSafeFindCodeForInnerPointer(Isolate* isolate) {
  // Test GcSafeFindCodeForInnerPointer
#define __ assm.

  Assembler assm(AssemblerOptions{});

  __ nop();  // supported on all architectures

  PtrComprCageBase cage_base(isolate);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  DirectHandle<InstructionStream> code(
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING)
          .Build()
          ->instruction_stream(),
      isolate);
  CHECK(IsInstructionStream(*code, cage_base));

  Tagged<HeapObject> obj = Cast<HeapObject>(*code);
  Address obj_addr = obj.address();

  for (int i = 0; i < obj->Size(cage_base); i += kTaggedSize) {
    Tagged<Code> lookup_result =
        isolate->heap()->FindCodeForInnerPointer(obj_addr + i);
    CHECK_EQ(*code, lookup_result->instruction_stream());
  }

  DirectHandle<InstructionStream> copy(
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING)
          .Build()
          ->instruction_stream(),
      isolate);
  Tagged<HeapObject> obj_copy = Cast<HeapObject>(*copy);
  Tagged<Code> not_right = isolate->heap()->FindCodeForInnerPointer(
      obj_copy.address() + obj_copy->Size(cage_base) / 2);
  CHECK_NE(not_right->instruction_stream(), *code);
  CHECK_EQ(not_right->instruction_stream(), *copy);
}

TEST(HandleNull) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope outer_scope(isolate);
  LocalContext context;
  Handle<Object> n(Tagged<Object>(kNullAddress), isolate);
  CHECK(!n.is_null());
}

TEST(HeapObjects) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();

  HandleScope sc(isolate);
  DirectHandle<Object> value = factory->NewNumber(1.000123);
  CHECK(IsHeapNumber(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(1.000123, Object::NumberValue(*value));

  value = factory->NewNumber(1.0);
  CHECK(IsSmi(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(1.0, Object::NumberValue(*value));

  value = factory->NewNumberFromInt(1024);
  CHECK(IsSmi(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(1024.0, Object::NumberValue(*value));

  value = factory->NewNumberFromInt(Smi::kMinValue);
  CHECK(IsSmi(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(Smi::kMinValue, Cast<Smi>(*value).value());

  value = factory->NewNumberFromInt(Smi::kMaxValue);
  CHECK(IsSmi(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(Smi::kMaxValue, Cast<Smi>(*value).value());

#if !defined(V8_TARGET_ARCH_64_BIT)
  // TODO(lrn): We need a NumberFromIntptr function in order to test this.
  value = factory->NewNumberFromInt(Smi::kMinValue - 1);
  CHECK(IsHeapNumber(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(static_cast<double>(Smi::kMinValue - 1),
           Object::NumberValue(*value));
#endif

  value = factory->NewNumberFromUint(static_cast<uint32_t>(Smi::kMaxValue) + 1);
  CHECK(IsHeapNumber(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(static_cast<double>(static_cast<uint32_t>(Smi::kMaxValue) + 1),
           Object::NumberValue(*value));

  value = factory->NewNumberFromUint(static_cast<uint32_t>(1) << 31);
  CHECK(IsHeapNumber(*value));
  CHECK(IsNumber(*value));
  CHECK_EQ(static_cast<double>(static_cast<uint32_t>(1) << 31),
           Object::NumberValue(*value));

  // nan oddball checks
  CHECK(IsNumber(*factory->nan_value()));
  CHECK(std::isnan(Object::NumberValue(*factory->nan_value())));

  DirectHandle<String> s = factory->NewStringFromStaticChars("fisk hest ");
  CHECK(IsString(*s));
  CHECK_EQ(10, s->length());

  Handle<String> object_string = Cast<String>(factory->Object_string());
  Handle<JSGlobalObject> global(CcTest::i_isolate()->context()->global_object(),
                                isolate);
  CHECK(Just(true) ==
        JSReceiver::HasOwnProperty(isolate, global, object_string));

  // Check ToString for oddballs
  ReadOnlyRoots roots(heap);
  CheckOddball(isolate, roots.true_value(), "true");
  CheckOddball(isolate, roots.false_value(), "false");
  CheckOddball(isolate, roots.null_value(), "null");
  CheckOddball(isolate, roots.undefined_value(), "undefined");

  // Check ToString for Smis
  CheckSmi(isolate, 0, "0");
  CheckSmi(isolate, 42, "42");
  CheckSmi(isolate, -42, "-42");

  // Check ToString for Numbers
  CheckNumber(isolate, 1.1, "1.1");

  CheckGcSafeFindCodeForInnerPointer(isolate);
}

TEST(Tagging) {
  CcTest::InitializeVM();
  int request = 24;
  CHECK_EQ(request, static_cast<int>(OBJECT_POINTER_ALIGN(request)));
  CHECK(IsSmi(Smi::FromInt(42)));
  CHECK(IsSmi(Smi::FromInt(Smi::kMinValue)));
  CHECK(IsSmi(Smi::FromInt(Smi::kMaxValue)));
}

TEST(GarbageCollection) {
  if (v8_flags.single_generation) return;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  HandleScope sc(isolate);
  // Check GC.
  heap::InvokeMinorGC(CcTest::heap());

  Handle<JSGlobalObject> global(CcTest::i_isolate()->context()->global_object(),
                                isolate);
  Handle<String> name = factory->InternalizeUtf8String("theFunction");
  Handle<String> prop_name = factory->InternalizeUtf8String("theSlot");
  Handle<String> prop_namex = factory->InternalizeUtf8String("theSlotx");
  Handle<String> obj_name = factory->InternalizeUtf8String("theObject");
  Handle<Smi> twenty_three(Smi::FromInt(23), isolate);
  Handle<Smi> twenty_four(Smi::FromInt(24), isolate);

  {
    HandleScope inner_scope(isolate);
    // Allocate a function and keep it in global object's property.
    Handle<JSFunction> function = factory->NewFunctionForTesting(name);
    Object::SetProperty(isolate, global, name, function).Check();
    // Allocate an object.  Unrooted after leaving the scope.
    Handle<JSObject> obj = factory->NewJSObject(function);
    Object::SetProperty(isolate, obj, prop_name, twenty_three).Check();
    Object::SetProperty(isolate, obj, prop_namex, twenty_four).Check();

    CHECK_EQ(Smi::FromInt(23),
             *Object::GetProperty(isolate, obj, prop_name).ToHandleChecked());
    CHECK_EQ(Smi::FromInt(24),
             *Object::GetProperty(isolate, obj, prop_namex).ToHandleChecked());
  }

  heap::InvokeMinorGC(CcTest::heap());

  // Function should be alive.
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, global, name));
  // Check function is retained.
  Handle<Object> func_value =
      Object::GetProperty(isolate, global, name).ToHandleChecked();
  CHECK(IsJSFunction(*func_value));
  Handle<JSFunction> function = Cast<JSFunction>(func_value);

  {
    HandleScope inner_scope(isolate);
    // Allocate another object, make it reachable from global.
    Handle<JSObject> obj = factory->NewJSObject(function);
    Object::SetProperty(isolate, global, obj_name, obj).Check();
    Object::SetProperty(isolate, obj, prop_name, twenty_three).Check();
  }

  // After gc, it should survive.
  heap::InvokeMinorGC(CcTest::heap());

  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, global, obj_name));
  Handle<Object> obj =
      Object::GetProperty(isolate, global, obj_name).ToHandleChecked();
  CHECK(IsJSObject(*obj));
  CHECK_EQ(Smi::FromInt(23),
           *Object::GetProperty(isolate, obj, prop_name).ToHandleChecked());
}

static void VerifyStringAllocation(Isolate* isolate, const char* string) {
  HandleScope scope(isolate);
  DirectHandle<String> s = isolate->factory()
                               ->NewStringFromUtf8(base::CStrVector(string))
                               .ToHandleChecked();
  CHECK_EQ(strlen(string), s->length());
  for (int index = 0; index < s->length(); index++) {
    CHECK_EQ(static_cast<uint16_t>(string[index]), s->Get(index));
  }
}

TEST(String) {
  CcTest::InitializeVM();
  Isolate* isolate = reinterpret_cast<Isolate*>(CcTest::isolate());

  VerifyStringAllocation(isolate, "a");
  VerifyStringAllocation(isolate, "ab");
  VerifyStringAllocation(isolate, "abc");
  VerifyStringAllocation(isolate, "abcd");
  VerifyStringAllocation(isolate, "fiskerdrengen er paa havet");
}

TEST(LocalHandles) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  v8::HandleScope scope(CcTest::isolate());
  const char* name = "Kasper the spunky";
  DirectHandle<String> string = factory->NewStringFromAsciiChecked(name);
  CHECK_EQ(strlen(name), string->length());
}

TEST(GlobalHandles) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  GlobalHandles* global_handles = isolate->global_handles();

  Handle<Object> h1;
  Handle<Object> h2;
  Handle<Object> h3;
  Handle<Object> h4;

  {
    HandleScope scope(isolate);

    DirectHandle<Object> i = factory->NewStringFromStaticChars("fisk");
    DirectHandle<Object> u = factory->NewNumber(1.12344);

    h1 = global_handles->Create(*i);
    h2 = global_handles->Create(*u);
    h3 = global_handles->Create(*i);
    h4 = global_handles->Create(*u);
  }

  // after gc, it should survive
  heap::InvokeMinorGC(CcTest::heap());

  CHECK(IsString(*h1));
  CHECK(IsHeapNumber(*h2));
  CHECK(IsString(*h3));
  CHECK(IsHeapNumber(*h4));

  CHECK_EQ(*h3, *h1);
  GlobalHandles::Destroy(h1.location());
  GlobalHandles::Destroy(h3.location());

  CHECK_EQ(*h4, *h2);
  GlobalHandles::Destroy(h2.location());
  GlobalHandles::Destroy(h4.location());
}

static bool WeakPointerCleared = false;

static void TestWeakGlobalHandleCallback(
    const v8::WeakCallbackInfo<void>& data) {
  std::pair<v8::Persistent<v8::Value>*, int>* p =
      reinterpret_cast<std::pair<v8::Persistent<v8::Value>*, int>*>(
          data.GetParameter());
  if (p->second == 1234) WeakPointerCleared = true;
  p->first->Reset();
}

TEST(WeakGlobalUnmodifiedApiHandlesScavenge) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  LocalContext context;
  Factory* factory = isolate->factory();
  GlobalHandles* global_handles = isolate->global_handles();

  WeakPointerCleared = false;

  IndirectHandle<Object> h1;
  IndirectHandle<Object> h2;

  {
    HandleScope scope(isolate);

    // Create an Api object that is unmodified.
    Local<v8::Function> function = FunctionTemplate::New(context->GetIsolate())
                                       ->GetFunction(context.local())
                                       .ToLocalChecked();
    Local<v8::Object> i =
        function->NewInstance(context.local()).ToLocalChecked();
    DirectHandle<Object> u = factory->NewNumber(1.12344);

    h1 = global_handles->Create(*u);
    h2 = global_handles->Create(internal::ValueHelper::ValueAsAddress(*i));
  }

  std::pair<Handle<Object>*, int> handle_and_id(&h2, 1234);
  GlobalHandles::MakeWeak(
      h2.location(), reinterpret_cast<void*>(&handle_and_id),
      &TestWeakGlobalHandleCallback, v8::WeakCallbackType::kParameter);

  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    v8_flags.single_generation ? heap::InvokeMajorGC(heap)
                               : heap::InvokeMinorGC(heap);
  }

  CHECK(IsHeapNumber(*h1));
  CHECK(WeakPointerCleared);
  GlobalHandles::Destroy(h1.location());
}

TEST(WeakGlobalHandlesMark) {
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  Factory* factory = isolate->factory();
  GlobalHandles* global_handles = isolate->global_handles();

  WeakPointerCleared = false;

  IndirectHandle<Object> h1;
  IndirectHandle<Object> h2;

  {
    HandleScope scope(isolate);

    DirectHandle<Object> i = factory->NewStringFromStaticChars("fisk");
    DirectHandle<Object> u = factory->NewNumber(1.12344);

    h1 = global_handles->Create(*i);
    h2 = global_handles->Create(*u);
  }

  // Make sure the objects are promoted.
  heap::EmptyNewSpaceUsingGC(heap);
  CHECK(!Heap::InYoungGeneration(*h1) && !Heap::InYoungGeneration(*h2));

  std::pair<Handle<Object>*, int> handle_and_id(&h2, 1234);
  GlobalHandles::MakeWeak(
      h2.location(), reinterpret_cast<void*>(&handle_and_id),
      &TestWeakGlobalHandleCallback, v8::WeakCallbackType::kParameter);

  // Incremental marking potentially marked handles before they turned weak.
  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(IsString(*h1));
  CHECK(WeakPointerCleared);
  GlobalHandles::Destroy(h1.location());
}

TEST(DeleteWeakGlobalHandle) {
  v8_flags.stress_compaction = false;
  v8_flags.stress_incremental_marking = false;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  Factory* factory = isolate->factory();
  GlobalHandles* global_handles = isolate->global_handles();

  WeakPointerCleared = false;
  IndirectHandle<Object> h;
  {
    HandleScope scope(isolate);

    DirectHandle<Object> i = factory->NewStringFromStaticChars("fisk");
    h = global_handles->Create(*i);
  }

  std::pair<Handle<Object>*, int> handle_and_id(&h, 1234);
  GlobalHandles::MakeWeak(h.location(), reinterpret_cast<void*>(&handle_and_id),
                          &TestWeakGlobalHandleCallback,
                          v8::WeakCallbackType::kParameter);
  CHECK(!WeakPointerCleared);
  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(WeakPointerCleared);
}

TEST(BytecodeArray) {
  if (!v8_flags.compact) return;
  static const uint8_t kRawBytes[] = {0xC3, 0x7E, 0xA5, 0x5A};
  static const int kRawBytesSize = sizeof(kRawBytes);
  static const int32_t kFrameSize = 32;
  static const uint16_t kParameterCount = 2;
  static const uint16_t kMaxArguments = 0;

  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  heap::SimulateFullSpace(heap->old_space());
  IndirectHandle<TrustedFixedArray> constant_pool =
      factory->NewTrustedFixedArray(5);
  for (int i = 0; i < 5; i++) {
    IndirectHandle<Object> number = factory->NewHeapNumber(i);
    constant_pool->set(i, *number);
  }

  IndirectHandle<TrustedByteArray> handler_table =
      factory->NewTrustedByteArray(3);

  // Allocate and initialize BytecodeArray
  IndirectHandle<BytecodeArray> array = factory->NewBytecodeArray(
      kRawBytesSize, kRawBytes, kFrameSize, kParameterCount, kMaxArguments,
      constant_pool, handler_table);

  CHECK(IsBytecodeArray(*array));
  CHECK_EQ(array->length(), (int)sizeof(kRawBytes));
  CHECK_EQ(array->frame_size(), kFrameSize);
  CHECK_EQ(array->parameter_count(), kParameterCount);
  CHECK_EQ(array->constant_pool(), *constant_pool);
  CHECK_EQ(array->handler_table(), *handler_table);
  CHECK_LE(array->address(), array->GetFirstBytecodeAddress());
  CHECK_GE(array->address() + array->BytecodeArraySize(),
           array->GetFirstBytecodeAddress() + array->length());
  for (int i = 0; i < kRawBytesSize; i++) {
    CHECK_EQ(Memory<uint8_t>(array->GetFirstBytecodeAddress() + i),
             kRawBytes[i]);
    CHECK_EQ(array->get(i), kRawBytes[i]);
  }

  Tagged<TrustedFixedArray> old_constant_pool_address = *constant_pool;

  // Perform a full garbage collection and force the constant pool to be on an
  // evacuation candidate.
  PageMetadata* evac_page = PageMetadata::FromHeapObject(*constant_pool);
  heap::ForceEvacuationCandidate(evac_page);
  {
    // We need to invoke GC without stack, otherwise no compaction is performed.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  // BytecodeArray should survive.
  CHECK_EQ(array->length(), kRawBytesSize);
  CHECK_EQ(array->frame_size(), kFrameSize);
  for (int i = 0; i < kRawBytesSize; i++) {
    CHECK_EQ(array->get(i), kRawBytes[i]);
    CHECK_EQ(Memory<uint8_t>(array->GetFirstBytecodeAddress() + i),
             kRawBytes[i]);
  }

  // Constant pool should have been migrated.
  CHECK_EQ(array->constant_pool().ptr(), constant_pool->ptr());
  CHECK_NE(array->constant_pool().ptr(), old_constant_pool_address.ptr());
}

static const char* not_so_random_string_table[] = {
    "abstract",   "boolean",      "break",      "byte",    "case",
    "catch",      "char",         "class",      "const",   "continue",
    "debugger",   "default",      "delete",     "do",      "double",
    "else",       "enum",         "export",     "extends", "false",
    "final",      "finally",      "float",      "for",     "function",
    "goto",       "if",           "implements", "import",  "in",
    "instanceof", "int",          "interface",  "long",    "native",
    "new",        "null",         "package",    "private", "protected",
    "public",     "return",       "short",      "static",  "super",
    "switch",     "synchronized", "this",       "throw",   "throws",
    "transient",  "true",         "try",        "typeof",  "var",
    "void",       "volatile",     "while",      "with",    nullptr};

static void CheckInternalizedStrings(const char** strings) {
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  for (const char* string = *strings; *strings != nullptr;
       string = *strings++) {
    HandleScope scope(isolate);
    DirectHandle<String> a =
        isolate->factory()->InternalizeUtf8String(base::CStrVector(string));
    // InternalizeUtf8String may return a failure if a GC is needed.
    CHECK(IsInternalizedString(*a));
    DirectHandle<String> b = factory->InternalizeUtf8String(string);
    CHECK_EQ(*b, *a);
    CHECK(b->IsOneByteEqualTo(base::CStrVector(string)));
    b = isolate->factory()->InternalizeUtf8String(base::CStrVector(string));
    CHECK_EQ(*b, *a);
    CHECK(b->IsOneByteEqualTo(base::CStrVector(string)));
  }
}

TEST(StringTable) {
  CcTest::InitializeVM();

  v8::HandleScope sc(CcTest::isolate());
  CheckInternalizedStrings(not_so_random_string_table);
  CheckInternalizedStrings(not_so_random_string_table);
}

TEST(FunctionAllocation) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  v8::HandleScope sc(CcTest::isolate());
  DirectHandle<String> name = factory->InternalizeUtf8String("theFunction");
  Handle<JSFunction> function = factory->NewFunctionForTesting(name);

  Handle<Smi> twenty_three(Smi::FromInt(23), isolate);
  Handle<Smi> twenty_four(Smi::FromInt(24), isolate);

  Handle<String> prop_name = factory->InternalizeUtf8String("theSlot");
  Handle<JSObject> obj = factory->NewJSObject(function);
  Object::SetProperty(isolate, obj, prop_name, twenty_three).Check();
  CHECK_EQ(Smi::FromInt(23),
           *Object::GetProperty(isolate, obj, prop_name).ToHandleChecked());
  // Check that we can add properties to function objects.
  Object::SetProperty(isolate, function, prop_name, twenty_four).Check();
  CHECK_EQ(
      Smi::FromInt(24),
      *Object::GetProperty(isolate, function, prop_name).ToHandleChecked());
}

TEST(ObjectProperties) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  v8::HandleScope sc(CcTest::isolate());
  Handle<String> object_string(
      Cast<String>(ReadOnlyRoots(CcTest::heap()).Object_string()), isolate);
  Handle<Object> object =
      Object::GetProperty(isolate, CcTest::i_isolate()->global_object(),
                          object_string)
          .ToHandleChecked();
  Handle<JSFunction> constructor = Cast<JSFunction>(object);
  Handle<JSObject> obj = factory->NewJSObject(constructor);
  Handle<String> first = factory->InternalizeUtf8String("first");
  Handle<String> second = factory->InternalizeUtf8String("second");

  Handle<Smi> one(Smi::FromInt(1), isolate);
  Handle<Smi> two(Smi::FromInt(2), isolate);

  // check for empty
  CHECK(Just(false) == JSReceiver::HasOwnProperty(isolate, obj, first));

  // add first
  Object::SetProperty(isolate, obj, first, one).Check();
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, first));

  // delete first
  CHECK(Just(true) ==
        JSReceiver::DeleteProperty(obj, first, LanguageMode::kSloppy));
  CHECK(Just(false) == JSReceiver::HasOwnProperty(isolate, obj, first));

  // add first and then second
  Object::SetProperty(isolate, obj, first, one).Check();
  Object::SetProperty(isolate, obj, second, two).Check();
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, first));
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, second));

  // delete first and then second
  CHECK(Just(true) ==
        JSReceiver::DeleteProperty(obj, first, LanguageMode::kSloppy));
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, second));
  CHECK(Just(true) ==
        JSReceiver::DeleteProperty(obj, second, LanguageMode::kSloppy));
  CHECK(Just(false) == JSReceiver::HasOwnProperty(isolate, obj, first));
  CHECK(Just(false) == JSReceiver::HasOwnProperty(isolate, obj, second));

  // add first and then second
  Object::SetProperty(isolate, obj, first, one).Check();
  Object::SetProperty(isolate, obj, second, two).Check();
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, first));
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, second));

  // delete second and then first
  CHECK(Just(true) ==
        JSReceiver::DeleteProperty(obj, second, LanguageMode::kSloppy));
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, first));
  CHECK(Just(true) ==
        JSReceiver::DeleteProperty(obj, first, LanguageMode::kSloppy));
  CHECK(Just(false) == JSReceiver::HasOwnProperty(isolate, obj, first));
  CHECK(Just(false) == JSReceiver::HasOwnProperty(isolate, obj, second));

  // check string and internalized string match
  const char* string1 = "fisk";
  Handle<String> s1 = factory->NewStringFromAsciiChecked(string1);
  Object::SetProperty(isolate, obj, s1, one).Check();
  Handle<String> s1_string = factory->InternalizeUtf8String(string1);
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, s1_string));

  // check internalized string and string match
  const char* string2 = "fugl";
  Handle<String> s2_string = factory->InternalizeUtf8String(string2);
  Object::SetProperty(isolate, obj, s2_string, one).Check();
  Handle<String> s2 = factory->NewStringFromAsciiChecked(string2);
  CHECK(Just(true) == JSReceiver::HasOwnProperty(isolate, obj, s2));
}

TEST(JSObjectMaps) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  v8::HandleScope sc(CcTest::isolate());
  DirectHandle<String> name = factory->InternalizeUtf8String("theFunction");
  Handle<JSFunction> function = factory->NewFunctionForTesting(name);

  Handle<String> prop_name = factory->InternalizeUtf8String("theSlot");
  Handle<JSObject> obj = factory->NewJSObject(function);
  DirectHandle<Map> initial_map(function->initial_map(), isolate);

  // Set a propery
  Handle<Smi> twenty_three(Smi::FromInt(23), isolate);
  Object::SetProperty(isolate, obj, prop_name, twenty_three).Check();
  CHECK_EQ(Smi::FromInt(23),
           *Object::GetProperty(isolate, obj, prop_name).ToHandleChecked());

  // Check the map has changed
  CHECK(*initial_map != obj->map());
}

TEST(JSArray) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  v8::HandleScope sc(CcTest::isolate());
  Handle<String> name = factory->InternalizeUtf8String("Array");
  Handle<Object> fun_obj =
      Object::GetProperty(isolate, CcTest::i_isolate()->global_object(), name)
          .ToHandleChecked();
  Handle<JSFunction> function = Cast<JSFunction>(fun_obj);

  // Allocate the object.
  DirectHandle<Object> element;
  Handle<JSObject> object = factory->NewJSObject(function);
  Handle<JSArray> array = Cast<JSArray>(object);
  // We just initialized the VM, no heap allocation failure yet.
  JSArray::Initialize(array, 0);

  // Set array length to 0.
  JSArray::SetLength(array, 0);
  CHECK_EQ(Smi::zero(), array->length());
  // Must be in fast mode.
  CHECK(array->HasSmiOrObjectElements());

  // array[length] = name.
  Object::SetElement(isolate, array, 0, name, ShouldThrow::kDontThrow).Check();
  CHECK_EQ(Smi::FromInt(1), array->length());
  element = i::Object::GetElement(isolate, array, 0).ToHandleChecked();
  CHECK_EQ(*element, *name);

  // Set array length with larger than smi value.
  JSArray::SetLength(array, static_cast<uint32_t>(Smi::kMaxValue) + 1);

  uint32_t int_length = 0;
  CHECK(Object::ToArrayIndex(array->length(), &int_length));
  CHECK_EQ(static_cast<uint32_t>(Smi::kMaxValue) + 1, int_length);
  CHECK(array->HasDictionaryElements());  // Must be in slow mode.

  // array[length] = name.
  Object::SetElement(isolate, array, int_length, name, ShouldThrow::kDontThrow)
      .Check();
  uint32_t new_int_length = 0;
  CHECK(Object::ToArrayIndex(array->length(), &new_int_length));
  CHECK_EQ(static_cast<double>(int_length), new_int_length - 1);
  element = Object::GetElement(isolate, array, int_length).ToHandleChecked();
  CHECK_EQ(*element, *name);
  element = Object::GetElement(isolate, array, 0).ToHandleChecked();
  CHECK_EQ(*element, *name);
}

TEST(JSObjectCopy) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  v8::HandleScope sc(CcTest::isolate());
  Handle<String> object_string(
      Cast<String>(ReadOnlyRoots(CcTest::heap()).Object_string()), isolate);
  Handle<Object> object =
      Object::GetProperty(isolate, CcTest::i_isolate()->global_object(),
                          object_string)
          .ToHandleChecked();
  Handle<JSFunction> constructor = Cast<JSFunction>(object);
  Handle<JSObject> obj = factory->NewJSObject(constructor);
  Handle<String> first = factory->InternalizeUtf8String("first");
  Handle<String> second = factory->InternalizeUtf8String("second");

  Handle<Smi> one(Smi::FromInt(1), isolate);
  Handle<Smi> two(Smi::FromInt(2), isolate);

  Object::SetProperty(isolate, obj, first, one).Check();
  Object::SetProperty(isolate, obj, second, two).Check();

  Object::SetElement(isolate, obj, 0, first, ShouldThrow::kDontThrow).Check();
  Object::SetElement(isolate, obj, 1, second, ShouldThrow::kDontThrow).Check();

  // Make the clone.
  DirectHandle<Object> value1, value2;
  Handle<JSObject> clone = factory->CopyJSObject(obj);
  CHECK(!clone.is_identical_to(obj));

  value1 = Object::GetElement(isolate, obj, 0).ToHandleChecked();
  value2 = Object::GetElement(isolate, clone, 0).ToHandleChecked();
  CHECK_EQ(*value1, *value2);
  value1 = Object::GetElement(isolate, obj, 1).ToHandleChecked();
  value2 = Object::GetElement(isolate, clone, 1).ToHandleChecked();
  CHECK_EQ(*value1, *value2);

  value1 = Object::GetProperty(isolate, obj, first).ToHandleChecked();
  value2 = Object::GetProperty(isolate, clone, first).ToHandleChecked();
  CHECK_EQ(*value1, *value2);
  value1 = Object::GetProperty(isolate, obj, second).ToHandleChecked();
  value2 = Object::GetProperty(isolate, clone, second).ToHandleChecked();
  CHECK_EQ(*value1, *value2);

  // Flip the values.
  Object::SetProperty(isolate, clone, first, two).Check();
  Object::SetProperty(isolate, clone, second, one).Check();

  Object::SetElement(isolate, clone, 0, second, ShouldThrow::kDontThrow)
      .Check();
  Object::SetElement(isolate, clone, 1, first, ShouldThrow::kDontThrow).Check();

  value1 = Object::GetElement(isolate, obj, 1).ToHandleChecked();
  value2 = Object::GetElement(isolate, clone, 0).ToHandleChecked();
  CHECK_EQ(*value1, *value2);
  value1 = Object::GetElement(isolate, obj, 0).ToHandleChecked();
  value2 = Object::GetElement(isolate, clone, 1).ToHandleChecked();
  CHECK_EQ(*value1, *value2);

  value1 = Object::GetProperty(isolate, obj, second).ToHandleChecked();
  value2 = Object::GetProperty(isolate, clone, first).ToHandleChecked();
  CHECK_EQ(*value1, *value2);
  value1 = Object::GetProperty(isolate, obj, first).ToHandleChecked();
  value2 = Object::GetProperty(isolate, clone, second).ToHandleChecked();
  CHECK_EQ(*value1, *value2);
}

TEST(StringAllocation) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  const unsigned char chars[] = {0xE5, 0xA4, 0xA7};
  for (int length = 0; length < 100; length++) {
    v8::HandleScope scope(CcTest::isolate());
    char* non_one_byte = NewArray<char>(3 * length + 1);
    char* one_byte = NewArray<char>(length + 1);
    non_one_byte[3 * length] = 0;
    one_byte[length] = 0;
    for (int i = 0; i < length; i++) {
      one_byte[i] = 'a';
      non_one_byte[3 * i] = chars[0];
      non_one_byte[3 * i + 1] = chars[1];
      non_one_byte[3 * i + 2] = chars[2];
    }
    DirectHandle<String> non_one_byte_sym = factory->InternalizeUtf8String(
        base::Vector<const char>(non_one_byte, 3 * length));
    CHECK_EQ(length, non_one_byte_sym->length());
    DirectHandle<String> one_byte_sym =
        factory->InternalizeString(base::OneByteVector(one_byte, length));
    CHECK_EQ(length, one_byte_sym->length());
    CHECK(one_byte_sym->HasHashCode());
    DirectHandle<String> non_one_byte_str =
        factory
            ->NewStringFromUtf8(
                base::Vector<const char>(non_one_byte, 3 * length))
            .ToHandleChecked();
    CHECK_EQ(length, non_one_byte_str->length());
    DirectHandle<String> one_byte_str =
        factory->NewStringFromUtf8(base::Vector<const char>(one_byte, length))
            .ToHandleChecked();
    CHECK_EQ(length, one_byte_str->length());
    DeleteArray(non_one_byte);
    DeleteArray(one_byte);
  }
}

static int ObjectsFoundInHeap(Heap* heap, Handle<Object> objs[], int size) {
  // Count the number of objects found in the heap.
  int found_count = 0;
  HeapObjectIterator iterator(heap);
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    for (int i = 0; i < size; i++) {
      // V8_EXTERNAL_CODE_SPACE specific: we might be comparing
      // InstructionStream object with non-InstructionStream object here and it
      // might produce false positives because operator== for tagged values
      // compares only lower 32 bits when pointer compression is enabled.
      if ((*objs[i]).ptr() == obj.ptr()) {
        found_count++;
      }
    }
  }
  return found_count;
}

TEST(Iteration) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Array of objects to scan heap for.
  const int objs_count = 6;
  Handle<Object> objs[objs_count];
  int next_objs_index = 0;

  // Allocate a JS array to OLD_SPACE and NEW_SPACE
  objs[next_objs_index++] = factory->NewJSArray(10);
  objs[next_objs_index++] =
      factory->NewJSArray(10, HOLEY_ELEMENTS, AllocationType::kOld);

  // Allocate a small string to OLD_DATA_SPACE and NEW_SPACE
  objs[next_objs_index++] = factory->NewStringFromStaticChars("abcdefghij");
  objs[next_objs_index++] =
      factory->NewStringFromStaticChars("abcdefghij", AllocationType::kOld);

  // Allocate a large string (for large object space).
  int large_size = kMaxRegularHeapObjectSize + 1;
  char* str = new char[large_size];
  for (int i = 0; i < large_size - 1; ++i) str[i] = 'a';
  str[large_size - 1] = '\0';
  objs[next_objs_index++] =
      factory->NewStringFromAsciiChecked(str, AllocationType::kOld);
  delete[] str;

  // Add a Map object to look for.
  objs[next_objs_index++] =
      Handle<Map>(Cast<HeapObject>(*objs[0])->map(), isolate);

  CHECK_EQ(objs_count, next_objs_index);
  CHECK_EQ(objs_count, ObjectsFoundInHeap(CcTest::heap(), objs, objs_count));
}

TEST(TestBytecodeFlushing) {
#if !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  v8_flags.turbofan = false;
  v8_flags.always_turbofan = false;
  i::v8_flags.optimize_for_size = false;
#endif  // !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
#ifdef V8_ENABLE_SPARKPLUG
  v8_flags.always_sparkplug = false;
#endif  // V8_ENABLE_SPARKPLUG
  i::v8_flags.flush_bytecode = true;
  i::v8_flags.allow_natives_syntax = true;

  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  Isolate* i_isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  Factory* factory = i_isolate->factory();

  {
    v8::HandleScope scope(isolate);
    v8::Context::New(isolate)->Enter();
    const char* source =
        "function foo() {"
        "  var x = 42;"
        "  var y = 42;"
        "  var z = x + y;"
        "};"
        "foo()";
    IndirectHandle<String> foo_name = factory->InternalizeUtf8String("foo");

    // This compile will add the code to the compilation cache.
    {
      v8::HandleScope new_scope(isolate);
      CompileRun(source);
    }

    // Check function is compiled.
    IndirectHandle<Object> func_value =
        Object::GetProperty(i_isolate, i_isolate->global_object(), foo_name)
            .ToHandleChecked();
    CHECK(IsJSFunction(*func_value));
    IndirectHandle<JSFunction> function = Cast<JSFunction>(func_value);
    CHECK(function->shared()->is_compiled());

    // The code will survive at least two GCs.
    {
      // In this test, we need to invoke GC without stack, otherwise some
      // objects may not be reclaimed because of conservative stack scanning.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
      heap::InvokeMajorGC(heap);
    }
    CHECK(function->shared()->is_compiled());

    i::SharedFunctionInfo::EnsureOldForTesting(function->shared());
    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
    }

    // foo should no longer be in the compilation cache
    CHECK(!function->shared()->is_compiled());
    CHECK(!function->is_compiled(i_isolate));
    // Call foo to get it recompiled.
    CompileRun("foo()");
    CHECK(function->shared()->is_compiled());
    CHECK(function->is_compiled(i_isolate));
  }
}

static void TestMultiReferencedBytecodeFlushing(bool sparkplug_compile) {
#if !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  v8_flags.turbofan = false;
  v8_flags.always_turbofan = false;
  i::v8_flags.optimize_for_size = false;
#endif  // !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
#ifdef V8_ENABLE_SPARKPLUG
  v8_flags.always_sparkplug = false;
  v8_flags.flush_baseline_code = true;
#else
  if (sparkplug_compile) return;
#endif  // V8_ENABLE_SPARKPLUG
  i::v8_flags.flush_bytecode = true;
  i::v8_flags.allow_natives_syntax = true;

  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  Isolate* i_isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  Factory* factory = i_isolate->factory();

  {
    v8::HandleScope scope(isolate);
    v8::Context::New(isolate)->Enter();
    const char* source =
        "function foo() {"
        "  var x = 42;"
        "  var y = 42;"
        "  var z = x + y;"
        "};"
        "foo()";
    IndirectHandle<String> foo_name = factory->InternalizeUtf8String("foo");

    // This compile will add the code to the compilation cache.
    {
      v8::HandleScope new_scope(isolate);
      CompileRun(source);
    }

    // Check function is compiled.
    IndirectHandle<Object> func_value =
        Object::GetProperty(i_isolate, i_isolate->global_object(), foo_name)
            .ToHandleChecked();
    CHECK(IsJSFunction(*func_value));
    IndirectHandle<JSFunction> function = Cast<JSFunction>(func_value);
    IndirectHandle<SharedFunctionInfo> shared(function->shared(), i_isolate);
    CHECK(shared->is_compiled());

    // Make a copy of the SharedFunctionInfo which points to the same bytecode.
    IndirectHandle<SharedFunctionInfo> copy =
        i_isolate->factory()->CloneSharedFunctionInfo(shared);

    if (sparkplug_compile) {
      v8::HandleScope baseline_compilation_scope(isolate);
      IsCompiledScope is_compiled_scope = copy->is_compiled_scope(i_isolate);
      Compiler::CompileSharedWithBaseline(
          i_isolate, copy, Compiler::CLEAR_EXCEPTION, &is_compiled_scope);
    }

    i::SharedFunctionInfo::EnsureOldForTesting(*shared);
    {
      // We need to invoke GC without stack, otherwise some objects may not be
      // reclaimed because of conservative stack scanning.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
    }

    // shared SFI is marked old but BytecodeArray is kept alive by copy.
    CHECK(shared->is_compiled());
    CHECK(copy->is_compiled());
    CHECK(function->is_compiled(i_isolate));

    // The feedback metadata for both SharedFunctionInfo instances should have
    // been reset.
    CHECK(shared->HasFeedbackMetadata());
    CHECK(copy->HasFeedbackMetadata());
  }
}

TEST(TestMultiReferencedBytecodeFlushing) {
  TestMultiReferencedBytecodeFlushing(/*sparkplug_compile=*/false);
}

TEST(TestMultiReferencedBytecodeFlushingWithSparkplug) {
  TestMultiReferencedBytecodeFlushing(/*sparkplug_compile=*/true);
}

HEAP_TEST(Regress10560) {
  i::v8_flags.flush_bytecode = true;
  i::v8_flags.allow_natives_syntax = true;
  // Disable flags that allocate a feedback vector eagerly.
#if !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  i::v8_flags.turbofan = false;
  i::v8_flags.always_turbofan = false;
#endif  // !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
#ifdef V8_ENABLE_SPARKPLUG
  v8_flags.always_sparkplug = false;
#endif  // V8_ENABLE_SPARKPLUG
  i::v8_flags.lazy_feedback_allocation = true;

  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  Isolate* i_isolate = CcTest::i_isolate();
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();

  {
    v8::HandleScope scope(isolate);
    const char* source =
        "function foo() {"
        "  var x = 42;"
        "  var y = 42;"
        "  var z = x + y;"
        "};"
        "foo()";
    Handle<String> foo_name = factory->InternalizeUtf8String("foo");
    CompileRun(source);

    // Check function is compiled.
    Handle<Object> func_value =
        Object::GetProperty(i_isolate, i_isolate->global_object(), foo_name)
            .ToHandleChecked();
    CHECK(IsJSFunction(*func_value));
    DirectHandle<JSFunction> function = Cast<JSFunction>(func_value);
    CHECK(function->shared()->is_compiled());
    CHECK(!function->has_feedback_vector());

    // Pre-age bytecode so it will be flushed on next run.
    CHECK(function->shared()->HasBytecodeArray());
    SharedFunctionInfo::EnsureOldForTesting(function->shared());

    heap::SimulateFullSpace(heap->old_space());

    // Just check bytecode isn't flushed still
    CHECK(function->shared()->is_compiled());

    heap->set_force_gc_on_next_allocation();

    // Allocate feedback vector.
    IsCompiledScope is_compiled_scope(
        function->shared()->is_compiled_scope(i_isolate));
    JSFunction::EnsureFeedbackVector(i_isolate, function, &is_compiled_scope);

    CHECK(function->has_feedback_vector());
    CHECK(function->shared()->is_compiled());
    CHECK(function->is_compiled(i_isolate));
  }
}

UNINITIALIZED_TEST(Regress10843) {
  v8_flags.max_semi_space_size = 2;
  v8_flags.min_semi_space_size = 2;
  v8_flags.max_old_space_size = 8;
  v8_flags.compact_on_every_full_gc = true;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();
  bool callback_was_invoked = false;

  heap->AddNearHeapLimitCallback(
      [](void* data, size_t current_heap_limit,
         size_t initial_heap_limit) -> size_t {
        *reinterpret_cast<bool*>(data) = true;
        return current_heap_limit * 2;
      },
      &callback_was_invoked);

  {
    PtrComprCageAccessScope ptr_compr_cage_access_scope(i_isolate);
    HandleScope scope(i_isolate);
    std::vector<Handle<FixedArray>> arrays;
    for (int i = 0; i < 140; i++) {
      arrays.push_back(factory->NewFixedArray(10000));
    }
    heap::InvokeMajorGC(heap);
    heap::InvokeMajorGC(heap);
    for (int i = 0; i < 40; i++) {
      arrays.push_back(factory->NewFixedArray(10000));
    }
    heap::InvokeMajorGC(heap);
    for (int i = 0; i < 100; i++) {
      arrays.push_back(factory->NewFixedArray(10000));
    }
    heap::InvokeMajorGC(heap);
    CHECK(callback_was_invoked);
  }
  isolate->Dispose();
}

size_t near_heap_limit_invocation_count = 0;
size_t InvokeGCNearHeapLimitCallback(void* data, size_t current_heap_limit,
                                     size_t initial_heap_limit) {
  near_heap_limit_invocation_count++;
  if (near_heap_limit_invocation_count > 1) {
    // We are already in a GC triggered in this callback, raise the limit
    // to avoid an OOM.
    return current_heap_limit * 5;
  }

  DCHECK_EQ(near_heap_limit_invocation_count, 1);
  // Operations that may cause GC (e.g. taking heap snapshots) in the
  // near heap limit callback should not hit the AllowGarbageCollection
  // assertion.
  static_cast<v8::Isolate*>(data)->GetHeapProfiler()->TakeHeapSnapshot();
  return current_heap_limit * 5;
}

UNINITIALIZED_TEST(Regress12777) {
  v8::Isolate::CreateParams create_params;
  create_params.constraints.set_max_old_generation_size_in_bytes(10 * i::MB);
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);

  isolate->AddNearHeapLimitCallback(InvokeGCNearHeapLimitCallback, isolate);

  {
    v8::Isolate::Scope isolate_scope(isolate);

    Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
    // Allocate data to trigger the NearHeapLimitCallback.
    HandleScope scope(i_isolate);
    int length = 2 * i::MB / i::kTaggedSize;
    std::vector<Handle<FixedArray>> arrays;
    for (int i = 0; i < 5; i++) {
      arrays.push_back(i_isolate->factory()->NewFixedArray(length));
    }
    heap::InvokeMajorGC(i_isolate->heap());
    for (int i = 0; i < 5; i++) {
      arrays.push_back(i_isolate->factory()->NewFixedArray(length));
    }
    heap::InvokeMajorGC(i_isolate->heap());
    for (int i = 0; i < 5; i++) {
      arrays.push_back(i_isolate->factory()->NewFixedArray(length));
    }

    // Normally, taking a heap snapshot in the near heap limit would result in
    // a full GC, then the overhead of the promotions would cause another
    // invocation of the heap limit callback and it can raise the limit in
    // the second call to avoid an OOM, so we test that the callback can
    // indeed raise the limit this way in this case. When there is only one
    // generation, however, there would not be the overhead of promotions so the
    // callback may not be triggered again during the generation of the heap
    // snapshot. In that case we only need to check that the callback is called
    // and it can perform GC-triggering operations jsut fine there.
    size_t minimum_callback_invocation_count =
        v8_flags.single_generation ? 1 : 2;
    CHECK_GE(near_heap_limit_invocation_count,
             minimum_callback_invocation_count);
  }

  isolate->GetHeapProfiler()->DeleteAllHeapSnapshots();
  isolate->Dispose();
}

#if !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
TEST(TestOptimizeAfterBytecodeFlushingCandidate) {
  if (v8_flags.single_generation) return;
  v8_flags.turbofan = true;
  v8_flags.always_turbofan = false;
#ifdef V8_ENABLE_SPARKPLUG
  v8_flags.always_sparkplug = false;
#endif  // V8_ENABLE_SPARKPLUG
  i::v8_flags.optimize_for_size = false;
  i::v8_flags.incremental_marking = true;
  i::v8_flags.flush_bytecode = true;
  i::v8_flags.allow_natives_syntax = true;
  ManualGCScope manual_gc_scope;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  Factory* factory = isolate->factory();

  v8::HandleScope outer_scope(CcTest::isolate());
  const char* source =
      "function foo() {"
      "  var x = 42;"
      "  var y = 42;"
      "  var z = x + y;"
      "};"
      "foo()";
  IndirectHandle<String> foo_name = factory->InternalizeUtf8String("foo");

  // This compile will add the code to the compilation cache.
  {
    v8::HandleScope scope(CcTest::isolate());
    CompileRun(source);
  }

  // Check function is compiled.
  IndirectHandle<Object> func_value =
      Object::GetProperty(isolate, isolate->global_object(), foo_name)
          .ToHandleChecked();
  CHECK(IsJSFunction(*func_value));
  IndirectHandle<JSFunction> function = Cast<JSFunction>(func_value);
  CHECK(function->shared()->is_compiled());

  // The code will survive at least two GCs.
  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(function->shared()->is_compiled());

  i::SharedFunctionInfo::EnsureOldForTesting(function->shared());
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  CHECK(!function->shared()->is_compiled());
  CHECK(!function->is_compiled(isolate));

  // This compile will compile the function again.
  {
    v8::HandleScope scope(CcTest::isolate());
    CompileRun("foo();");
  }

  SharedFunctionInfo::EnsureOldForTesting(function->shared());
  heap::SimulateIncrementalMarking(CcTest::heap());

  // Force optimization while incremental marking is active and while
  // the function is enqueued as a candidate.
  {
    v8::HandleScope scope(CcTest::isolate());
    CompileRun(
        "%PrepareFunctionForOptimization(foo);"
        "%OptimizeFunctionOnNextCall(foo); foo();");
  }

  // Simulate one final GC and make sure the candidate wasn't flushed.
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(function->shared()->is_compiled());
  CHECK(function->is_compiled(isolate));
}
#endif  // !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)

TEST(TestUseOfIncrementalBarrierOnCompileLazy) {
  if (!v8_flags.incremental_marking) return;
  // Turn off always_turbofan because it interferes with running the built-in
  // for the last call to g().
  v8_flags.always_turbofan = false;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  v8::HandleScope scope(CcTest::isolate());

  CompileRun(
      "function make_closure(x) {"
      "  return function() { return x + 3 };"
      "}"
      "var f = make_closure(5);"
      "%PrepareFunctionForOptimization(f); f();"
      "var g = make_closure(5);");

  // Check f is compiled.
  Handle<String> f_name = factory->InternalizeUtf8String("f");
  Handle<Object> f_value =
      Object::GetProperty(isolate, isolate->global_object(), f_name)
          .ToHandleChecked();
  DirectHandle<JSFunction> f_function = Cast<JSFunction>(f_value);
  CHECK(f_function->is_compiled(isolate));

  // Check g is not compiled.
  Handle<String> g_name = factory->InternalizeUtf8String("g");
  Handle<Object> g_value =
      Object::GetProperty(isolate, isolate->global_object(), g_name)
          .ToHandleChecked();
  DirectHandle<JSFunction> g_function = Cast<JSFunction>(g_value);
  CHECK(!g_function->is_compiled(isolate));

  heap::SimulateIncrementalMarking(heap);
  CompileRun("%OptimizeFunctionOnNextCall(f); f();");

  // g should now have available an optimized function, unmarked by gc. The
  // CompileLazy built-in will discover it and install it in the closure, and
  // the incremental write barrier should be used.
  CompileRun("g();");
  CHECK(g_function->is_compiled(isolate));
}

void CompilationCacheCachingBehavior(bool retain_script) {
  // If we do not have the compilation cache turned off, this test is invalid.
  if (!v8_flags.compilation_cache) {
    return;
  }
  if (!v8_flags.flush_bytecode ||
      (v8_flags.always_sparkplug && !v8_flags.flush_baseline_code)) {
    return;
  }
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  Factory* factory = isolate->factory();
  CompilationCache* compilation_cache = isolate->compilation_cache();
  LanguageMode language_mode = LanguageMode::kSloppy;

  v8::HandleScope outer_scope(CcTest::isolate());
  const char* raw_source = retain_script ? "function foo() {"
                                           "  var x = 42;"
                                           "  var y = 42;"
                                           "  var z = x + y;"
                                           "};"
                                           "foo();"
                                         : "(function foo() {"
                                           "  var x = 42;"
                                           "  var y = 42;"
                                           "  var z = x + y;"
                                           "})();";
  IndirectHandle<String> source = factory->InternalizeUtf8String(raw_source);

  {
    v8::HandleScope scope(CcTest::isolate());
    CompileRun(raw_source);
  }

  // The script should be in the cache now.
  {
    v8::HandleScope scope(CcTest::isolate());
    ScriptDetails script_details(Handle<Object>(),
                                 v8::ScriptOriginOptions(true, false));
    auto lookup_result =
        compilation_cache->LookupScript(source, script_details, language_mode);
    CHECK(!lookup_result.toplevel_sfi().is_null());
  }

  // Check that the code cache entry survives at least one GC.
  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  {
    v8::HandleScope scope(CcTest::isolate());
    ScriptDetails script_details(Handle<Object>(),
                                 v8::ScriptOriginOptions(true, false));
    auto lookup_result =
        compilation_cache->LookupScript(source, script_details, language_mode);
    CHECK(!lookup_result.toplevel_sfi().is_null());

    // Progress code age until it's old and ready for GC.
    DirectHandle<SharedFunctionInfo> shared =
        lookup_result.toplevel_sfi().ToHandleChecked();
    CHECK(shared->HasBytecodeArray());
    SharedFunctionInfo::EnsureOldForTesting(*shared);
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    // The first GC flushes the BytecodeArray from the SFI.
    heap::InvokeMajorGC(heap);
    // The second GC removes the SFI from the compilation cache.
    heap::InvokeMajorGC(heap);
  }

  {
    v8::HandleScope scope(CcTest::isolate());
    // Ensure code aging cleared the entry from the cache.
    ScriptDetails script_details(Handle<Object>(),
                                 v8::ScriptOriginOptions(true, false));
    auto lookup_result =
        compilation_cache->LookupScript(source, script_details, language_mode);
    CHECK(lookup_result.toplevel_sfi().is_null());
    CHECK_EQ(retain_script, !lookup_result.script().is_null());
  }
}

TEST(CompilationCacheCachingBehaviorDiscardScript) {
  CompilationCacheCachingBehavior(false);
}

TEST(CompilationCacheCachingBehaviorRetainScript) {
  CompilationCacheCachingBehavior(true);
}

namespace {

template <typename T>
Handle<SharedFunctionInfo> GetSharedFunctionInfo(
    v8::Local<T> function_or_script) {
  DirectHandle<JSFunction> i_function =
      Cast<JSFunction>(v8::Utils::OpenDirectHandle(*function_or_script));
  return handle(i_function->shared(), CcTest::i_isolate());
}

template <typename T>
void AgeBytecode(v8::Local<T> function_or_script) {
  DirectHandle<SharedFunctionInfo> shared =
      GetSharedFunctionInfo(function_or_script);
  CHECK(shared->HasBytecodeArray());
  SharedFunctionInfo::EnsureOldForTesting(*shared);
}

void CompilationCacheRegeneration(bool retain_root_sfi, bool flush_root_sfi,
                                  bool flush_eager_sfi) {
  // If the compilation cache is turned off, this test is invalid.
  if (!v8_flags.compilation_cache) {
    return;
  }

  // Skip test if code flushing was disabled.
  if (!v8_flags.flush_bytecode ||
      (v8_flags.always_sparkplug && !v8_flags.flush_baseline_code)) {
    return;
  }

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();

  const char* source =
      "({"
      "  lazyFunction: function () {"
      "    var x = 42;"
      "    var y = 42;"
      "    var z = x + y;"
      "  },"
      "  eagerFunction: (function () {"
      "    var x = 43;"
      "    var y = 43;"
      "    var z = x + y;"
      "  })"
      "})";

  v8::Global<v8::Script> outer_function;
  v8::Global<v8::Function> lazy_function;
  v8::Global<v8::Function> eager_function;

  {
    v8::HandleScope scope(CcTest::isolate());
    v8::Local<v8::Context> context =
        v8::Isolate::GetCurrent()->GetCurrentContext();
    v8::Local<v8::Script> script = v8_compile(v8_str(source));
    outer_function.Reset(CcTest::isolate(), script);

    // Even though the script has not executed, it should already be parsed.
    DirectHandle<SharedFunctionInfo> script_sfi = GetSharedFunctionInfo(script);
    CHECK(script_sfi->is_compiled());

    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

    // Now that the script has run, we can get references to the inner
    // functions, and verify that the eager parsing heuristics are behaving as
    // expected.
    v8::Local<v8::Object> result_obj =
        result->ToObject(context).ToLocalChecked();
    v8::Local<v8::Value> lazy_function_value =
        result_obj->GetRealNamedProperty(context, v8_str("lazyFunction"))
            .ToLocalChecked();
    CHECK(lazy_function_value->IsFunction());
    CHECK(!GetSharedFunctionInfo(lazy_function_value)->is_compiled());
    lazy_function.Reset(CcTest::isolate(),
                        lazy_function_value.As<v8::Function>());
    v8::Local<v8::Value> eager_function_value =
        result_obj->GetRealNamedProperty(context, v8_str("eagerFunction"))
            .ToLocalChecked();
    CHECK(eager_function_value->IsFunction());
    eager_function.Reset(CcTest::isolate(),
                         eager_function_value.As<v8::Function>());
    CHECK(GetSharedFunctionInfo(eager_function_value)->is_compiled());
  }

  {
    v8::HandleScope scope(CcTest::isolate());

    // Progress code age until it's old and ready for GC.
    if (flush_root_sfi) {
      v8::Local<v8::Script> outer_function_value =
          outer_function.Get(CcTest::isolate());
      AgeBytecode(outer_function_value);
    }
    if (flush_eager_sfi) {
      v8::Local<v8::Function> eager_function_value =
          eager_function.Get(CcTest::isolate());
      AgeBytecode(eager_function_value);
    }
    if (!retain_root_sfi) {
      outer_function.Reset();
    }
  }

  {
    // In these tests, we need to invoke GC without stack, otherwise some
    // objects may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);

    if (v8_flags.stress_incremental_marking) {
      // This GC finishes incremental marking if it is already running. If
      // incremental marking was already running we would not flush the code
      // right away.
      heap::InvokeMajorGC(heap);
    }

    // The first GC performs code flushing.
    heap::InvokeMajorGC(heap);
    // The second GC clears the entry from the compilation cache.
    heap::InvokeMajorGC(heap);
  }

  // The root SharedFunctionInfo can be retained either by a Global in this
  // function or by the compilation cache.
  bool root_sfi_should_still_exist = retain_root_sfi || !flush_root_sfi;

  {
    v8::HandleScope scope(CcTest::isolate());

    // The lazy function should still not be compiled.
    DirectHandle<SharedFunctionInfo> lazy_sfi =
        GetSharedFunctionInfo(lazy_function.Get(CcTest::isolate()));
    CHECK(!lazy_sfi->is_compiled());

    // The eager function may have had its bytecode flushed.
    DirectHandle<SharedFunctionInfo> eager_sfi =
        GetSharedFunctionInfo(eager_function.Get(CcTest::isolate()));
    CHECK_EQ(!flush_eager_sfi, eager_sfi->is_compiled());

    // Check whether the root SharedFunctionInfo is still reachable from the
    // Script.
    DirectHandle<Script> script(Cast<Script>(lazy_sfi->script()), isolate);
    bool root_sfi_still_exists = false;
    Tagged<MaybeObject> maybe_root_sfi =
        script->infos()->get(kFunctionLiteralIdTopLevel);
    if (Tagged<HeapObject> sfi_or_undefined;
        maybe_root_sfi.GetHeapObject(&sfi_or_undefined)) {
      root_sfi_still_exists = !IsUndefined(sfi_or_undefined);
    }
    CHECK_EQ(root_sfi_should_still_exist, root_sfi_still_exists);
  }

  {
    // Run the script again and check that no SharedFunctionInfos were
    // duplicated, and that the expected ones were compiled.
    v8::HandleScope scope(CcTest::isolate());
    v8::Local<v8::Context> context =
        v8::Isolate::GetCurrent()->GetCurrentContext();
    v8::Local<v8::Script> script = v8_compile(v8_str(source));

    // The script should be compiled by now.
    DirectHandle<SharedFunctionInfo> script_sfi = GetSharedFunctionInfo(script);
    CHECK(script_sfi->is_compiled());

    // This compilation should not have created a new root SharedFunctionInfo if
    // one already existed.
    if (retain_root_sfi) {
      DirectHandle<SharedFunctionInfo> old_script_sfi =
          GetSharedFunctionInfo(outer_function.Get(CcTest::isolate()));
      CHECK_EQ(*old_script_sfi, *script_sfi);
    }

    DirectHandle<SharedFunctionInfo> old_lazy_sfi =
        GetSharedFunctionInfo(lazy_function.Get(CcTest::isolate()));
    CHECK(!old_lazy_sfi->is_compiled());

    // The only way for the eager function to be uncompiled at this point is if
    // it was flushed but the root function was not.
    DirectHandle<SharedFunctionInfo> old_eager_sfi =
        GetSharedFunctionInfo(eager_function.Get(CcTest::isolate()));
    CHECK_EQ(!(flush_eager_sfi && !flush_root_sfi),
             old_eager_sfi->is_compiled());

    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

    // Check that both functions reused the existing SharedFunctionInfos.
    v8::Local<v8::Object> result_obj =
        result->ToObject(context).ToLocalChecked();
    v8::Local<v8::Value> lazy_function_value =
        result_obj->GetRealNamedProperty(context, v8_str("lazyFunction"))
            .ToLocalChecked();
    CHECK(lazy_function_value->IsFunction());
    DirectHandle<SharedFunctionInfo> lazy_sfi =
        GetSharedFunctionInfo(lazy_function_value);
    CHECK_EQ(*old_lazy_sfi, *lazy_sfi);
    v8::Local<v8::Value> eager_function_value =
        result_obj->GetRealNamedProperty(context, v8_str("eagerFunction"))
            .ToLocalChecked();
    CHECK(eager_function_value->IsFunction());
    DirectHandle<SharedFunctionInfo> eager_sfi =
        GetSharedFunctionInfo(eager_function_value);
    CHECK_EQ(*old_eager_sfi, *eager_sfi);
  }
}

}  // namespace

TEST(CompilationCacheRegeneration0) {
  CompilationCacheRegeneration(false, false, false);
}
TEST(CompilationCacheRegeneration1) {
  CompilationCacheRegeneration(false, false, true);
}
TEST(CompilationCacheRegeneration2) {
  CompilationCacheRegeneration(false, true, false);
}
TEST(CompilationCacheRegeneration3) {
  CompilationCacheRegeneration(false, true, true);
}
TEST(CompilationCacheRegeneration4) {
  CompilationCacheRegeneration(true, false, false);
}
TEST(CompilationCacheRegeneration5) {
  CompilationCacheRegeneration(true, false, true);
}
TEST(CompilationCacheRegeneration6) {
  CompilationCacheRegeneration(true, true, false);
}
TEST(CompilationCacheRegeneration7) {
  CompilationCacheRegeneration(true, true, true);
}

static void OptimizeEmptyFunction(const char* name) {
  HandleScope scope(CcTest::i_isolate());
  base::EmbeddedVector<char, 256> source;
  base::SNPrintF(source,
                 "function %s() { return 0; }"
                 "%%PrepareFunctionForOptimization(%s);"
                 "%s(); %s();"
                 "%%OptimizeFunctionOnNextCall(%s);"
                 "%s();",
                 name, name, name, name, name, name);
  CompileRun(source.begin());
}

// Count the number of native contexts in the weak list of native contexts.
int CountNativeContexts() {
  int count = 0;
  Tagged<Object> object = CcTest::heap()->native_contexts_list();
  while (!IsUndefined(object, CcTest::i_isolate())) {
    count++;
    object = Cast<Context>(object)->next_context_link();
  }
  return count;
}

TEST(TestInternalWeakLists) {
  v8_flags.always_turbofan = false;
  v8_flags.allow_natives_syntax = true;

  // Some flags turn Scavenge collections into Mark-sweep collections
  // and hence are incompatible with this test case.
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.separate_gc_phases || v8_flags.stress_concurrent_allocation)
    return;
  v8_flags.retain_maps_for_n_gc = 0;

  static const int kNumTestContexts = 10;

  ManualGCScope manual_gc_scope;
  v8::Isolate* v8_isolate = CcTest::isolate();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  HandleScope scope(isolate);
  v8::Global<v8::Context> ctx[kNumTestContexts];
  if (!isolate->use_optimizer()) return;

  CHECK_EQ(0, CountNativeContexts());

  // Create a number of global contests which gets linked together.
  for (int i = 0; i < kNumTestContexts; i++) {
    // Create a handle scope so no contexts or function objects get stuck in the
    // outer handle scope.
    HandleScope new_scope(isolate);

    ctx[i].Reset(v8_isolate, v8::Context::New(v8_isolate));

    // Collect garbage that might have been created by one of the
    // installed extensions.
    isolate->compilation_cache()->Clear();
    {
      // In this test, we need to invoke GC without stack, otherwise some
      // objects may not be reclaimed because of conservative stack scanning.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
    }

    CHECK_EQ(i + 1, CountNativeContexts());

    ctx[i].Get(v8_isolate)->Enter();

    OptimizeEmptyFunction("f1");
    OptimizeEmptyFunction("f2");
    OptimizeEmptyFunction("f3");
    OptimizeEmptyFunction("f4");
    OptimizeEmptyFunction("f5");

    // Remove function f1, and
    CompileRun("f1=null");

    // Scavenge treats these references as strong.
    for (int j = 0; j < 10; j++) {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMinorGC(heap);
    }

    // Mark compact handles the weak references.
    isolate->compilation_cache()->Clear();
    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
    }

    // Get rid of f3 and f5 in the same way.
    CompileRun("f3=null");
    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      for (int j = 0; j < 10; j++) {
        heap::InvokeMinorGC(heap);
      }
      heap::InvokeMajorGC(heap);
    }
    CompileRun("f5=null");
    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      for (int j = 0; j < 10; j++) {
        heap::InvokeMinorGC(heap);
      }
      heap::InvokeMajorGC(heap);
    }
    ctx[i].Get(v8_isolate)->Exit();
  }

  // Force compilation cache cleanup.
  heap->NotifyContextDisposed(true);
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  // Dispose the native contexts one by one.
  for (int i = 0; i < kNumTestContexts; i++) {
    ctx[i].Reset();

    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);

    // Scavenge treats these references as strong.
    for (int j = 0; j < 10; j++) {
      heap::InvokeMinorGC(heap);
      CHECK_EQ(kNumTestContexts - i, CountNativeContexts());
    }
    // Mark-compact handles the weak references.
    heap::InvokeMajorGC(heap);

    CHECK_EQ(kNumTestContexts - i - 1, CountNativeContexts());
  }

  CHECK_EQ(0, CountNativeContexts());
}

TEST(TestSizeOfRegExpCode) {
  if (!v8_flags.regexp_optimization) return;
  v8_flags.stress_concurrent_allocation = false;

  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  HandleScope scope(isolate);

  LocalContext context;

  // Adjust source below and this check to match
  // RegExp::kRegExpTooLargeToOptimize.
  CHECK_EQ(i::RegExp::kRegExpTooLargeToOptimize, 20 * KB);

  // Compile a regexp that is much larger if we are using regexp optimizations.
  CompileRun(
      "var reg_exp_source = '(?:a|bc|def|ghij|klmno|pqrstu)';"
      "var half_size_reg_exp;"
      "while (reg_exp_source.length < 20 * 1024) {"
      "  half_size_reg_exp = reg_exp_source;"
      "  reg_exp_source = reg_exp_source + reg_exp_source;"
      "}"
      // Flatten string.
      "reg_exp_source.match(/f/);");

  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    // Get initial heap size after several full GCs, which will stabilize
    // the heap size and return with sweeping finished completely.
    heap::InvokeMemoryReducingMajorGCs(heap);
    if (heap->sweeping_in_progress()) {
      heap->EnsureSweepingCompleted(
          Heap::SweepingForcedFinalizationMode::kV8Only);
    }
  }
  int initial_size = static_cast<int>(heap->SizeOfObjects());

  CompileRun("'foo'.match(reg_exp_source);");
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  int size_with_regexp = static_cast<int>(heap->SizeOfObjects());

  CompileRun("'foo'.match(half_size_reg_exp);");
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  int size_with_optimized_regexp = static_cast<int>(heap->SizeOfObjects());

  int size_of_regexp_code = size_with_regexp - initial_size;

  // On some platforms the debug-code flag causes huge amounts of regexp code
  // to be emitted, breaking this test.
  if (!v8_flags.debug_code) {
    CHECK_LE(size_of_regexp_code, 1 * MB);
  }

  // Small regexp is half the size, but compiles to more than twice the code
  // due to the optimization steps.
  CHECK_GE(size_with_optimized_regexp,
           size_with_regexp + size_of_regexp_code * 2);
}

HEAP_TEST(TestSizeOfObjects) {
  v8_flags.stress_concurrent_allocation = false;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();

  // Disable LAB, such that calculations with SizeOfObjects() and object size
  // are correct.
  heap->DisableInlineAllocation();

  // Get initial heap size after several full GCs, which will stabilize
  // the heap size and return with sweeping finished completely.
  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
    if (heap->sweeping_in_progress()) {
      heap->EnsureSweepingCompleted(
          Heap::SweepingForcedFinalizationMode::kV8Only);
    }
  }
  int initial_size = static_cast<int>(heap->SizeOfObjects());

  {
    HandleScope scope(isolate);
    // Allocate objects on several different old-space pages so that
    // concurrent sweeper threads will be busy sweeping the old space on
    // subsequent GC runs.
    AlwaysAllocateScopeForTesting always_allocate(heap);
    int filler_size = static_cast<int>(FixedArray::SizeFor(8192));
    for (int i = 1; i <= 100; i++) {
      isolate->factory()->NewFixedArray(8192, AllocationType::kOld);
      CHECK_EQ(initial_size + i * filler_size,
               static_cast<int>(heap->SizeOfObjects()));
    }
  }

  // The heap size should go back to initial size after a full GC, even
  // though sweeping didn't finish yet.
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  // Normally sweeping would not be complete here, but no guarantees.
  CHECK_EQ(initial_size, static_cast<int>(heap->SizeOfObjects()));
  // Waiting for sweeper threads should not change heap size.
  if (heap->sweeping_in_progress()) {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }
  CHECK_EQ(initial_size, static_cast<int>(heap->SizeOfObjects()));
}

TEST(TestAlignmentCalculations) {
  // Maximum fill amounts are consistent.
  int maximum_double_misalignment = kDoubleSize - kTaggedSize;
  int max_word_fill = Heap::GetMaximumFillToAlign(kTaggedAligned);
  CHECK_EQ(0, max_word_fill);
  int max_double_fill = Heap::GetMaximumFillToAlign(kDoubleAligned);
  CHECK_EQ(maximum_double_misalignment, max_double_fill);
  int max_double_unaligned_fill = Heap::GetMaximumFillToAlign(kDoubleUnaligned);
  CHECK_EQ(maximum_double_misalignment, max_double_unaligned_fill);

  Address base = kNullAddress;
  int fill = 0;

  // Word alignment never requires fill.
  fill = Heap::GetFillToAlign(base, kTaggedAligned);
  CHECK_EQ(0, fill);
  fill = Heap::GetFillToAlign(base + kTaggedSize, kTaggedAligned);
  CHECK_EQ(0, fill);

  // No fill is required when address is double aligned.
  fill = Heap::GetFillToAlign(base, kDoubleAligned);
  CHECK_EQ(0, fill);
  // Fill is required if address is not double aligned.
  fill = Heap::GetFillToAlign(base + kTaggedSize, kDoubleAligned);
  CHECK_EQ(maximum_double_misalignment, fill);
  // kDoubleUnaligned has the opposite fill amounts.
  fill = Heap::GetFillToAlign(base, kDoubleUnaligned);
  CHECK_EQ(maximum_double_misalignment, fill);
  fill = Heap::GetFillToAlign(base + kTaggedSize, kDoubleUnaligned);
  CHECK_EQ(0, fill);
}

static Tagged<HeapObject> AllocateAligned(MainAllocator* allocator, int size,
                                          AllocationAlignment alignment) {
  Heap* heap = CcTest::heap();
  AllocationResult allocation = allocator->AllocateRawForceAlignmentForTesting(
      size, alignment, AllocationOrigin::kRuntime);
  Tagged<HeapObject> obj;
  allocation.To(&obj);
  heap->CreateFillerObjectAt(obj.address(), size);
  return obj;
}

TEST(TestAlignedAllocation) {
  if (v8_flags.single_generation) return;
  // Double misalignment is 4 on 32-bit platforms or when pointer compression
  // is enabled, 0 on 64-bit ones when pointer compression is disabled.
  const intptr_t double_misalignment = kDoubleSize - kTaggedSize;
  Address start;
  Tagged<HeapObject> obj;
  Tagged<HeapObject> filler;
  if (double_misalignment) {
    MainAllocator* allocator =
        CcTest::heap()->allocator()->new_space_allocator();

    // Make one allocation to force allocating an allocation area. Using
    // kDoubleSize to not change space alignment
    USE(allocator->AllocateRaw(kDoubleSize, kDoubleAligned,
                               AllocationOrigin::kRuntime));

    // Allocate a pointer sized object that must be double aligned at an
    // aligned address.
    start = allocator->AlignTopForTesting(kDoubleAligned, 0);
    obj = AllocateAligned(allocator, kTaggedSize, kDoubleAligned);
    CHECK(IsAligned(obj.address(), kDoubleAlignment));
    // There is no filler.
    CHECK_EQ(start, obj.address());

    // Allocate a second pointer sized object that must be double aligned at an
    // unaligned address.
    start = allocator->AlignTopForTesting(kDoubleAligned, kTaggedSize);
    obj = AllocateAligned(allocator, kTaggedSize, kDoubleAligned);
    CHECK(IsAligned(obj.address(), kDoubleAlignment));
    // There is a filler object before the object.
    filler = HeapObject::FromAddress(start);
    CHECK(obj != filler && IsFreeSpaceOrFiller(filler) &&
          filler->Size() == kTaggedSize);
    CHECK_EQ(start + double_misalignment, obj->address());

    // Similarly for kDoubleUnaligned.
    start = allocator->AlignTopForTesting(kDoubleUnaligned, 0);
    obj = AllocateAligned(allocator, kTaggedSize, kDoubleUnaligned);
    CHECK(IsAligned(obj.address() + kTaggedSize, kDoubleAlignment));
    CHECK_EQ(start, obj->address());

    start = allocator->AlignTopForTesting(kDoubleUnaligned, kTaggedSize);
    obj = AllocateAligned(allocator, kTaggedSize, kDoubleUnaligned);
    CHECK(IsAligned(obj.address() + kTaggedSize, kDoubleAlignment));
    // There is a filler object before the object.
    filler = HeapObject::FromAddress(start);
    CHECK(obj != filler && IsFreeSpaceOrFiller(filler) &&
          filler->Size() == kTaggedSize);
    CHECK_EQ(start + kTaggedSize, obj->address());
  }
}

static Tagged<HeapObject> OldSpaceAllocateAligned(
    int size, AllocationAlignment alignment) {
  Heap* heap = CcTest::heap();
  AllocationResult allocation =
      heap->allocator()
          ->old_space_allocator()
          ->AllocateRawForceAlignmentForTesting(size, alignment,
                                                AllocationOrigin::kRuntime);
  Tagged<HeapObject> obj;
  allocation.To(&obj);
  heap->CreateFillerObjectAt(obj.address(), size);
  return obj;
}

// Get old space allocation into the desired alignment.
static Address AlignOldSpace(AllocationAlignment alignment, int offset) {
  Address* top_addr = CcTest::heap()->OldSpaceAllocationTopAddress();
  int fill = Heap::GetFillToAlign(*top_addr, alignment);
  int allocation = fill + offset;
  if (allocation) {
    OldSpaceAllocateAligned(allocation, kTaggedAligned);
  }
  Address top = *top_addr;
  // Now force the remaining allocation onto the free list.
  CcTest::heap()->FreeMainThreadLinearAllocationAreas();
  return top;
}

// Test the case where allocation must be done from the free list, so filler
// may precede or follow the object.
TEST(TestAlignedOverAllocation) {
  if (v8_flags.stress_concurrent_allocation) return;
  ManualGCScope manual_gc_scope;
  Heap* heap = CcTest::heap();
  // Test checks for fillers before and behind objects and requires a fresh
  // page and empty free list.
  heap::AbandonCurrentlyFreeMemory(heap->old_space());
  // Allocate a dummy object to properly set up the linear allocation info.
  AllocationResult dummy =
      heap->allocator()->old_space_allocator()->AllocateRaw(
          kTaggedSize, kTaggedAligned, AllocationOrigin::kRuntime);
  CHECK(!dummy.IsFailure());
  heap->CreateFillerObjectAt(dummy.ToObjectChecked().address(), kTaggedSize);

  // Double misalignment is 4 on 32-bit platforms or when pointer compression
  // is enabled, 0 on 64-bit ones when pointer compression is disabled.
  const intptr_t double_misalignment = kDoubleSize - kTaggedSize;
  Address start;
  Tagged<HeapObject> obj;
  Tagged<HeapObject> filler;
  if (double_misalignment) {
    start = AlignOldSpace(kDoubleAligned, 0);
    obj = OldSpaceAllocateAligned(kTaggedSize, kDoubleAligned);
    // The object is aligned.
    CHECK(IsAligned(obj.address(), kDoubleAlignment));
    // Try the opposite alignment case.
    start = AlignOldSpace(kDoubleAligned, kTaggedSize);
    obj = OldSpaceAllocateAligned(kTaggedSize, kDoubleAligned);
    CHECK(IsAligned(obj.address(), kDoubleAlignment));
    filler = HeapObject::FromAddress(start);
    CHECK(obj != filler);
    CHECK(IsFreeSpaceOrFiller(filler));
    CHECK_EQ(kTaggedSize, filler->Size());
    CHECK(obj != filler && IsFreeSpaceOrFiller(filler) &&
          filler->Size() == kTaggedSize);

    // Similarly for kDoubleUnaligned.
    start = AlignOldSpace(kDoubleUnaligned, 0);
    obj = OldSpaceAllocateAligned(kTaggedSize, kDoubleUnaligned);
    // The object is aligned.
    CHECK(IsAligned(obj.address() + kTaggedSize, kDoubleAlignment));
    // Try the opposite alignment case.
    start = AlignOldSpace(kDoubleUnaligned, kTaggedSize);
    obj = OldSpaceAllocateAligned(kTaggedSize, kDoubleUnaligned);
    CHECK(IsAligned(obj.address() + kTaggedSize, kDoubleAlignment));
    filler = HeapObject::FromAddress(start);
    CHECK(obj != filler && IsFreeSpaceOrFiller(filler) &&
          filler->Size() == kTaggedSize);
  }
}

TEST(HeapNumberAlignment) {
  if (!v8_flags.allocation_site_pretenuring) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  HandleScope sc(isolate);

  const auto required_alignment =
      HeapObject::RequiredAlignment(*factory->heap_number_map());
  const int maximum_misalignment =
      Heap::GetMaximumFillToAlign(required_alignment);

  for (int offset = 0; offset <= maximum_misalignment; offset += kTaggedSize) {
    if (!v8_flags.single_generation) {
      heap->allocator()->new_space_allocator()->AlignTopForTesting(
          required_alignment, offset);
      DirectHandle<Object> number_new = factory->NewNumber(1.000123);
      CHECK(IsHeapNumber(*number_new));
      CHECK(Heap::InYoungGeneration(*number_new));
      CHECK_EQ(0, Heap::GetFillToAlign(Cast<HeapObject>(*number_new).address(),
                                       required_alignment));
    }

    AlignOldSpace(required_alignment, offset);
    DirectHandle<Object> number_old =
        factory->NewNumber<AllocationType::kOld>(1.000321);
    CHECK(IsHeapNumber(*number_old));
    CHECK(heap->InOldSpace(*number_old));
    CHECK_EQ(0, Heap::GetFillToAlign(Cast<HeapObject>(*number_old).address(),
                                     required_alignment));
  }
}

TEST(TestSizeOfObjectsVsHeapObjectIteratorPrecision) {
  CcTest::InitializeVM();
  // Disable LAB, such that calculations with SizeOfObjects() and object size
  // are correct.
  CcTest::heap()->DisableInlineAllocation();
  HeapObjectIterator iterator(CcTest::heap());
  PtrComprCageBase cage_base(CcTest::i_isolate());
  intptr_t size_of_objects_1 = CcTest::heap()->SizeOfObjects();
  intptr_t size_of_objects_2 = 0;
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    if (!IsFreeSpace(obj, cage_base)) {
      size_of_objects_2 += obj->Size(cage_base);
    }
  }
  // Delta must be within 5% of the larger result.
  // TODO(gc): Tighten this up by distinguishing between byte
  // arrays that are real and those that merely mark free space
  // on the heap.
  if (size_of_objects_1 > size_of_objects_2) {
    intptr_t delta = size_of_objects_1 - size_of_objects_2;
    PrintF("Heap::SizeOfObjects: %" V8PRIdPTR
           ", "
           "Iterator: %" V8PRIdPTR
           ", "
           "delta: %" V8PRIdPTR "\n",
           size_of_objects_1, size_of_objects_2, delta);
    CHECK_GT(size_of_objects_1 / 20, delta);
  } else {
    intptr_t delta = size_of_objects_2 - size_of_objects_1;
    PrintF("Heap::SizeOfObjects: %" V8PRIdPTR
           ", "
           "Iterator: %" V8PRIdPTR
           ", "
           "delta: %" V8PRIdPTR "\n",
           size_of_objects_1, size_of_objects_2, delta);
    CHECK_GT(size_of_objects_2 / 20, delta);
  }
}

static int NumberOfGlobalObjects() {
  int count = 0;
  HeapObjectIterator iterator(CcTest::heap());
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    if (IsJSGlobalObject(obj)) count++;
  }
  return count;
}

// Test that we don't embed maps from foreign contexts into
// optimized code.
TEST(LeakNativeContextViaMap) {
  v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = CcTest::isolate();
  Heap* heap = CcTest::heap();
  v8::HandleScope outer_scope(isolate);
  v8::Persistent<v8::Context> ctx1p;
  v8::Persistent<v8::Context> ctx2p;
  {
    v8::HandleScope scope(isolate);
    ctx1p.Reset(isolate, v8::Context::New(isolate));
    ctx2p.Reset(isolate, v8::Context::New(isolate));
    v8::Local<v8::Context>::New(isolate, ctx1p)->Enter();
  }

  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(2, NumberOfGlobalObjects());

  {
    v8::HandleScope inner_scope(isolate);
    CompileRun("var v = {x: 42}");
    v8::Local<v8::Context> ctx1 = v8::Local<v8::Context>::New(isolate, ctx1p);
    v8::Local<v8::Context> ctx2 = v8::Local<v8::Context>::New(isolate, ctx2p);
    v8::Local<v8::Value> v =
        ctx1->Global()->Get(ctx1, v8_str("v")).ToLocalChecked();
    ctx2->Enter();
    CHECK(ctx2->Global()->Set(ctx2, v8_str("o"), v).FromJust());
    v8::Local<v8::Value> res = CompileRun(
        "function f() { return o.x; }"
        "%PrepareFunctionForOptimization(f);"
        "for (var i = 0; i < 10; ++i) f();"
        "%OptimizeFunctionOnNextCall(f);"
        "f();");
    CHECK_EQ(42, res->Int32Value(ctx2).FromJust());
    CHECK(ctx2->Global()
              ->Set(ctx2, v8_str("o"), v8::Int32::New(isolate, 0))
              .FromJust());
    ctx2->Exit();
    v8::Local<v8::Context>::New(isolate, ctx1)->Exit();
    ctx1p.Reset();
    isolate->ContextDisposedNotification();
  }
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(1, NumberOfGlobalObjects());
  ctx2p.Reset();
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(0, NumberOfGlobalObjects());
}

// Test that we don't embed functions from foreign contexts into
// optimized code.
TEST(LeakNativeContextViaFunction) {
  v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = CcTest::isolate();
  Heap* heap = CcTest::heap();
  v8::HandleScope outer_scope(isolate);
  v8::Persistent<v8::Context> ctx1p;
  v8::Persistent<v8::Context> ctx2p;
  {
    v8::HandleScope scope(isolate);
    ctx1p.Reset(isolate, v8::Context::New(isolate));
    ctx2p.Reset(isolate, v8::Context::New(isolate));
    v8::Local<v8::Context>::New(isolate, ctx1p)->Enter();
  }

  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(2, NumberOfGlobalObjects());

  {
    v8::HandleScope inner_scope(isolate);
    CompileRun("var v = function() { return 42; }");
    v8::Local<v8::Context> ctx1 = v8::Local<v8::Context>::New(isolate, ctx1p);
    v8::Local<v8::Context> ctx2 = v8::Local<v8::Context>::New(isolate, ctx2p);
    v8::Local<v8::Value> v =
        ctx1->Global()->Get(ctx1, v8_str("v")).ToLocalChecked();
    ctx2->Enter();
    CHECK(ctx2->Global()->Set(ctx2, v8_str("o"), v).FromJust());
    v8::Local<v8::Value> res = CompileRun(
        "function f(x) { return x(); }"
        "%PrepareFunctionForOptimization(f);"
        "for (var i = 0; i < 10; ++i) f(o);"
        "%OptimizeFunctionOnNextCall(f);"
        "f(o);");
    CHECK_EQ(42, res->Int32Value(ctx2).FromJust());
    CHECK(ctx2->Global()
              ->Set(ctx2, v8_str("o"), v8::Int32::New(isolate, 0))
              .FromJust());
    ctx2->Exit();
    ctx1->Exit();
    ctx1p.Reset();
    isolate->ContextDisposedNotification();
  }
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(1, NumberOfGlobalObjects());
  ctx2p.Reset();
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(0, NumberOfGlobalObjects());
}

TEST(LeakNativeContextViaMapKeyed) {
  v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = CcTest::isolate();
  Heap* heap = CcTest::heap();
  v8::HandleScope outer_scope(isolate);
  v8::Persistent<v8::Context> ctx1p;
  v8::Persistent<v8::Context> ctx2p;
  {
    v8::HandleScope scope(isolate);
    ctx1p.Reset(isolate, v8::Context::New(isolate));
    ctx2p.Reset(isolate, v8::Context::New(isolate));
    v8::Local<v8::Context>::New(isolate, ctx1p)->Enter();
  }

  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(2, NumberOfGlobalObjects());

  {
    v8::HandleScope inner_scope(isolate);
    CompileRun("var v = [42, 43]");
    v8::Local<v8::Context> ctx1 = v8::Local<v8::Context>::New(isolate, ctx1p);
    v8::Local<v8::Context> ctx2 = v8::Local<v8::Context>::New(isolate, ctx2p);
    v8::Local<v8::Value> v =
        ctx1->Global()->Get(ctx1, v8_str("v")).ToLocalChecked();
    ctx2->Enter();
    CHECK(ctx2->Global()->Set(ctx2, v8_str("o"), v).FromJust());
    v8::Local<v8::Value> res = CompileRun(
        "function f() { return o[0]; }"
        "%PrepareFunctionForOptimization(f);"
        "for (var i = 0; i < 10; ++i) f();"
        "%OptimizeFunctionOnNextCall(f);"
        "f();");
    CHECK_EQ(42, res->Int32Value(ctx2).FromJust());
    CHECK(ctx2->Global()
              ->Set(ctx2, v8_str("o"), v8::Int32::New(isolate, 0))
              .FromJust());
    ctx2->Exit();
    ctx1->Exit();
    ctx1p.Reset();
    isolate->ContextDisposedNotification();
  }
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(1, NumberOfGlobalObjects());
  ctx2p.Reset();
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(0, NumberOfGlobalObjects());
}

TEST(LeakNativeContextViaMapProto) {
  v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = CcTest::isolate();
  Heap* heap = CcTest::heap();
  v8::HandleScope outer_scope(isolate);
  v8::Persistent<v8::Context> ctx1p;
  v8::Persistent<v8::Context> ctx2p;
  {
    v8::HandleScope scope(isolate);
    ctx1p.Reset(isolate, v8::Context::New(isolate));
    ctx2p.Reset(isolate, v8::Context::New(isolate));
    v8::Local<v8::Context>::New(isolate, ctx1p)->Enter();
  }

  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(2, NumberOfGlobalObjects());

  {
    v8::HandleScope inner_scope(isolate);
    CompileRun("var v = { y: 42}");
    v8::Local<v8::Context> ctx1 = v8::Local<v8::Context>::New(isolate, ctx1p);
    v8::Local<v8::Context> ctx2 = v8::Local<v8::Context>::New(isolate, ctx2p);
    v8::Local<v8::Value> v =
        ctx1->Global()->Get(ctx1, v8_str("v")).ToLocalChecked();
    ctx2->Enter();
    CHECK(ctx2->Global()->Set(ctx2, v8_str("o"), v).FromJust());
    v8::Local<v8::Value> res = CompileRun(
        "function f() {"
        "  var p = {x: 42};"
        "  p.__proto__ = o;"
        "  return p.x;"
        "}"
        "%PrepareFunctionForOptimization(f);"
        "for (var i = 0; i < 10; ++i) f();"
        "%OptimizeFunctionOnNextCall(f);"
        "f();");
    CHECK_EQ(42, res->Int32Value(ctx2).FromJust());
    CHECK(ctx2->Global()
              ->Set(ctx2, v8_str("o"), v8::Int32::New(isolate, 0))
              .FromJust());
    ctx2->Exit();
    ctx1->Exit();
    ctx1p.Reset();
    isolate->ContextDisposedNotification();
  }
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(1, NumberOfGlobalObjects());
  ctx2p.Reset();
  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }
  CHECK_EQ(0, NumberOfGlobalObjects());
}

TEST(InstanceOfStubWriteBarrier) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  v8_flags.allow_natives_syntax = true;
#ifdef VERIFY_HEAP
  v8_flags.verify_heap = true;
#endif

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  if (!isolate->use_optimizer()) return;
  if (v8_flags.force_marking_deque_overflows) return;
  v8::HandleScope outer_scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();

  // Store native context in global as well to make it part of the root set when
  // starting incremental marking. This will ensure that function will be part
  // of the transitive closure during incremental marking.
  v8::Global<v8::Context> global_ctx(CcTest::isolate(), ctx);

  {
    v8::HandleScope scope(CcTest::isolate());
    CompileRun(
        "function foo () { }"
        "function mkbar () { return new (new Function(\"\")) (); }"
        "function f (x) { return (x instanceof foo); }"
        "function g () { f(mkbar()); }"
        "%PrepareFunctionForOptimization(f);"
        "f(new foo()); f(new foo());"
        "%OptimizeFunctionOnNextCall(f);"
        "f(new foo()); g();");
  }

  IncrementalMarking* marking = CcTest::heap()->incremental_marking();
  marking->Stop();
  CcTest::heap()->StartIncrementalMarking(i::GCFlag::kNoFlags,
                                          i::GarbageCollectionReason::kTesting);

  i::DirectHandle<JSFunction> f = i::Cast<JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(ctx, v8_str("f")).ToLocalChecked())));

  CHECK(f->HasAttachedOptimizedCode(isolate));

  MarkingState* marking_state = CcTest::heap()->marking_state();

  static constexpr auto kStepSize = v8::base::TimeDelta::FromMilliseconds(100);
  while (!marking_state->IsMarked(f->code(isolate))) {
    // Discard any pending GC requests otherwise we will get GC when we enter
    // code below.
    CHECK(!marking->IsMajorMarkingComplete());
    marking->AdvanceForTesting(kStepSize);
  }

  CHECK(marking->IsMarking());

  {
    v8::HandleScope scope(CcTest::isolate());
    v8::Local<v8::Object> global = CcTest::global();
    v8::Local<v8::Function> g = v8::Local<v8::Function>::Cast(
        global->Get(ctx, v8_str("g")).ToLocalChecked());
    g->Call(ctx, global, 0, nullptr).ToLocalChecked();
  }

  heap::InvokeMajorGC(CcTest::heap());
}

HEAP_TEST(GCFlags) {
  if (!v8_flags.incremental_marking) return;
  CcTest::InitializeVM();
  Heap* heap = CcTest::heap();

  heap->current_gc_flags_ = GCFlag::kNoFlags;
  // Check whether we appropriately reset flags after GC.
  heap::InvokeMajorGC(CcTest::heap(), GCFlag::kReduceMemoryFootprint);
  CHECK_EQ(heap->current_gc_flags_, GCFlag::kNoFlags);

  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }

  IncrementalMarking* marking = heap->incremental_marking();
  marking->Stop();
  heap->StartIncrementalMarking(GCFlag::kReduceMemoryFootprint,
                                GarbageCollectionReason::kTesting);
  CHECK(heap->current_gc_flags_ & GCFlag::kReduceMemoryFootprint);

  if (!v8_flags.separate_gc_phases) {
    heap::InvokeMinorGC(heap);
    // NewSpace scavenges should not overwrite the flags.
    CHECK(heap->current_gc_flags_ & GCFlag::kReduceMemoryFootprint);
  }

  heap::InvokeMajorGC(heap, GCFlag::kNoFlags);
  CHECK_EQ(heap->current_gc_flags_, GCFlag::kNoFlags);
}

HEAP_TEST(Regress845060) {
  if (v8_flags.single_generation) return;
  // Regression test for crbug.com/845060, where a raw pointer to a string's
  // data was kept across an allocation. If the allocation causes GC and
  // moves the string, such raw pointers become invalid.
  v8_flags.allow_natives_syntax = true;
  v8_flags.stress_incremental_marking = false;
  v8_flags.stress_compaction = false;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  LocalContext context;
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();

  // Preparation: create a string in new space.
  Local<Value> str = CompileRun("var str = (new Array(10000)).join('x'); str");
  CHECK(Heap::InYoungGeneration(*v8::Utils::OpenDirectHandle(*str)));

  // Use kReduceMemoryFootprintMask to unmap from space after scavenging.
  heap->StartIncrementalMarking(i::GCFlag::kReduceMemoryFootprint,
                                GarbageCollectionReason::kTesting);

  // Run the test (which allocates results) until the original string was
  // promoted to old space. Unmapping of from_space causes accesses to any
  // stale raw pointers to crash.
  CompileRun("while (%InYoungGeneration(str)) { str.split(''); }");
  CHECK(!Heap::InYoungGeneration(*v8::Utils::OpenDirectHandle(*str)));
}

TEST(OptimizedPretenuringAllocationFolding) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation)
    return;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array();"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = [[{}], [1.1]];"
                 "  }"
                 "  return elements[number_elements-1]"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  v8::Local<v8::Value> int_array =
      v8::Object::Cast(*res)->Get(ctx, v8_str("0")).ToLocalChecked();
  i::DirectHandle<JSObject> int_array_handle = i::Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(int_array)));
  v8::Local<v8::Value> double_array =
      v8::Object::Cast(*res)->Get(ctx, v8_str("1")).ToLocalChecked();
  i::DirectHandle<JSObject> double_array_handle = i::Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(double_array)));

  i::DirectHandle<JSReceiver> o =
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res));
  CHECK(CcTest::heap()->InOldSpace(*o));
  CHECK(CcTest::heap()->InOldSpace(*int_array_handle));
  CHECK(CcTest::heap()->InOldSpace(int_array_handle->elements()));
  CHECK(CcTest::heap()->InOldSpace(*double_array_handle));
  CHECK(CcTest::heap()->InOldSpace(double_array_handle->elements()));
}

TEST(OptimizedPretenuringObjectArrayLiterals) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation) {
    return;
  }
  v8::HandleScope scope(CcTest::isolate());
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array(number_elements);"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = [{}, {}, {}];"
                 "  }"
                 "  return elements[number_elements - 1];"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  i::DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));

  CHECK(CcTest::heap()->InOldSpace(o->elements()));
  CHECK(CcTest::heap()->InOldSpace(*o));
}

TEST(OptimizedPretenuringNestedInObjectProperties) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation) {
    return;
  }
  v8::HandleScope scope(CcTest::isolate());
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  // Keep the nested literal alive while its root is freed
  base::ScopedVector<char> source(1024);
  base::SNPrintF(
      source,
      "let number_elements = %d;"
      "let elements = new Array(number_elements);"
      "function f() {"
      "  for (let i = 0; i < number_elements; i++) {"
      "     let l =  {a: {b: {c: {d: {e: 2.2}, e: 3.3}, g: {h: 1.1}}}}; "
      "    elements[i] = l.a.b.c.d;"
      "  }"
      "  return elements[number_elements-1];"
      "};"
      "%%PrepareFunctionForOptimization(f);"
      "f(); gc(); gc();"
      "f(); f();"
      "%%OptimizeFunctionOnNextCall(f);"
      "f();",
      kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  i::DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));

  // Nested literal sites are only pretenured if the top level
  // literal is pretenured
  CHECK(Heap::InYoungGeneration(*o));
}

TEST(OptimizedPretenuringMixedInObjectProperties) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation)
    return;
  v8::HandleScope scope(CcTest::isolate());
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array(number_elements);"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = {a: {c: 2.2, d: {}}, b: 1.1};"
                 "  }"
                 "  return elements[number_elements - 1];"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  i::DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));

  CHECK(CcTest::heap()->InOldSpace(*o));
  FieldIndex idx1 = FieldIndex::ForPropertyIndex(o->map(), 0);
  FieldIndex idx2 = FieldIndex::ForPropertyIndex(o->map(), 1);
  CHECK(CcTest::heap()->InOldSpace(o->RawFastPropertyAt(idx1)));
  CHECK(CcTest::heap()->InOldSpace(o->RawFastPropertyAt(idx2)));

  Tagged<JSObject> inner_object = Cast<JSObject>(o->RawFastPropertyAt(idx1));
  CHECK(CcTest::heap()->InOldSpace(inner_object));
  CHECK(CcTest::heap()->InOldSpace(inner_object->RawFastPropertyAt(idx1)));
  CHECK(CcTest::heap()->InOldSpace(inner_object->RawFastPropertyAt(idx2)));
}

TEST(OptimizedPretenuringDoubleArrayProperties) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation)
    return;
  v8::HandleScope scope(CcTest::isolate());
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array(number_elements);"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = {a: 1.1, b: 2.2};"
                 "  }"
                 "  return elements[i - 1];"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  i::DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));

  CHECK(CcTest::heap()->InOldSpace(*o));
  CHECK_EQ(o->property_array(),
           ReadOnlyRoots(CcTest::heap()).empty_property_array());
}

TEST(OptimizedPretenuringDoubleArrayLiterals) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation)
    return;
  v8::HandleScope scope(CcTest::isolate());
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array(number_elements);"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = [1.1, 2.2, 3.3];"
                 "  }"
                 "  return elements[number_elements - 1];"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  i::DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));

  CHECK(CcTest::heap()->InOldSpace(o->elements()));
  CHECK(CcTest::heap()->InOldSpace(*o));
}

TEST(OptimizedPretenuringNestedMixedArrayLiterals) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation)
    return;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array(number_elements);"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = [[{}, {}, {}], [1.1, 2.2, 3.3]];"
                 "  }"
                 "  return elements[number_elements - 1];"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  v8::Local<v8::Value> int_array =
      v8::Object::Cast(*res)->Get(ctx, v8_str("0")).ToLocalChecked();
  i::DirectHandle<JSObject> int_array_handle = i::Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(int_array)));
  v8::Local<v8::Value> double_array =
      v8::Object::Cast(*res)->Get(ctx, v8_str("1")).ToLocalChecked();
  i::DirectHandle<JSObject> double_array_handle = i::Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(double_array)));

  DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));
  CHECK(CcTest::heap()->InOldSpace(*o));
  CHECK(CcTest::heap()->InOldSpace(*int_array_handle));
  CHECK(CcTest::heap()->InOldSpace(int_array_handle->elements()));
  CHECK(CcTest::heap()->InOldSpace(*double_array_handle));
  CHECK(CcTest::heap()->InOldSpace(double_array_handle->elements()));
}

TEST(OptimizedPretenuringNestedObjectLiterals) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation)
    return;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array(number_elements);"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = [[{}, {}, {}],[{}, {}, {}]];"
                 "  }"
                 "  return elements[number_elements - 1];"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  v8::Local<v8::Value> int_array_1 =
      v8::Object::Cast(*res)->Get(ctx, v8_str("0")).ToLocalChecked();
  DirectHandle<JSObject> int_array_handle_1 = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(int_array_1)));
  v8::Local<v8::Value> int_array_2 =
      v8::Object::Cast(*res)->Get(ctx, v8_str("1")).ToLocalChecked();
  DirectHandle<JSObject> int_array_handle_2 = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(int_array_2)));

  DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));
  CHECK(CcTest::heap()->InOldSpace(*o));
  CHECK(CcTest::heap()->InOldSpace(*int_array_handle_1));
  CHECK(CcTest::heap()->InOldSpace(int_array_handle_1->elements()));
  CHECK(CcTest::heap()->InOldSpace(*int_array_handle_2));
  CHECK(CcTest::heap()->InOldSpace(int_array_handle_2->elements()));
}

TEST(OptimizedPretenuringNestedDoubleLiterals) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.stress_concurrent_allocation)
    return;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  ManualGCScope manual_gc_scope;
  GrowNewSpaceToMaximumCapacity(CcTest::heap());

  base::ScopedVector<char> source(1024);
  base::SNPrintF(source,
                 "var number_elements = %d;"
                 "var elements = new Array(number_elements);"
                 "function f() {"
                 "  for (var i = 0; i < number_elements; i++) {"
                 "    elements[i] = [[1.1, 1.2, 1.3],[2.1, 2.2, 2.3]];"
                 "  }"
                 "  return elements[number_elements - 1];"
                 "};"
                 "%%PrepareFunctionForOptimization(f);"
                 "f(); gc();"
                 "f(); f();"
                 "%%OptimizeFunctionOnNextCall(f);"
                 "f();",
                 kPretenureCreationCount);

  v8::Local<v8::Value> res = CompileRun(source.begin());

  v8::Local<v8::Value> double_array_1 =
      v8::Object::Cast(*res)->Get(ctx, v8_str("0")).ToLocalChecked();
  i::DirectHandle<JSObject> double_array_handle_1 =
      i::Cast<JSObject>(v8::Utils::OpenDirectHandle(
          *v8::Local<v8::Object>::Cast(double_array_1)));
  v8::Local<v8::Value> double_array_2 =
      v8::Object::Cast(*res)->Get(ctx, v8_str("1")).ToLocalChecked();
  i::DirectHandle<JSObject> double_array_handle_2 =
      Cast<JSObject>(v8::Utils::OpenDirectHandle(
          *v8::Local<v8::Object>::Cast(double_array_2)));

  i::DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));
  CHECK(CcTest::heap()->InOldSpace(*o));
  CHECK(CcTest::heap()->InOldSpace(*double_array_handle_1));
  CHECK(CcTest::heap()->InOldSpace(double_array_handle_1->elements()));
  CHECK(CcTest::heap()->InOldSpace(*double_array_handle_2));
  CHECK(CcTest::heap()->InOldSpace(double_array_handle_2->elements()));
}

// Test regular array literals allocation.
TEST(OptimizedAllocationArrayLiterals) {
  v8_flags.allow_natives_syntax = true;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  if (!CcTest::i_isolate()->use_optimizer() || v8_flags.always_turbofan) return;
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking)
    return;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  v8::Local<v8::Value> res = CompileRun(
      "function f() {"
      "  var numbers = new Array(1, 2, 3);"
      "  numbers[0] = 3.14;"
      "  return numbers;"
      "};"
      "%PrepareFunctionForOptimization(f);"
      "f(); f(); f();"
      "%OptimizeFunctionOnNextCall(f);"
      "f();");
  CHECK_EQ(static_cast<int>(3.14), v8::Object::Cast(*res)
                                       ->Get(ctx, v8_str("0"))
                                       .ToLocalChecked()
                                       ->Int32Value(ctx)
                                       .FromJust());

  i::DirectHandle<JSObject> o = Cast<JSObject>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(res)));

  CHECK(InCorrectGeneration(o->elements()));
}

static int CountMapTransitions(i::Isolate* isolate, Tagged<Map> map) {
  return TransitionsAccessor(isolate, map).NumberOfTransitions();
}

// Test that map transitions are cleared and maps are collected with
// incremental marking as well.
TEST(Regress1465) {
  if (!v8_flags.incremental_marking) return;
  v8_flags.stress_compaction = false;
  v8_flags.stress_incremental_marking = false;
  v8_flags.allow_natives_syntax = true;
  v8_flags.trace_incremental_marking = true;
  v8_flags.retain_maps_for_n_gc = 0;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  v8::HandleScope scope(isolate);
  static const int transitions_count = 256;

  CompileRun("function F() {}");
  {
    AlwaysAllocateScopeForTesting always_allocate(heap);
    for (int i = 0; i < transitions_count; i++) {
      base::EmbeddedVector<char, 64> buffer;
      base::SNPrintF(buffer, "var o = new F; o.prop%d = %d;", i, i);
      CompileRun(buffer.begin());
    }
    CompileRun("var root = new F;");
  }

  i::IndirectHandle<JSReceiver> root =
      v8::Utils::OpenIndirectHandle(*v8::Local<v8::Object>::Cast(
          CcTest::global()
              ->Get(isolate->GetCurrentContext(), v8_str("root"))
              .ToLocalChecked()));

  // Count number of live transitions before marking.
  int transitions_before = CountMapTransitions(i_isolate, root->map());
  CompileRun("%DebugPrint(root);");
  CHECK_EQ(transitions_count, transitions_before);

  heap::SimulateIncrementalMarking(heap);
  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  // Count number of live transitions after marking.  Note that one transition
  // is left, because 'o' still holds an instance of one transition target.
  int transitions_after = CountMapTransitions(i_isolate, root->map());
  CompileRun("%DebugPrint(root);");
  CHECK_EQ(1, transitions_after);
}

static i::Handle<JSObject> GetByName(const char* name) {
  return i::Cast<i::JSObject>(
      v8::Utils::OpenHandle(*v8::Local<v8::Object>::Cast(
          CcTest::global()
              ->Get(CcTest::isolate()->GetCurrentContext(), v8_str(name))
              .ToLocalChecked())));
}

#ifdef DEBUG
static void AddTransitions(int transitions_count) {
  AlwaysAllocateScopeForTesting always_allocate(CcTest::i_isolate()->heap());
  for (int i = 0; i < transitions_count; i++) {
    base::EmbeddedVector<char, 64> buffer;
    base::SNPrintF(buffer, "var o = new F; o.prop%d = %d;", i, i);
    CompileRun(buffer.begin());
  }
}

static void AddPropertyTo(int gc_count, Handle<JSObject> object,
                          const char* property_name) {
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Handle<String> prop_name = factory->InternalizeUtf8String(property_name);
  Handle<Smi> twenty_three(Smi::FromInt(23), isolate);
  HeapAllocator::SetAllocationGcInterval(gc_count);
  v8_flags.gc_global = true;
  v8_flags.retain_maps_for_n_gc = 0;
  CcTest::heap()->set_allocation_timeout(gc_count);
  Object::SetProperty(isolate, object, prop_name, twenty_three).Check();
}

TEST(TransitionArrayShrinksDuringAllocToZero) {
  v8_flags.stress_compaction = false;
  v8_flags.stress_incremental_marking = false;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  i::Isolate* i_isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());
  static const int transitions_count = 10;
  CompileRun("function F() { }");
  AddTransitions(transitions_count);
  CompileRun("var root = new F;");
  IndirectHandle<JSObject> root = GetByName("root");

  // Count number of live transitions before marking.
  int transitions_before = CountMapTransitions(i_isolate, root->map());
  CHECK_EQ(transitions_count, transitions_before);

  // Get rid of o
  CompileRun(
      "o = new F;"
      "root = new F");
  root = GetByName("root");
  AddPropertyTo(2, root, "funny");
  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        CcTest::heap());
    heap::InvokeMinorGC(CcTest::heap());
  }

  // Count number of live transitions after marking.  Note that one transition
  // is left, because 'o' still holds an instance of one transition target.
  int transitions_after =
      CountMapTransitions(i_isolate, Cast<Map>(root->map()->GetBackPointer()));
  CHECK_EQ(1, transitions_after);
}

TEST(TransitionArrayShrinksDuringAllocToOne) {
  v8_flags.stress_compaction = false;
  v8_flags.stress_incremental_marking = false;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  i::Isolate* i_isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());
  static const int transitions_count = 10;
  CompileRun("function F() {}");
  AddTransitions(transitions_count);
  CompileRun("var root = new F;");
  IndirectHandle<JSObject> root = GetByName("root");

  // Count number of live transitions before marking.
  int transitions_before = CountMapTransitions(i_isolate, root->map());
  CHECK_EQ(transitions_count, transitions_before);

  root = GetByName("root");
  AddPropertyTo(2, root, "funny");
  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        CcTest::heap());
    heap::InvokeMinorGC(CcTest::heap());
  }

  // Count number of live transitions after marking.  Note that one transition
  // is left, because 'o' still holds an instance of one transition target.
  int transitions_after =
      CountMapTransitions(i_isolate, Cast<Map>(root->map()->GetBackPointer()));
  CHECK_EQ(2, transitions_after);
}

TEST(TransitionArrayShrinksDuringAllocToOnePropertyFound) {
  v8_flags.stress_compaction = false;
  v8_flags.stress_incremental_marking = false;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  i::Isolate* i_isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());
  static const int transitions_count = 10;
  CompileRun("function F() {}");
  AddTransitions(transitions_count);
  CompileRun("var root = new F;");
  Handle<JSObject> root = GetByName("root");

  // Count number of live transitions before marking.
  int transitions_before = CountMapTransitions(i_isolate, root->map());
  CHECK_EQ(transitions_count, transitions_before);

  root = GetByName("root");
  AddPropertyTo(0, root, "prop9");
  heap::InvokeMajorGC(CcTest::heap());

  // Count number of live transitions after marking.  Note that one transition
  // is left, because 'o' still holds an instance of one transition target.
  int transitions_after =
      CountMapTransitions(i_isolate, Cast<Map>(root->map()->GetBackPointer()));
  CHECK_EQ(1, transitions_after);
}
#endif  // DEBUG

TEST(ReleaseOverReservedPages) {
  if (!v8_flags.compact) return;
  v8_flags.trace_gc = true;
  // The optimizer can allocate stuff, messing up the test.
#if !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  v8_flags.turbofan = false;
  v8_flags.always_turbofan = false;
#endif  // !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  // - Parallel compaction increases fragmentation, depending on how existing
  //   memory is distributed. Since this is non-deterministic because of
  //   concurrent sweeping, we disable it for this test.
  // - Concurrent sweeping adds non determinism, depending on when memory is
  //   available for further reuse.
  // - Fast evacuation of pages may result in a different page count in old
  //   space.
  ManualGCScope manual_gc_scope;
  v8_flags.page_promotion = false;
  v8_flags.parallel_compaction = false;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  // If there's snapshot available, we don't know whether 20 small arrays will
  // fit on the initial pages.
  if (!isolate->snapshot_available()) return;
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  v8::HandleScope scope(CcTest::isolate());

  // Ensure that the young generation is empty.
  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::EmptyNewSpaceUsingGC(heap);
  }
  static const int number_of_test_pages = 20;

  // Prepare many pages with low live-bytes count.
  PagedSpace* old_space = heap->old_space();
  const int initial_page_count = old_space->CountTotalPages();
  const int overall_page_count = number_of_test_pages + initial_page_count;
  for (int i = 0; i < number_of_test_pages; i++) {
    AlwaysAllocateScopeForTesting always_allocate(heap);
    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::SimulateFullSpace(old_space);
    }
    factory->NewFixedArray(1, AllocationType::kOld);
  }
  CHECK_EQ(overall_page_count, old_space->CountTotalPages());

  DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);

  // Triggering one GC will cause a lot of garbage to be discovered but
  // even spread across all allocated pages.
  heap::InvokeMajorGC(heap);
  CHECK_GE(overall_page_count, old_space->CountTotalPages());

  // Triggering subsequent GCs should cause at least half of the pages
  // to be released to the OS after at most two cycles.
  heap::InvokeMajorGC(heap);
  CHECK_GE(overall_page_count, old_space->CountTotalPages());
  heap::InvokeMajorGC(heap);
  CHECK_GE(number_of_test_pages,
           (old_space->CountTotalPages() - initial_page_count) * 2);

  // Triggering a last-resort GC should cause all pages to be released to the
  // OS so that other processes can seize the memory.  If we get a failure here
  // where there are 2 pages left instead of 1, then we should increase the
  // size of the first page a little in SizeOfFirstPage in spaces.cc.  The
  // first page should be small in order to reduce memory used when the VM
  // boots, but if the 20 small arrays don't fit on the first page then that's
  // an indication that it is too small.
  heap::InvokeMemoryReducingMajorGCs(heap);
  CHECK_GE(initial_page_count, old_space->CountTotalPages());
}

static int forced_gc_counter = 0;

void MockUseCounterCallback(v8::Isolate* isolate,
                            v8::Isolate::UseCounterFeature feature) {
  isolate->GetCurrentContext();
  if (feature == v8::Isolate::kForcedGC) {
    forced_gc_counter++;
  }
}

TEST(CountForcedGC) {
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  isolate->SetUseCounterCallback(MockUseCounterCallback);

  forced_gc_counter = 0;
  const char* source = "gc();";
  CompileRun(source);
  CHECK_GT(forced_gc_counter, 0);
}

#ifdef OBJECT_PRINT
TEST(PrintSharedFunctionInfo) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  const char* source =
      "f = function() { return 987654321; }\n"
      "g = function() { return 123456789; }\n";
  CompileRun(source);
  i::DirectHandle<JSFunction> g = i::Cast<JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(ctx, v8_str("g")).ToLocalChecked())));

  StdoutStream os;
  Print(g->shared(), os);
  os << std::endl;
}
#endif  // OBJECT_PRINT

TEST(IncrementalMarkingPreservesMonomorphicCallIC) {
  if (!v8_flags.use_ic) return;
  if (!v8_flags.incremental_marking) return;
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Value> fun1, fun2;
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  {
    CompileRun("function fun() {};");
    fun1 = CcTest::global()->Get(ctx, v8_str("fun")).ToLocalChecked();
  }

  {
    CompileRun("function fun() {};");
    fun2 = CcTest::global()->Get(ctx, v8_str("fun")).ToLocalChecked();
  }

  // Prepare function f that contains type feedback for the two closures.
  CHECK(CcTest::global()->Set(ctx, v8_str("fun1"), fun1).FromJust());
  CHECK(CcTest::global()->Set(ctx, v8_str("fun2"), fun2).FromJust());
  CompileRun(
      "function f(a, b) { a(); b(); } %EnsureFeedbackVectorForFunction(f); "
      "f(fun1, fun2);");

  DirectHandle<JSFunction> f = Cast<JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(ctx, v8_str("f")).ToLocalChecked())));

  Handle<FeedbackVector> feedback_vector(f->feedback_vector(), f->GetIsolate());
  FeedbackVectorHelper feedback_helper(feedback_vector);

  int expected_slots = 2;
  CHECK_EQ(expected_slots, feedback_helper.slot_count());
  int slot1 = 0;
  int slot2 = 1;
  CHECK(feedback_vector->Get(feedback_helper.slot(slot1)).IsWeak());
  CHECK(feedback_vector->Get(feedback_helper.slot(slot2)).IsWeak());

  heap::SimulateIncrementalMarking(CcTest::heap());
  heap::InvokeMajorGC(CcTest::heap());

  CHECK(feedback_vector->Get(feedback_helper.slot(slot1)).IsWeak());
  CHECK(feedback_vector->Get(feedback_helper.slot(slot2)).IsWeak());
}

static void CheckVectorIC(DirectHandle<JSFunction> f, int slot_index,
                          InlineCacheState desired_state) {
  Handle<FeedbackVector> vector =
      Handle<FeedbackVector>(f->feedback_vector(), f->GetIsolate());
  FeedbackVectorHelper helper(vector);
  FeedbackSlot slot = helper.slot(slot_index);
  FeedbackNexus nexus(CcTest::i_isolate(), vector, slot);
  CHECK(nexus.ic_state() == desired_state);
}

TEST(IncrementalMarkingPreservesMonomorphicConstructor) {
  if (!v8_flags.incremental_marking) return;
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  // Prepare function f that contains a monomorphic IC for object
  // originating from the same native context.
  CompileRun(
      "function fun() { this.x = 1; };"
      "function f(o) { return new o(); }"
      "%EnsureFeedbackVectorForFunction(f);"
      "f(fun); f(fun);");
  DirectHandle<JSFunction> f = Cast<JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(ctx, v8_str("f")).ToLocalChecked())));

  DirectHandle<FeedbackVector> vector(f->feedback_vector(), f->GetIsolate());
  CHECK(vector->Get(FeedbackSlot(0)).IsWeakOrCleared());

  heap::SimulateIncrementalMarking(CcTest::heap());
  heap::InvokeMajorGC(CcTest::heap());

  CHECK(vector->Get(FeedbackSlot(0)).IsWeakOrCleared());
}

TEST(IncrementalMarkingPreservesMonomorphicIC) {
  if (!v8_flags.use_ic) return;
  if (!v8_flags.incremental_marking) return;
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();
  // Prepare function f that contains a monomorphic IC for object
  // originating from the same native context.
  CompileRun(
      "function fun() { this.x = 1; }; var obj = new fun();"
      "%EnsureFeedbackVectorForFunction(f);"
      "function f(o) { return o.x; } f(obj); f(obj);");
  DirectHandle<JSFunction> f = Cast<JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(ctx, v8_str("f")).ToLocalChecked())));

  CheckVectorIC(f, 0, InlineCacheState::MONOMORPHIC);

  heap::SimulateIncrementalMarking(CcTest::heap());
  heap::InvokeMajorGC(CcTest::heap());

  CheckVectorIC(f, 0, InlineCacheState::MONOMORPHIC);
}

TEST(IncrementalMarkingPreservesPolymorphicIC) {
  if (!v8_flags.use_ic) return;
  if (!v8_flags.incremental_marking) return;
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Value> obj1, obj2;
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();

  {
    LocalContext env;
    CompileRun("function fun() { this.x = 1; }; var obj = new fun();");
    obj1 = env->Global()->Get(env.local(), v8_str("obj")).ToLocalChecked();
  }

  {
    LocalContext env;
    CompileRun("function fun() { this.x = 2; }; var obj = new fun();");
    obj2 = env->Global()->Get(env.local(), v8_str("obj")).ToLocalChecked();
  }

  // Prepare function f that contains a polymorphic IC for objects
  // originating from two different native contexts.
  CHECK(CcTest::global()->Set(ctx, v8_str("obj1"), obj1).FromJust());
  CHECK(CcTest::global()->Set(ctx, v8_str("obj2"), obj2).FromJust());
  CompileRun(
      "function f(o) { return o.x; }; "
      "%EnsureFeedbackVectorForFunction(f);"
      "f(obj1); f(obj1); f(obj2);");
  DirectHandle<JSFunction> f = Cast<JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(ctx, v8_str("f")).ToLocalChecked())));

  CheckVectorIC(f, 0, InlineCacheState::POLYMORPHIC);

  // Fire context dispose notification.
  heap::SimulateIncrementalMarking(CcTest::heap());
  heap::InvokeMajorGC(CcTest::heap());

  CheckVectorIC(f, 0, InlineCacheState::POLYMORPHIC);
}

TEST(ContextDisposeDoesntClearPolymorphicIC) {
  if (!v8_flags.use_ic) return;
  if (!v8_flags.incremental_marking) return;
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Value> obj1, obj2;
  v8::Local<v8::Context> ctx = CcTest::isolate()->GetCurrentContext();

  {
    LocalContext env;
    CompileRun("function fun() { this.x = 1; }; var obj = new fun();");
    obj1 = env->Global()->Get(env.local(), v8_str("obj")).ToLocalChecked();
  }

  {
    LocalContext env;
    CompileRun("function fun() { this.x = 2; }; var obj = new fun();");
    obj2 = env->Global()->Get(env.local(), v8_str("obj")).ToLocalChecked();
  }

  // Prepare function f that contains a polymorphic IC for objects
  // originating from two different native contexts.
  CHECK(CcTest::global()->Set(ctx, v8_str("obj1"), obj1).FromJust());
  CHECK(CcTest::global()->Set(ctx, v8_str("obj2"), obj2).FromJust());
  CompileRun(
      "function f(o) { return o.x; }; "
      "%EnsureFeedbackVectorForFunction(f);"
      "f(obj1); f(obj1); f(obj2);");
  DirectHandle<JSFunction> f = Cast<JSFunction>(
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(ctx, v8_str("f")).ToLocalChecked())));

  CheckVectorIC(f, 0, InlineCacheState::POLYMORPHIC);

  // Fire context dispose notification.
  CcTest::isolate()->ContextDisposedNotification();
  heap::SimulateIncrementalMarking(CcTest::heap());
  heap::InvokeMajorGC(CcTest::heap());

  CheckVectorIC(f, 0, InlineCacheState::POLYMORPHIC);
}

class SourceResource : public v8::String::ExternalOneByteStringResource {
 public:
  explicit SourceResource(const char* data)
    : data_(data), length_(strlen(data)) { }

  void Dispose() override {
    i::DeleteArray(data_);
    data_ = nullptr;
  }

  const char* data() const override { return data_; }

  size_t length() const override { return length_; }

  bool IsDisposed() { return data_ == nullptr; }

 private:
  const char* data_;
  size_t length_;
};

void ReleaseStackTraceDataTest(v8::Isolate* isolate, const char* source,
                               const char* accessor) {
  // Test that the data retained by the Error.stack accessor is released
  // after the first time the accessor is fired.  We use external string
  // to check whether the data is being released since the external string
  // resource's callback is fired when the external string is GC'ed.
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::Heap* heap = i_isolate->heap();
  v8::HandleScope scope(isolate);

  SourceResource* resource = new SourceResource(i::StrDup(source));
  {
    v8::HandleScope new_scope(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::String> source_string =
        v8::String::NewExternalOneByte(isolate, resource).ToLocalChecked();
    heap::InvokeMemoryReducingMajorGCs(heap);
    v8::Script::Compile(ctx, source_string)
        .ToLocalChecked()
        ->Run(ctx)
        .ToLocalChecked();
    CHECK(!resource->IsDisposed());
  }
  CHECK(!resource->IsDisposed());

  CompileRun(accessor);

  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMemoryReducingMajorGCs(heap);
  }

  // External source has been released.
  CHECK(resource->IsDisposed());
  delete resource;
}

UNINITIALIZED_TEST(ReleaseStackTraceData) {
#ifndef V8_LITE_MODE
  // ICs retain objects.
  v8_flags.use_ic = false;
#endif  // V8_LITE_MODE
  v8_flags.concurrent_recompilation = false;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Context::New(isolate)->Enter();
    static const char* source1 =
        "var error = null;            "
        /* Normal Error */
        "try {                        "
        "  throw new Error();         "
        "} catch (e) {                "
        "  error = e;                 "
        "}                            ";
    static const char* source2 =
        "var error = null;            "
        /* Stack overflow */
        "try {                        "
        "  (function f() { f(); })(); "
        "} catch (e) {                "
        "  error = e;                 "
        "}                            ";
    static const char* source3 =
        "var error = null;            "
        /* Normal Error */
        "try {                        "
        /* as prototype */
        "  throw new Error();         "
        "} catch (e) {                "
        "  error = {};                "
        "  error.__proto__ = e;       "
        "}                            ";
    static const char* source4 =
        "var error = null;            "
        /* Stack overflow */
        "try {                        "
        /* as prototype   */
        "  (function f() { f(); })(); "
        "} catch (e) {                "
        "  error = {};                "
        "  error.__proto__ = e;       "
        "}                            ";
    static const char* getter = "error.stack";
    static const char* setter = "error.stack = 0";

    ReleaseStackTraceDataTest(isolate, source1, setter);
    ReleaseStackTraceDataTest(isolate, source2, setter);
    // We do not test source3 and source4 with setter, since the setter is
    // supposed to (untypically) write to the receiver, not the holder.  This is
    // to emulate the behavior of a data property.

    ReleaseStackTraceDataTest(isolate, source1, getter);
    ReleaseStackTraceDataTest(isolate, source2, getter);
    ReleaseStackTraceDataTest(isolate, source3, getter);
    ReleaseStackTraceDataTest(isolate, source4, getter);
  }
  isolate->Dispose();
}

// TODO(mmarchini) also write tests for async/await and Promise.all
void DetailedErrorStackTraceTest(const char* src,
                                 std::function<void(Handle<FixedArray>)> test) {
  v8_flags.detailed_error_stack_trace = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  v8::TryCatch try_catch(CcTest::isolate());
  CompileRun(src);

  CHECK(try_catch.HasCaught());
  Handle<Object> exception = v8::Utils::OpenHandle(*try_catch.Exception());

  test(CcTest::i_isolate()->GetSimpleStackTrace(Cast<JSReceiver>(exception)));
}

Tagged<FixedArray> ParametersOf(DirectHandle<FixedArray> stack_trace,
                                int frame_index) {
  return Cast<CallSiteInfo>(stack_trace->get(frame_index))->parameters();
}

// * Test interpreted function error
TEST(DetailedErrorStackTrace) {
  static const char* source =
      "function func1(arg1) {       "
      "  let err = new Error();     "
      "  throw err;                 "
      "}                            "
      "function func2(arg1, arg2) { "
      "  func1(42);                 "
      "}                            "
      "class Foo {};                "
      "function main(arg1, arg2) {  "
      "  func2(arg1, false);        "
      "}                            "
      "var foo = new Foo();         "
      "main(foo);                   ";

  DetailedErrorStackTraceTest(source, [](DirectHandle<FixedArray> stack_trace) {
    Tagged<FixedArray> foo_parameters = ParametersOf(stack_trace, 0);
    CHECK_EQ(foo_parameters->length(), 1);
    CHECK(IsSmi(foo_parameters->get(0)));
    CHECK_EQ(Smi::ToInt(foo_parameters->get(0)), 42);

    Tagged<FixedArray> bar_parameters = ParametersOf(stack_trace, 1);
    CHECK_EQ(bar_parameters->length(), 2);
    CHECK(IsJSObject(bar_parameters->get(0)));
    CHECK(IsBoolean(bar_parameters->get(1)));
    DirectHandle<Object> foo = Cast<Object>(GetByName("foo"));
    CHECK_EQ(bar_parameters->get(0), *foo);
    CHECK(!Object::BooleanValue(bar_parameters->get(1), CcTest::i_isolate()));

    Tagged<FixedArray> main_parameters = ParametersOf(stack_trace, 2);
    CHECK_EQ(main_parameters->length(), 2);
    CHECK(IsJSObject(main_parameters->get(0)));
    CHECK(IsUndefined(main_parameters->get(1)));
    CHECK_EQ(main_parameters->get(0), *foo);
  });
}

// * Test optimized function with inline frame error
TEST(DetailedErrorStackTraceInline) {
  v8_flags.allow_natives_syntax = true;
  static const char* source =
      "function add(x) {                     "
      " if (x == 42)                         "
      "  throw new Error();                  "
      " return x + x;                        "
      "}                                     "
      "add(0);                               "
      "add(1);                               "
      "function foo(x) {                     "
      " return add(x + 1)                    "
      "}                                     "
      "%PrepareFunctionForOptimization(foo); "
      "foo(40);                              "
      "%OptimizeFunctionOnNextCall(foo);     "
      "foo(41);                              ";

  DetailedErrorStackTraceTest(source, [](DirectHandle<FixedArray> stack_trace) {
    Tagged<FixedArray> parameters_add = ParametersOf(stack_trace, 0);
    CHECK_EQ(parameters_add->length(), 1);
    CHECK(IsSmi(parameters_add->get(0)));
    CHECK_EQ(Smi::ToInt(parameters_add->get(0)), 42);

    Tagged<FixedArray> parameters_foo = ParametersOf(stack_trace, 1);
    CHECK_EQ(parameters_foo->length(), 1);
    CHECK(IsSmi(parameters_foo->get(0)));
    CHECK_EQ(Smi::ToInt(parameters_foo->get(0)), 41);
  });
}

// * Test builtin exit error
TEST(DetailedErrorStackTraceBuiltinExit) {
  static const char* source =
      "function test(arg1) {           "
      "  (new Number()).toFixed(arg1); "
      "}                               "
      "test(9999);                     ";

  DetailedErrorStackTraceTest(source, [](DirectHandle<FixedArray> stack_trace) {
    Tagged<FixedArray> parameters = ParametersOf(stack_trace, 0);

    CHECK_EQ(parameters->length(), 2);
    CHECK(IsSmi(parameters->get(1)));
    CHECK_EQ(Smi::ToInt(parameters->get(1)), 9999);
  });
}

TEST(Regress169928) {
  v8_flags.allow_natives_syntax = true;
#if !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  v8_flags.turbofan = false;
#endif  // !defined(V8_LITE_MODE) && defined(V8_ENABLE_TURBOFAN)
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  LocalContext env;
  Factory* factory = isolate->factory();
  v8::HandleScope scope(CcTest::isolate());

  // Some flags turn Scavenge collections into Mark-sweep collections
  // and hence are incompatible with this test case.
  if (v8_flags.gc_global || v8_flags.stress_compaction ||
      v8_flags.stress_incremental_marking || v8_flags.single_generation ||
      v8_flags.minor_ms)
    return;

  // Prepare the environment
  CompileRun(
      "function fastliteralcase(literal, value) {"
      "    literal[0] = value;"
      "    return literal;"
      "}"
      "function get_standard_literal() {"
      "    var literal = [1, 2, 3];"
      "    return literal;"
      "}"
      "obj = fastliteralcase(get_standard_literal(), 1);"
      "obj = fastliteralcase(get_standard_literal(), 1.5);"
      "obj = fastliteralcase(get_standard_literal(), 2);");

  // prepare the heap
  v8::Local<v8::String> mote_code_string =
      v8_str("fastliteralcase(mote, 2.5);");

  v8::Local<v8::String> array_name = v8_str("mote");
  CHECK(CcTest::global()
            ->Set(env.local(), array_name, v8::Int32::New(CcTest::isolate(), 0))
            .FromJust());

  // First make sure we flip spaces
  heap::InvokeMinorGC(CcTest::heap());

  // Allocate the object.
  DirectHandle<FixedArray> array_data =
      factory->NewFixedArray(2, AllocationType::kYoung);
  array_data->set(0, Smi::FromInt(1));
  array_data->set(1, Smi::FromInt(2));

  heap::FillCurrentPageButNBytes(
      SemiSpaceNewSpace::From(CcTest::heap()->new_space()),
      JSArray::kHeaderSize + AllocationMemento::kSize + kTaggedSize);

  Handle<JSArray> array =
      factory->NewJSArrayWithElements(array_data, PACKED_SMI_ELEMENTS);

  CHECK_EQ(Smi::FromInt(2), array->length());
  CHECK(array->HasSmiOrObjectElements());

  // We need filler the size of AllocationMemento object, plus an extra
  // fill pointer value.
  Tagged<HeapObject> obj;
  AllocationResult allocation =
      CcTest::heap()->allocator()->new_space_allocator()->AllocateRaw(
          AllocationMemento::kSize + kTaggedSize, kTaggedAligned,
          AllocationOrigin::kRuntime);
  CHECK(allocation.To(&obj));
  Address addr_obj = obj.address();
  CcTest::heap()->CreateFillerObjectAt(addr_obj,
                                       AllocationMemento::kSize + kTaggedSize);

  // Give the array a name, making sure not to allocate strings.
  v8::Local<v8::Object> array_obj = v8::Utils::ToLocal(array);
  CHECK(CcTest::global()->Set(env.local(), array_name, array_obj).FromJust());

  // This should crash with a protection violation if we are running a build
  // with the bug.
  AlwaysAllocateScopeForTesting aa_scope(isolate->heap());
  v8::Script::Compile(env.local(), mote_code_string)
      .ToLocalChecked()
      ->Run(env.local())
      .ToLocalChecked();
}

TEST(LargeObjectSlotRecording) {
  if (!v8_flags.incremental_marking) return;
  if (!v8_flags.compact) return;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  HandleScope scope(isolate);

  // Create an object on an evacuation candidate.
  heap::SimulateFullSpace(heap->old_space());
  IndirectHandle<FixedArray> lit =
      isolate->factory()->NewFixedArray(4, AllocationType::kOld);
  PageMetadata* evac_page = PageMetadata::FromHeapObject(*lit);
  heap::ForceEvacuationCandidate(evac_page);
  Tagged<FixedArray> old_location = *lit;

  // Allocate a large object.
  int size = std::max(1000000, kMaxRegularHeapObjectSize + KB);
  CHECK_LT(kMaxRegularHeapObjectSize, size);
  IndirectHandle<FixedArray> lo =
      isolate->factory()->NewFixedArray(size, AllocationType::kOld);
  CHECK(heap->lo_space()->Contains(*lo));

  // Start incremental marking to active write barrier.
  heap::SimulateIncrementalMarking(heap, false);

  // Create references from the large object to the object on the evacuation
  // candidate.
  const int kStep = size / 10;
  for (int i = 0; i < size; i += kStep) {
    lo->set(i, *lit);
    CHECK(lo->get(i) == old_location);
  }

  heap::SimulateIncrementalMarking(heap, true);

  // Move the evacuation candidate object.
  {
    // We need to invoke GC without stack, otherwise no compaction is performed.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  // Verify that the pointers in the large object got updated.
  for (int i = 0; i < size; i += kStep) {
    CHECK_EQ(lo->get(i).ptr(), lit->ptr());
    CHECK_NE(lo->get(i).ptr(), old_location.ptr());
  }
}

class DummyVisitor : public RootVisitor {
 public:
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {}
};

TEST(PersistentHandles) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  v8::HandleScope scope(reinterpret_cast<v8::Isolate*>(isolate));
  HandleScopeData* data = isolate->handle_scope_data();
  IndirectHandle<Object> init(ReadOnlyRoots(heap).empty_string(), isolate);
  while (data->next < data->limit) {
    IndirectHandle<Object> obj(ReadOnlyRoots(heap).empty_string(), isolate);
  }
  // An entire block of handles has been filled.
  // Next handle would require a new block.
  CHECK(data->next == data->limit);

  PersistentHandlesScope persistent(isolate);
  DummyVisitor visitor;
  isolate->handle_scope_implementer()->Iterate(&visitor);
  persistent.Detach();
}

static void TestFillersFromPersistentHandles(bool promote) {
  // We assume that the fillers can only arise when left-trimming arrays.
  ManualGCScope manual_gc_scope;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  v8::HandleScope scope(reinterpret_cast<v8::Isolate*>(isolate));

  const size_t n = 10;
  DirectHandle<FixedArray> array = isolate->factory()->NewFixedArray(n);

  if (promote) {
    // Age the array so it's ready for promotion on next GC.
    heap::InvokeMinorGC(heap);
  }
  CHECK(Heap::InYoungGeneration(*array));

  PersistentHandlesScope persistent_scope(isolate);

  // Trim the array three times to different sizes so all kinds of fillers are
  // created and tracked by the persistent handles.
  DirectHandle<FixedArrayBase> filler_1(*array, isolate);
  DirectHandle<FixedArrayBase> filler_2(heap->LeftTrimFixedArray(*filler_1, 1),
                                        isolate);
  DirectHandle<FixedArrayBase> filler_3(heap->LeftTrimFixedArray(*filler_2, 2),
                                        isolate);
  DirectHandle<FixedArrayBase> tail(heap->LeftTrimFixedArray(*filler_3, 3),
                                    isolate);

  std::unique_ptr<PersistentHandles> persistent_handles(
      persistent_scope.Detach());

  // GC should retain the trimmed array but drop all of the three fillers.
  heap::InvokeMinorGC(heap);
  if (!v8_flags.single_generation) {
    if (promote) {
      CHECK(heap->InOldSpace(*tail));
    } else {
      CHECK(Heap::InYoungGeneration(*tail));
    }
  }
  CHECK_EQ(n - 6, tail->length());
  CHECK(!IsHeapObject(*filler_1));
  CHECK(!IsHeapObject(*filler_2));
  CHECK(!IsHeapObject(*filler_3));
}

TEST(DoNotEvacuateFillersFromPersistentHandles) {
  if (v8_flags.single_generation || v8_flags.move_object_start) return;
  TestFillersFromPersistentHandles(false /*promote*/);
}

TEST(DoNotPromoteFillersFromPersistentHandles) {
  if (v8_flags.single_generation || v8_flags.move_object_start) return;
  TestFillersFromPersistentHandles(true /*promote*/);
}

TEST(IncrementalMarkingStepMakesBigProgressWithLargeObjects) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  CompileRun(
      "function f(n) {"
      "    var a = new Array(n);"
      "    for (var i = 0; i < n; i += 100) a[i] = i;"
      "};"
      "f(10 * 1024 * 1024);");
  IncrementalMarking* marking = CcTest::heap()->incremental_marking();
  if (marking->IsStopped()) {
    CcTest::heap()->StartIncrementalMarking(
        i::GCFlag::kNoFlags, i::GarbageCollectionReason::kTesting);
  }
  heap::SimulateIncrementalMarking(CcTest::heap());
  CHECK(marking->IsMajorMarkingComplete());
}

TEST(DisableInlineAllocation) {
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  CompileRun(
      "function test() {"
      "  var x = [];"
      "  for (var i = 0; i < 10; i++) {"
      "    x[i] = [ {}, [1,2,3], [1,x,3] ];"
      "  }"
      "}"
      "function run() {"
      "  %PrepareFunctionForOptimization(test);"
      "  %OptimizeFunctionOnNextCall(test);"
      "  test();"
      "  %DeoptimizeFunction(test);"
      "}");

  // Warm-up with inline allocation enabled.
  CompileRun("test(); test(); run();");

  // Run test with inline allocation disabled.
  CcTest::heap()->DisableInlineAllocation();
  CompileRun("run()");

  // Run test with inline allocation re-enabled.
  CcTest::heap()->EnableInlineAllocation();
  CompileRun("run()");
}

static int AllocationSitesCount(Heap* heap) {
  int count = 0;
  for (Tagged<Object> site = heap->allocation_sites_list();
       IsAllocationSite(site);) {
    Tagged<AllocationSite> cur = Cast<AllocationSite>(site);
    CHECK(cur->HasWeakNext());
    site = cur->weak_next();
    count++;
  }
  return count;
}

static int SlimAllocationSiteCount(Heap* heap) {
  int count = 0;
  for (Tagged<Object> weak_list = heap->allocation_sites_list();
       IsAllocationSite(weak_list);) {
    Tagged<AllocationSite> weak_cur = Cast<AllocationSite>(weak_list);
    for (Tagged<Object> site = weak_cur->nested_site();
         IsAllocationSite(site);) {
      Tagged<AllocationSite> cur = Cast<AllocationSite>(site);
      CHECK(!cur->HasWeakNext());
      site = cur->nested_site();
      count++;
    }
    weak_list = weak_cur->weak_next();
  }
  return count;
}

TEST(EnsureAllocationSiteDependentCodesProcessed) {
  if (v8_flags.always_turbofan || !V8_ALLOCATION_SITE_TRACKING_BOOL) {
    return;
  }
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::internal::Heap* heap = CcTest::heap();
  GlobalHandles* global_handles = isolate->global_handles();

  if (!isolate->use_optimizer()) return;

  // The allocation site at the head of the list is ours.
  IndirectHandle<AllocationSite> site;
  {
    LocalContext context;
    v8::HandleScope scope(context->GetIsolate());

    int count = AllocationSitesCount(heap);
    CompileRun(
        "var bar = function() { return (new Array()); };"
        "%PrepareFunctionForOptimization(bar);"
        "var a = bar();"
        "bar();"
        "bar();");

    // One allocation site should have been created.
    int new_count = AllocationSitesCount(heap);
    CHECK_EQ(new_count, (count + 1));
    site = Cast<AllocationSite>(global_handles->Create(
        Cast<AllocationSite>(heap->allocation_sites_list())));

    CompileRun("%OptimizeFunctionOnNextCall(bar); bar();");

    IndirectHandle<JSFunction> bar_handle = Cast<JSFunction>(
        v8::Utils::OpenIndirectHandle(*v8::Local<v8::Function>::Cast(
            CcTest::global()
                ->Get(context.local(), v8_str("bar"))
                .ToLocalChecked())));

    // Expect a dependent code object for transitioning and pretenuring.
    Tagged<DependentCode> dependency = site->dependent_code();
    CHECK_NE(dependency,
             DependentCode::empty_dependent_code(ReadOnlyRoots(isolate)));
    CHECK_EQ(dependency->length(), DependentCode::kSlotsPerEntry);
    Tagged<MaybeObject> code =
        dependency->Get(0 + DependentCode::kCodeSlotOffset);
    CHECK(code.IsWeak());
    CHECK_EQ(bar_handle->code(isolate),
             Cast<CodeWrapper>(code.GetHeapObjectAssumeWeak())->code(isolate));
    Tagged<Smi> groups =
        dependency->Get(0 + DependentCode::kGroupsSlotOffset).ToSmi();
    CHECK_EQ(static_cast<DependentCode::DependencyGroups>(groups.value()),
             DependentCode::kAllocationSiteTransitionChangedGroup |
                 DependentCode::kAllocationSiteTenuringChangedGroup);
  }

  // Now make sure that a gc should get rid of the function, even though we
  // still have the allocation site alive.
  for (int i = 0; i < 4; i++) {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  // The site still exists because of our global handle, but the code is no
  // longer referred to by dependent_code().
  CHECK(site->dependent_code()->Get(0).IsCleared());
}

void CheckNumberOfAllocations(Heap* heap, const char* source,
                              int expected_full_alloc,
                              int expected_slim_alloc) {
  int prev_fat_alloc_count = AllocationSitesCount(heap);
  int prev_slim_alloc_count = SlimAllocationSiteCount(heap);

  CompileRun(source);

  int fat_alloc_sites = AllocationSitesCount(heap) - prev_fat_alloc_count;
  int slim_alloc_sites = SlimAllocationSiteCount(heap) - prev_slim_alloc_count;

  CHECK_EQ(expected_full_alloc, fat_alloc_sites);
  CHECK_EQ(expected_slim_alloc, slim_alloc_sites);
}

TEST(AllocationSiteCreation) {
  v8_flags.always_turbofan = false;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  HandleScope scope(isolate);
  i::v8_flags.allow_natives_syntax = true;

  // Array literals.
  CheckNumberOfAllocations(heap,
                           "function f1() {"
                           "  return []; "
                           "};"
                           "%EnsureFeedbackVectorForFunction(f1); f1();",
                           1, 0);
  CheckNumberOfAllocations(heap,
                           "function f2() {"
                           "  return [1, 2];"
                           "};"
                           "%EnsureFeedbackVectorForFunction(f2); f2();",
                           1, 0);
  CheckNumberOfAllocations(heap,
                           "function f3() {"
                           "  return [[1], [2]];"
                           "};"
                           "%EnsureFeedbackVectorForFunction(f3); f3();",
                           1, 2);
  CheckNumberOfAllocations(heap,
                           "function f4() { "
                           "return [0, [1, 1.1, 1.2, "
                           "], 1.5, [2.1, 2.2], 3];"
                           "};"
                           "%EnsureFeedbackVectorForFunction(f4); f4();",
                           1, 2);

  // Object literals have lazy AllocationSites
  CheckNumberOfAllocations(heap,
                           "function f5() {"
                           " return {};"
                           "};"
                           "%EnsureFeedbackVectorForFunction(f5); f5();",
                           0, 0);

  // No AllocationSites are created for the empty object literal.
  for (int i = 0; i < 5; i++) {
    CheckNumberOfAllocations(heap, "f5(); ", 0, 0);
  }

  CheckNumberOfAllocations(heap,
                           "function f6() {"
                           "  return {a:1};"
                           "};"
                           "%EnsureFeedbackVectorForFunction(f6); f6();",
                           0, 0);

  CheckNumberOfAllocations(heap, "f6(); ", 1, 0);

  CheckNumberOfAllocations(heap,
                           "function f7() {"
                           "  return {a:1, b:2};"
                           "};"
                           "%EnsureFeedbackVectorForFunction(f7); f7(); ",
                           0, 0);
  CheckNumberOfAllocations(heap, "f7(); ", 1, 0);

  // No Allocation sites are created for object subliterals
  CheckNumberOfAllocations(heap,
                           "function f8() {"
                           "return {a:{}, b:{ a:2, c:{ d:{f:{}}} } }; "
                           "};"
                           "%EnsureFeedbackVectorForFunction(f8); f8();",
                           0, 0);
  CheckNumberOfAllocations(heap, "f8(); ", 1, 0);

  // We currently eagerly create allocation sites if there are sub-arrays.
  // Allocation sites are created only for array subliterals
  CheckNumberOfAllocations(heap,
                           "function f9() {"
                           "return {a:[1, 2, 3], b:{ a:2, c:{ d:{f:[]} } }}; "
                           "};"
                           "%EnsureFeedbackVectorForFunction(f9); f9(); ",
                           1, 2);

  // No new AllocationSites created on the second invocation.
  CheckNumberOfAllocations(heap, "f9(); ", 0, 0);
}

TEST(CellsInOptimizedCodeAreWeak) {
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::internal::Heap* heap = CcTest::heap();

  if (!isolate->use_optimizer()) return;
  HandleScope outer_scope(heap->isolate());
  IndirectHandle<Code> code;
  {
    LocalContext context;
    HandleScope scope(heap->isolate());

    CompileRun(
        "bar = (function() {"
        "  function bar() {"
        "    return foo(1);"
        "  };"
        "  %PrepareFunctionForOptimization(bar);"
        "  var foo = function(x) { with (x) { return 1 + x; } };"
        "  %NeverOptimizeFunction(foo);"
        "  bar(foo);"
        "  bar(foo);"
        "  bar(foo);"
        "  %OptimizeFunctionOnNextCall(bar);"
        "  bar(foo);"
        "  return bar;})();");

    DirectHandle<JSFunction> bar = Cast<JSFunction>(v8::Utils::OpenDirectHandle(
        *v8::Local<v8::Function>::Cast(CcTest::global()
                                           ->Get(context.local(), v8_str("bar"))
                                           .ToLocalChecked())));
    code = handle(bar->code(isolate), isolate);
    code = scope.CloseAndEscape(code);
  }

  // Now make sure that a gc should get rid of the function
  for (int i = 0; i < 4; i++) {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  CHECK(code->marked_for_deoptimization());
  CHECK(code->embedded_objects_cleared());
}

TEST(ObjectsInOptimizedCodeAreWeak) {
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::internal::Heap* heap = CcTest::heap();

  if (!isolate->use_optimizer()) return;
  HandleScope outer_scope(heap->isolate());
  IndirectHandle<Code> code;
  {
    LocalContext context;
    HandleScope scope(heap->isolate());

    CompileRun(
        "function bar() {"
        "  return foo(1);"
        "};"
        "%PrepareFunctionForOptimization(bar);"
        "function foo(x) { with (x) { return 1 + x; } };"
        "%NeverOptimizeFunction(foo);"
        "bar();"
        "bar();"
        "bar();"
        "%OptimizeFunctionOnNextCall(bar);"
        "bar();");

    DirectHandle<JSFunction> bar = Cast<JSFunction>(v8::Utils::OpenDirectHandle(
        *v8::Local<v8::Function>::Cast(CcTest::global()
                                           ->Get(context.local(), v8_str("bar"))
                                           .ToLocalChecked())));
    code = handle(bar->code(isolate), isolate);
    code = scope.CloseAndEscape(code);
  }

  // Now make sure that a gc should get rid of the function
  for (int i = 0; i < 4; i++) {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  CHECK(code->marked_for_deoptimization());
  CHECK(code->embedded_objects_cleared());
}

TEST(NewSpaceObjectsInOptimizedCode) {
  if (v8_flags.always_turbofan || v8_flags.single_generation) return;
  v8_flags.allow_natives_syntax = true;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::internal::Heap* heap = CcTest::heap();

  if (!isolate->use_optimizer()) return;
  HandleScope outer_scope(isolate);
  IndirectHandle<Code> code;
  {
    LocalContext context;
    HandleScope scope(isolate);

    CompileRun(
        "var foo;"
        "var bar;"
        "(function() {"
        "  function foo_func(x) { with (x) { return 1 + x; } };"
        "  %NeverOptimizeFunction(foo_func);"
        "  function bar_func() {"
        "    return foo(1);"
        "  };"
        "  %PrepareFunctionForOptimization(bar_func);"
        "  bar = bar_func;"
        "  foo = foo_func;"
        "  bar_func();"
        "  bar_func();"
        "  bar_func();"
        "  %OptimizeFunctionOnNextCall(bar_func);"
        "  bar_func();"
        "})();");

    DirectHandle<JSFunction> bar = Cast<JSFunction>(v8::Utils::OpenDirectHandle(
        *v8::Local<v8::Function>::Cast(CcTest::global()
                                           ->Get(context.local(), v8_str("bar"))
                                           .ToLocalChecked())));

    DirectHandle<JSFunction> foo = Cast<JSFunction>(v8::Utils::OpenDirectHandle(
        *v8::Local<v8::Function>::Cast(CcTest::global()
                                           ->Get(context.local(), v8_str("foo"))
                                           .ToLocalChecked())));

    CHECK(Heap::InYoungGeneration(*foo));
    heap::InvokeMajorGC(heap);
    CHECK(!Heap::InYoungGeneration(*foo));
#ifdef VERIFY_HEAP
    HeapVerifier::VerifyHeap(CcTest::heap());
#endif
    CHECK(!bar->code(isolate)->marked_for_deoptimization());
    code = handle(bar->code(isolate), isolate);
    code = scope.CloseAndEscape(code);
  }

  // Now make sure that a gc should get rid of the function
  for (int i = 0; i < 4; i++) {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  CHECK(code->marked_for_deoptimization());
  CHECK(code->embedded_objects_cleared());
}

TEST(ObjectsInEagerlyDeoptimizedCodeAreWeak) {
  if (v8_flags.always_turbofan) return;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::internal::Heap* heap = CcTest::heap();

  if (!isolate->use_optimizer()) return;
  HandleScope outer_scope(heap->isolate());
  IndirectHandle<Code> code;
  {
    LocalContext context;
    HandleScope scope(heap->isolate());

    CompileRun(
        "function bar() {"
        "  return foo(1);"
        "};"
        "function foo(x) { with (x) { return 1 + x; } };"
        "%NeverOptimizeFunction(foo);"
        "%PrepareFunctionForOptimization(bar);"
        "bar();"
        "bar();"
        "bar();"
        "%OptimizeFunctionOnNextCall(bar);"
        "bar();"
        "%DeoptimizeFunction(bar);");

    DirectHandle<JSFunction> bar = Cast<JSFunction>(v8::Utils::OpenDirectHandle(
        *v8::Local<v8::Function>::Cast(CcTest::global()
                                           ->Get(context.local(), v8_str("bar"))
                                           .ToLocalChecked())));
    code = handle(bar->code(isolate), isolate);
    code = scope.CloseAndEscape(code);
  }

  CHECK(code->marked_for_deoptimization());

  // Now make sure that a gc should get rid of the function
  for (int i = 0; i < 4; i++) {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  CHECK(code->marked_for_deoptimization());
  CHECK(code->embedded_objects_cleared());
}

static Handle<InstructionStream> DummyOptimizedCode(Isolate* isolate) {
  uint8_t buffer[i::Assembler::kDefaultBufferSize];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  CodeDesc desc;
#if V8_TARGET_ARCH_ARM64
  UseScratchRegisterScope temps(&masm);
  Register tmp = temps.AcquireX();
  masm.Mov(tmp, Operand(isolate->factory()->undefined_value()));
  masm.Push(tmp, tmp);
#else
  masm.Push(isolate->factory()->undefined_value());
  masm.Push(isolate->factory()->undefined_value());
#endif
  masm.Drop(2);
  masm.GetCode(isolate, &desc);
  Handle<InstructionStream> code(
      Factory::CodeBuilder(isolate, desc, CodeKind::TURBOFAN)
          .set_self_reference(masm.CodeObject())
          .set_empty_source_position_table()
          .set_deoptimization_data(DeoptimizationData::Empty(isolate))
          .Build()
          ->instruction_stream(),
      isolate);
  CHECK(IsInstructionStream(*code));
  return code;
}

static bool weak_ic_cleared = false;

static void ClearWeakIC(
    const v8::WeakCallbackInfo<v8::Persistent<v8::Object>>& data) {
  printf("clear weak is called\n");
  weak_ic_cleared = true;
  data.GetParameter()->Reset();
}

TEST(WeakFunctionInConstructor) {
  if (v8_flags.always_turbofan) return;
  v8_flags.stress_compaction = false;
  v8_flags.stress_incremental_marking = false;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::internal::Heap* heap = CcTest::heap();
  LocalContext env;
  v8::HandleScope scope(isolate);

  CompileRun(
      "function createObj(obj) {"
      "  return new obj();"
      "}");
  i::IndirectHandle<JSFunction> createObj = Cast<JSFunction>(
      v8::Utils::OpenIndirectHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()
              ->Get(env.local(), v8_str("createObj"))
              .ToLocalChecked())));

  v8::Persistent<v8::Object> garbage;
  {
    v8::HandleScope new_scope(isolate);
    const char* source =
        " (function() {"
        "   function hat() { this.x = 5; }"
        "   %EnsureFeedbackVectorForFunction(hat);"
        "   %EnsureFeedbackVectorForFunction(createObj);"
        "   createObj(hat);"
        "   createObj(hat);"
        "   return hat;"
        " })();";
    garbage.Reset(isolate, CompileRun(env.local(), source)
                               .ToLocalChecked()
                               ->ToObject(env.local())
                               .ToLocalChecked());
  }
  weak_ic_cleared = false;
  garbage.SetWeak(&garbage, &ClearWeakIC, v8::WeakCallbackType::kParameter);
  {
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(weak_ic_cleared);

  // We've determined the constructor in createObj has had it's weak cell
  // cleared. Now, verify that one additional call with a new function
  // allows monomorphicity.
  IndirectHandle<FeedbackVector> feedback_vector(createObj->feedback_vector(),
                                                 CcTest::i_isolate());
  for (int i = 0; i < 20; i++) {
    Tagged<MaybeObject> slot_value = feedback_vector->Get(FeedbackSlot(0));
    CHECK(slot_value.IsWeakOrCleared());
    if (slot_value.IsCleared()) break;
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }

  Tagged<MaybeObject> slot_value = feedback_vector->Get(FeedbackSlot(0));
  CHECK(slot_value.IsCleared());
  CompileRun(
      "function coat() { this.x = 6; }"
      "createObj(coat);");
  slot_value = feedback_vector->Get(FeedbackSlot(0));
  CHECK(slot_value.IsWeak());
}

// Checks that the value returned by execution of the source is weak.
void CheckWeakness(const char* source) {
  v8_flags.stress_compaction = false;
  v8_flags.stress_incremental_marking = false;
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  Heap* heap = CcTest::heap();
  LocalContext env;
  v8::HandleScope scope(isolate);
  v8::Persistent<v8::Object> garbage;
  {
    v8::HandleScope new_scope(isolate);
    garbage.Reset(isolate, CompileRun(env.local(), source)
                               .ToLocalChecked()
                               ->ToObject(env.local())
                               .ToLocalChecked());
  }
  weak_ic_cleared = false;
  garbage.SetWeak(&garbage, &ClearWeakIC, v8::WeakCallbackType::kParameter);
  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(weak_ic_cleared);
}

// Each of the following "weak IC" tests creates an IC that embeds a map with
// the prototype pointing to _proto_ and checks that the _proto_ dies on GC.
TEST(WeakMapInMonomorphicLoadIC) {
  CheckWeakness(
      "function loadIC(obj) {"
      "  return obj.name;"
      "}"
      "%EnsureFeedbackVectorForFunction(loadIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   loadIC(obj);"
      "   loadIC(obj);"
      "   loadIC(obj);"
      "   return proto;"
      " })();");
}

TEST(WeakMapInPolymorphicLoadIC) {
  CheckWeakness(
      "function loadIC(obj) {"
      "  return obj.name;"
      "}"
      "%EnsureFeedbackVectorForFunction(loadIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   loadIC(obj);"
      "   loadIC(obj);"
      "   loadIC(obj);"
      "   var poly = Object.create(proto);"
      "   poly.x = true;"
      "   loadIC(poly);"
      "   return proto;"
      " })();");
}

TEST(WeakMapInMonomorphicKeyedLoadIC) {
  CheckWeakness(
      "function keyedLoadIC(obj, field) {"
      "  return obj[field];"
      "}"
      "%EnsureFeedbackVectorForFunction(keyedLoadIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   keyedLoadIC(obj, 'name');"
      "   keyedLoadIC(obj, 'name');"
      "   keyedLoadIC(obj, 'name');"
      "   return proto;"
      " })();");
}

TEST(WeakMapInPolymorphicKeyedLoadIC) {
  CheckWeakness(
      "function keyedLoadIC(obj, field) {"
      "  return obj[field];"
      "}"
      "%EnsureFeedbackVectorForFunction(keyedLoadIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   keyedLoadIC(obj, 'name');"
      "   keyedLoadIC(obj, 'name');"
      "   keyedLoadIC(obj, 'name');"
      "   var poly = Object.create(proto);"
      "   poly.x = true;"
      "   keyedLoadIC(poly, 'name');"
      "   return proto;"
      " })();");
}

TEST(WeakMapInMonomorphicStoreIC) {
  CheckWeakness(
      "function storeIC(obj, value) {"
      "  obj.name = value;"
      "}"
      "%EnsureFeedbackVectorForFunction(storeIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   storeIC(obj, 'x');"
      "   storeIC(obj, 'x');"
      "   storeIC(obj, 'x');"
      "   return proto;"
      " })();");
}

TEST(WeakMapInPolymorphicStoreIC) {
  CheckWeakness(
      "function storeIC(obj, value) {"
      "  obj.name = value;"
      "}"
      "%EnsureFeedbackVectorForFunction(storeIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   storeIC(obj, 'x');"
      "   storeIC(obj, 'x');"
      "   storeIC(obj, 'x');"
      "   var poly = Object.create(proto);"
      "   poly.x = true;"
      "   storeIC(poly, 'x');"
      "   return proto;"
      " })();");
}

TEST(WeakMapInMonomorphicKeyedStoreIC) {
  CheckWeakness(
      "function keyedStoreIC(obj, field, value) {"
      "  obj[field] = value;"
      "}"
      "%EnsureFeedbackVectorForFunction(keyedStoreIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   keyedStoreIC(obj, 'x');"
      "   keyedStoreIC(obj, 'x');"
      "   keyedStoreIC(obj, 'x');"
      "   return proto;"
      " })();");
}

TEST(WeakMapInPolymorphicKeyedStoreIC) {
  CheckWeakness(
      "function keyedStoreIC(obj, field, value) {"
      "  obj[field] = value;"
      "}"
      "%EnsureFeedbackVectorForFunction(keyedStoreIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   keyedStoreIC(obj, 'x');"
      "   keyedStoreIC(obj, 'x');"
      "   keyedStoreIC(obj, 'x');"
      "   var poly = Object.create(proto);"
      "   poly.x = true;"
      "   keyedStoreIC(poly, 'x');"
      "   return proto;"
      " })();");
}

TEST(WeakMapInMonomorphicCompareNilIC) {
  v8_flags.allow_natives_syntax = true;
  CheckWeakness(
      "function compareNilIC(obj) {"
      "  return obj == null;"
      "}"
      "%EnsureFeedbackVectorForFunction(compareNilIC);"
      " (function() {"
      "   var proto = {'name' : 'weak'};"
      "   var obj = Object.create(proto);"
      "   compareNilIC(obj);"
      "   compareNilIC(obj);"
      "   compareNilIC(obj);"
      "   return proto;"
      " })();");
}

Handle<JSFunction> GetFunctionByName(Isolate* isolate, const char* name) {
  Handle<String> str = isolate->factory()->InternalizeUtf8String(name);
  Handle<Object> obj =
      Object::GetProperty(isolate, isolate->global_object(), str)
          .ToHandleChecked();
  return Cast<JSFunction>(obj);
}

void CheckIC(DirectHandle<JSFunction> function, int slot_index,
             InlineCacheState state) {
  Tagged<FeedbackVector> vector = function->feedback_vector();
  FeedbackSlot slot(slot_index);
  FeedbackNexus nexus(CcTest::i_isolate(), vector, slot);
  CHECK_EQ(nexus.ic_state(), state);
}

TEST(MonomorphicStaysMonomorphicAfterGC) {
  if (!v8_flags.use_ic) return;
  if (v8_flags.always_turbofan) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());
  v8_flags.allow_natives_syntax = true;
  CompileRun(
      "function loadIC(obj) {"
      "  return obj.name;"
      "}"
      "%EnsureFeedbackVectorForFunction(loadIC);"
      "function testIC() {"
      "  var proto = {'name' : 'weak'};"
      "  var obj = Object.create(proto);"
      "  loadIC(obj);"
      "  loadIC(obj);"
      "  loadIC(obj);"
      "  return proto;"
      "};");
  DirectHandle<JSFunction> loadIC = GetFunctionByName(isolate, "loadIC");
  {
    v8::HandleScope new_scope(CcTest::isolate());
    CompileRun("(testIC())");
  }
  heap::InvokeMajorGC(CcTest::heap());
  CheckIC(loadIC, 0, InlineCacheState::MONOMORPHIC);
  {
    v8::HandleScope new_scope(CcTest::isolate());
    CompileRun("(testIC())");
  }
  CheckIC(loadIC, 0, InlineCacheState::MONOMORPHIC);
}

TEST(PolymorphicStaysPolymorphicAfterGC) {
  if (!v8_flags.use_ic) return;
  if (v8_flags.always_turbofan) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());
  v8_flags.allow_natives_syntax = true;
  CompileRun(
      "function loadIC(obj) {"
      "  return obj.name;"
      "}"
      "%EnsureFeedbackVectorForFunction(loadIC);"
      "function testIC() {"
      "  var proto = {'name' : 'weak'};"
      "  var obj = Object.create(proto);"
      "  loadIC(obj);"
      "  loadIC(obj);"
      "  loadIC(obj);"
      "  var poly = Object.create(proto);"
      "  poly.x = true;"
      "  loadIC(poly);"
      "  return proto;"
      "};");
  DirectHandle<JSFunction> loadIC = GetFunctionByName(isolate, "loadIC");
  {
    v8::HandleScope new_scope(CcTest::isolate());
    CompileRun("(testIC())");
  }
  heap::InvokeMajorGC(CcTest::heap());
  CheckIC(loadIC, 0, InlineCacheState::POLYMORPHIC);
  {
    v8::HandleScope new_scope(CcTest::isolate());
    CompileRun("(testIC())");
  }
  CheckIC(loadIC, 0, InlineCacheState::POLYMORPHIC);
}

#ifdef DEBUG
TEST(AddInstructionChangesNewSpacePromotion) {
  v8_flags.allow_natives_syntax = true;
  v8_flags.expose_gc = true;
  v8_flags.stress_compaction = true;
  HeapAllocator::SetAllocationGcInterval(1000);
  CcTest::InitializeVM();
  if (!v8_flags.allocation_site_pretenuring) return;
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  LocalContext env;
  CompileRun(
      "function add(a, b) {"
      "  return a + b;"
      "}"
      "add(1, 2);"
      "add(\"a\", \"b\");"
      "var oldSpaceObject;"
      "gc();"
      "function crash(x) {"
      "  var object = {a: null, b: null};"
      "  var result = add(1.5, x | 0);"
      "  object.a = result;"
      "  oldSpaceObject = object;"
      "  return object;"
      "}"
      "%PrepareFunctionForOptimization(crash);"
      "crash(1);"
      "crash(1);"
      "%OptimizeFunctionOnNextCall(crash);"
      "crash(1);");

  v8::Local<v8::Object> global = CcTest::global();
  v8::Local<v8::Function> g = v8::Local<v8::Function>::Cast(
      global->Get(env.local(), v8_str("crash")).ToLocalChecked());
  v8::Local<v8::Value> info1[] = {v8_num(1)};
  heap->DisableInlineAllocation();
  heap->set_allocation_timeout(1);
  g->Call(env.local(), global, 1, info1).ToLocalChecked();
  heap::InvokeMajorGC(heap);
}

void OnFatalErrorExpectOOM(const char* location, const char* message) {
  // Exit with 0 if the location matches our expectation.
  exit(strcmp(location, "CALL_AND_RETRY_LAST"));
}

TEST(CEntryStubOOM) {
  v8_flags.allow_natives_syntax = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  CcTest::isolate()->SetFatalErrorHandler(OnFatalErrorExpectOOM);

  v8::Local<v8::Value> result = CompileRun(
      "%SetAllocationTimeout(1, 1);"
      "var a = [];"
      "a.__proto__ = [];"
      "a.unshift(1)");

  CHECK(result->IsNumber());
}

#endif  // DEBUG

static void InterruptCallback357137(v8::Isolate* isolate, void* data) { }

static void RequestInterrupt(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CcTest::isolate()->RequestInterrupt(&InterruptCallback357137, nullptr);
}

HEAP_TEST(Regress538257) {
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  v8::Isolate::CreateParams create_params;
  // Set heap limits.
  create_params.constraints.set_max_young_generation_size_in_bytes(3 * MB);
#ifdef DEBUG
  create_params.constraints.set_max_old_generation_size_in_bytes(20 * MB);
#else
  create_params.constraints.set_max_old_generation_size_in_bytes(6 * MB);
#endif
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  isolate->Enter();
  {
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    Heap* heap = i_isolate->heap();
    HandleScope handle_scope(i_isolate);
    PagedSpace* old_space = heap->old_space();
    const int kMaxObjects = 10000;
    const int kFixedArrayLen = 512;
    Handle<FixedArray> objects[kMaxObjects];
    for (int i = 0; (i < kMaxObjects) &&
                    heap->CanExpandOldGeneration(old_space->AreaSize());
         i++) {
      objects[i] = i_isolate->factory()->NewFixedArray(kFixedArrayLen,
                                                       AllocationType::kOld);
      heap::ForceEvacuationCandidate(PageMetadata::FromHeapObject(*objects[i]));
    }
    heap::SimulateFullSpace(old_space);
    heap::InvokeMajorGC(heap);
    // If we get this far, we've successfully aborted compaction. Any further
    // allocations might trigger OOM.
  }
  isolate->Exit();
  isolate->Dispose();
}

TEST(Regress357137) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope hscope(isolate);
  v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
  global->Set(isolate, "interrupt",
              v8::FunctionTemplate::New(isolate, RequestInterrupt));
  v8::Local<v8::Context> context = v8::Context::New(isolate, nullptr, global);
  CHECK(!context.IsEmpty());
  v8::Context::Scope cscope(context);

  v8::Local<v8::Value> result = CompileRun(
      "var locals = '';"
      "for (var i = 0; i < 512; i++) locals += 'var v' + i + '= 42;';"
      "eval('function f() {' + locals + 'return function() { return v0; }; }');"
      "interrupt();"  // This triggers a fake stack overflow in f.
      "f()()");
  CHECK_EQ(42.0, result->ToNumber(context).ToLocalChecked()->Value());
}

TEST(Regress507979) {
  const int kFixedArrayLen = 10;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);

  DirectHandle<FixedArray> o1 =
      isolate->factory()->NewFixedArray(kFixedArrayLen);
  DirectHandle<FixedArray> o2 =
      isolate->factory()->NewFixedArray(kFixedArrayLen);
  CHECK(InCorrectGeneration(*o1));
  CHECK(InCorrectGeneration(*o2));

  HeapObjectIterator it(isolate->heap(),
                        i::HeapObjectIterator::kFilterUnreachable);

  // Replace parts of an object placed before a live object with a filler. This
  // way the filler object shares the mark bits with the following live object.
  o1->RightTrim(isolate, kFixedArrayLen - 1);

  for (Tagged<HeapObject> obj = it.Next(); !obj.is_null(); obj = it.Next()) {
    // Let's not optimize the loop away.
    CHECK_NE(obj.address(), kNullAddress);
  }
}

TEST(Regress388880) {
  if (!v8_flags.incremental_marking) return;
  v8_flags.stress_incremental_marking = false;
  v8_flags.expose_gc = true;
  v8_flags.stress_concurrent_allocation = false;  // For SimulateFullSpace.
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();

  Handle<Map> map1 = Map::Create(isolate, 1);
  Handle<String> name = factory->NewStringFromStaticChars("foo");
  name = factory->InternalizeString(name);
  DirectHandle<Map> map2 =
      Map::CopyWithField(isolate, map1, name, FieldType::Any(isolate), NONE,
                         PropertyConstness::kMutable, Representation::Tagged(),
                         OMIT_TRANSITION)
          .ToHandleChecked();

  size_t desired_offset = PageMetadata::kPageSize - map1->instance_size();

  // Allocate padding objects in old pointer space so, that object allocated
  // afterwards would end at the end of the page.
  heap::SimulateFullSpace(heap->old_space());
  size_t padding_size =
      desired_offset - MemoryChunkLayout::ObjectStartOffsetInDataPage();
  heap::CreatePadding(heap, static_cast<int>(padding_size),
                      AllocationType::kOld);

  DirectHandle<JSObject> o =
      factory->NewJSObjectFromMap(map1, AllocationType::kOld);
  o->set_raw_properties_or_hash(*factory->empty_fixed_array());

  // Ensure that the object allocated where we need it.
  PageMetadata* page = PageMetadata::FromHeapObject(*o);
  CHECK_EQ(desired_offset, page->Offset(o->address()));

  // Now we have an object right at the end of the page.

  // Enable incremental marking to trigger actions in Heap::AdjustLiveBytes()
  // that would cause crash.
  IncrementalMarking* marking = CcTest::heap()->incremental_marking();
  marking->Stop();
  CcTest::heap()->StartIncrementalMarking(i::GCFlag::kNoFlags,
                                          i::GarbageCollectionReason::kTesting);
  CHECK(marking->IsMarking());

  // Now everything is set up for crashing in JSObject::MigrateFastToFast()
  // when it calls heap->AdjustLiveBytes(...).
  JSObject::MigrateToMap(isolate, o, map2);
}

TEST(Regress3631) {
  if (!v8_flags.incremental_marking) return;
  v8_flags.expose_gc = true;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  IncrementalMarking* marking = CcTest::heap()->incremental_marking();
  v8::Local<v8::Value> result = CompileRun(
      "var weak_map = new WeakMap();"
      "var future_keys = [];"
      "for (var i = 0; i < 50; i++) {"
      "  var key = {'k' : i + 0.1};"
      "  weak_map.set(key, 1);"
      "  future_keys.push({'x' : i + 0.2});"
      "}"
      "weak_map");
  if (marking->IsStopped()) {
    CcTest::heap()->StartIncrementalMarking(
        i::GCFlag::kNoFlags, i::GarbageCollectionReason::kTesting);
  }
  // Incrementally mark the backing store.
  DirectHandle<JSReceiver> obj =
      v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(result));
  DirectHandle<JSWeakCollection> weak_map(Cast<JSWeakCollection>(*obj),
                                          isolate);
  SimulateIncrementalMarking(heap);
  // Stash the backing store in a handle.
  DirectHandle<Object> save(weak_map->table(), isolate);
  // The following line will update the backing store.
  CompileRun(
      "for (var i = 0; i < 50; i++) {"
      "  weak_map.set(future_keys[i], i);"
      "}");
  heap::InvokeMajorGC(heap);
}

TEST(Regress442710) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  HandleScope sc(isolate);
  Handle<JSGlobalObject> global(CcTest::i_isolate()->context()->global_object(),
                                isolate);
  Handle<JSArray> array = factory->NewJSArray(2);

  Handle<String> name = factory->InternalizeUtf8String("testArray");
  Object::SetProperty(isolate, global, name, array).Check();
  CompileRun("testArray[0] = 1; testArray[1] = 2; testArray.shift();");
  heap::InvokeMajorGC(CcTest::heap());
}

HEAP_TEST(NumberStringCacheSize) {
  // Test that the number-string cache has not been resized in the snapshot.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  if (!isolate->snapshot_available()) return;
  Heap* heap = isolate->heap();
  CHECK_EQ(Heap::kInitialNumberStringCacheSize * 2,
           heap->number_string_cache()->length());
}

TEST(Regress3877) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  HandleScope scope(isolate);
  CompileRun("function cls() { this.x = 10; }");
  DirectHandle<WeakFixedArray> weak_prototype_holder =
      factory->NewWeakFixedArray(1);
  {
    HandleScope inner_scope(isolate);
    v8::Local<v8::Value> result = CompileRun("cls.prototype");
    DirectHandle<JSReceiver> proto =
        v8::Utils::OpenDirectHandle(*v8::Local<v8::Object>::Cast(result));
    weak_prototype_holder->set(0, MakeWeak(*proto));
  }
  CHECK(!weak_prototype_holder->get(0).IsCleared());
  CompileRun(
      "var a = { };"
      "a.x = new cls();"
      "cls.prototype = null;");
  for (int i = 0; i < 4; i++) {
    heap::InvokeMajorGC(heap);
  }
  // The map of a.x keeps prototype alive
  CHECK(!weak_prototype_holder->get(0).IsCleared());
  // Detach the map (by promoting it to a prototype).
  CompileRun("var b = {}; b.__proto__ = a.x");
  // Change the map of a.x and make the previous map garbage collectable.
  CompileRun("a.x.__proto__ = {};");
  for (int i = 0; i < 4; i++) {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(weak_prototype_holder->get(0).IsCleared());
}

Handle<WeakFixedArray> AddRetainedMap(Isolate* isolate,
                                      DirectHandle<NativeContext> context) {
  HandleScope inner_scope(isolate);
  DirectHandle<Map> map = Map::Create(isolate, 1);
  v8::Local<v8::Value> result =
      CompileRun("(function () { return {x : 10}; })();");
  Handle<JSReceiver> proto =
      v8::Utils::OpenHandle(*v8::Local<v8::Object>::Cast(result));
  Map::SetPrototype(isolate, map, proto);
  GlobalHandleVector<Map> maps(isolate->heap());
  maps.Push(*map);
  isolate->heap()->AddRetainedMaps(context, std::move(maps));
  Handle<WeakFixedArray> array = isolate->factory()->NewWeakFixedArray(1);
  array->set(0, MakeWeak(*map));
  return inner_scope.CloseAndEscape(array);
}

void CheckMapRetainingFor(int n) {
  v8_flags.retain_maps_for_n_gc = n;
  v8::Isolate* isolate = CcTest::isolate();
  Isolate* i_isolate = CcTest::i_isolate();
  Heap* heap = i_isolate->heap();

  IndirectHandle<NativeContext> native_context;
  // This global is used to visit the object's constructor alive when starting
  // incremental marking. The native context keeps the constructor alive. The
  // constructor needs to be alive to retain the map.
  v8::Global<v8::Context> global_ctxt;

  {
    v8::Local<v8::Context> ctx = v8::Context::New(isolate);
    IndirectHandle<Context> context = Utils::OpenIndirectHandle(*ctx);
    CHECK(IsNativeContext(*context));
    native_context = Cast<NativeContext>(context);
    global_ctxt.Reset(isolate, ctx);
    ctx->Enter();
  }

  IndirectHandle<WeakFixedArray> array_with_map =
      AddRetainedMap(i_isolate, native_context);
  CHECK(array_with_map->get(0).IsWeak());
  for (int i = 0; i < n; i++) {
    heap::SimulateIncrementalMarking(heap);
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(array_with_map->get(0).IsWeak());
  {
    heap::SimulateIncrementalMarking(heap);
    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK(array_with_map->get(0).IsCleared());

  global_ctxt.Get(isolate)->Exit();
}

TEST(MapRetaining) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  CheckMapRetainingFor(v8_flags.retain_maps_for_n_gc);
  CheckMapRetainingFor(0);
  CheckMapRetainingFor(1);
  CheckMapRetainingFor(7);
}

TEST(RetainedMapsCleanup) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  v8::Local<v8::Context> ctx = v8::Context::New(CcTest::isolate());
  Handle<Context> context = Utils::OpenHandle(*ctx);
  CHECK(IsNativeContext(*context));
  DirectHandle<NativeContext> native_context = Cast<NativeContext>(context);

  ctx->Enter();
  DirectHandle<WeakFixedArray> array_with_map =
      AddRetainedMap(isolate, native_context);
  CHECK(array_with_map->get(0).IsWeak());
  heap->NotifyContextDisposed(true);
  heap::InvokeMajorGC(heap);
  ctx->Exit();

  CHECK_EQ(ReadOnlyRoots(heap).empty_weak_array_list(),
           native_context->retained_maps());
}

TEST(PreprocessStackTrace) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  v8::TryCatch try_catch(CcTest::isolate());
  CompileRun("throw new Error();");
  CHECK(try_catch.HasCaught());
  Isolate* isolate = CcTest::i_isolate();
  Handle<Object> exception = v8::Utils::OpenHandle(*try_catch.Exception());
  Handle<Name> key = isolate->factory()->error_stack_symbol();
  Handle<Object> stack_trace =
      Object::GetProperty(isolate, exception, key).ToHandleChecked();
  DirectHandle<Object> code =
      Object::GetElement(isolate, stack_trace, 3).ToHandleChecked();
  CHECK(IsInstructionStream(*code));

  heap::InvokeMemoryReducingMajorGCs(CcTest::heap());

  DirectHandle<Object> pos =
      Object::GetElement(isolate, stack_trace, 3).ToHandleChecked();
  CHECK(IsSmi(*pos));

  DirectHandle<FixedArray> frame_array = Cast<FixedArray>(stack_trace);
  int array_length = frame_array->length();
  for (int i = 0; i < array_length; i++) {
    DirectHandle<Object> element =
        Object::GetElement(isolate, stack_trace, i).ToHandleChecked();
    CHECK(!IsInstructionStream(*element));
  }
}

void AllocateInSpace(Isolate* isolate, size_t bytes, AllocationSpace space) {
  CHECK_LE(FixedArray::kHeaderSize, bytes);
  CHECK(IsAligned(bytes, kTaggedSize));
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);
  AlwaysAllocateScopeForTesting always_allocate(isolate->heap());
  int elements =
      static_cast<int>((bytes - FixedArray::kHeaderSize) / kTaggedSize);
  DirectHandle<FixedArray> array = factory->NewFixedArray(
      elements,
      space == NEW_SPACE ? AllocationType::kYoung : AllocationType::kOld);
  CHECK((space == NEW_SPACE) == Heap::InYoungGeneration(*array));
  CHECK_EQ(bytes, static_cast<size_t>(array->Size()));
}

TEST(NewSpaceAllocationCounter) {
  if (v8_flags.single_generation) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  heap->FreeMainThreadLinearAllocationAreas();
  size_t counter1 = heap->NewSpaceAllocationCounter();
  heap::EmptyNewSpaceUsingGC(heap);  // Ensure new space is empty.
  const size_t kSize = 1024;
  AllocateInSpace(isolate, kSize, NEW_SPACE);
  heap->FreeMainThreadLinearAllocationAreas();
  size_t counter2 = heap->NewSpaceAllocationCounter();
  CHECK_EQ(kSize, counter2 - counter1);
  heap::InvokeMinorGC(heap);
  size_t counter3 = heap->NewSpaceAllocationCounter();
  CHECK_EQ(0U, counter3 - counter2);
  // Test counter overflow.
  heap->FreeMainThreadLinearAllocationAreas();
  size_t max_counter = static_cast<size_t>(-1);
  heap->set_new_space_allocation_counter(max_counter - 10 * kSize);
  size_t start = heap->NewSpaceAllocationCounter();
  for (int i = 0; i < 20; i++) {
    AllocateInSpace(isolate, kSize, NEW_SPACE);
    heap->FreeMainThreadLinearAllocationAreas();
    size_t counter = heap->NewSpaceAllocationCounter();
    CHECK_EQ(kSize, counter - start);
    start = counter;
  }
}

TEST(OldSpaceAllocationCounter) {
  // Using the string forwarding table can free allocations during sweeping, due
  // to ThinString trimming, thus failing this test.
  // The flag (and handling of the forwarding table/ThinString transitions in
  // young gen) is only temporary so we just skip this test for now.
  if (v8_flags.always_use_string_forwarding_table) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  // Disable LAB, such that calculations with SizeOfObjects() and object size
  // are correct.
  heap->DisableInlineAllocation();
  heap::EmptyNewSpaceUsingGC(heap);
  size_t counter1 = heap->OldGenerationAllocationCounter();
  const size_t kSize = 1024;
  AllocateInSpace(isolate, kSize, OLD_SPACE);
  size_t counter2 = heap->OldGenerationAllocationCounter();
  // TODO(ulan): replace all CHECK_LE with CHECK_EQ after v8:4148 is fixed.
  CHECK_LE(kSize, counter2 - counter1);
  heap::InvokeMinorGC(heap);
  size_t counter3 = heap->OldGenerationAllocationCounter();
  CHECK_EQ(0u, counter3 - counter2);
  AllocateInSpace(isolate, kSize, OLD_SPACE);
  heap::InvokeMajorGC(heap);
  size_t counter4 = heap->OldGenerationAllocationCounter();
  CHECK_LE(kSize, counter4 - counter3);
  // Test counter overflow.
  size_t max_counter = static_cast<size_t>(-1);
  heap->set_old_generation_allocation_counter_at_last_gc(max_counter -
                                                         10 * kSize);
  size_t start = heap->OldGenerationAllocationCounter();
  for (int i = 0; i < 20; i++) {
    AllocateInSpace(isolate, kSize, OLD_SPACE);
    size_t counter = heap->OldGenerationAllocationCounter();
    CHECK_LE(kSize, counter - start);
    start = counter;
  }
}

static void CheckLeak(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  Isolate* isolate = CcTest::i_isolate();
  Tagged<Object> message(
      *reinterpret_cast<Address*>(isolate->pending_message_address()));
  CHECK(IsTheHole(message, isolate));
}

TEST(MessageObjectLeak) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
  global->Set(isolate, "check", v8::FunctionTemplate::New(isolate, CheckLeak));
  v8::Local<v8::Context> context = v8::Context::New(isolate, nullptr, global);
  v8::Context::Scope cscope(context);

  const char* test =
      "try {"
      "  throw 'message 1';"
      "} catch (e) {"
      "}"
      "check();"
      "L: try {"
      "  throw 'message 2';"
      "} finally {"
      "  break L;"
      "}"
      "check();";
  CompileRun(test);

  const char* flag = "--turbo-filter=*";
  FlagList::SetFlagsFromString(flag, strlen(flag));
  v8_flags.always_turbofan = true;

  CompileRun(test);
}

static void CheckEqualSharedFunctionInfos(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  Handle<Object> obj1 = v8::Utils::OpenHandle(*info[0]);
  Handle<Object> obj2 = v8::Utils::OpenHandle(*info[1]);
  DirectHandle<JSFunction> fun1 = Cast<JSFunction>(obj1);
  DirectHandle<JSFunction> fun2 = Cast<JSFunction>(obj2);
  CHECK(fun1->shared() == fun2->shared());
}

static void RemoveCodeAndGC(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  Isolate* isolate = CcTest::i_isolate();
  Handle<Object> obj = v8::Utils::OpenHandle(*info[0]);
  DirectHandle<JSFunction> fun = Cast<JSFunction>(obj);
  // Bytecode is code too.
  SharedFunctionInfo::DiscardCompiled(isolate, handle(fun->shared(), isolate));
  fun->set_code(*BUILTIN_CODE(isolate, CompileLazy));
  heap::InvokeMemoryReducingMajorGCs(CcTest::heap());
}

TEST(CanonicalSharedFunctionInfo) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
  global->Set(
      isolate, "check",
      v8::FunctionTemplate::New(isolate, CheckEqualSharedFunctionInfos));
  global->Set(isolate, "remove",
              v8::FunctionTemplate::New(isolate, RemoveCodeAndGC));
  v8::Local<v8::Context> context = v8::Context::New(isolate, nullptr, global);
  v8::Context::Scope cscope(context);
  CompileRun(
      "function f() { return function g() {}; }"
      "var g1 = f();"
      "remove(f);"
      "var g2 = f();"
      "check(g1, g2);");

  CompileRun(
      "function f() { return (function() { return function g() {}; })(); }"
      "var g1 = f();"
      "remove(f);"
      "var g2 = f();"
      "check(g1, g2);");
}

TEST(ScriptIterator) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  LocalContext context;

  heap::InvokeMajorGC(heap);

  int script_count = 0;
  {
    HeapObjectIterator it(heap);
    for (Tagged<HeapObject> obj = it.Next(); !obj.is_null(); obj = it.Next()) {
      if (IsScript(obj)) script_count++;
    }
  }

  {
    Script::Iterator iterator(isolate);
    for (Tagged<Script> script = iterator.Next(); !script.is_null();
         script = iterator.Next()) {
      script_count--;
    }
  }

  CHECK_EQ(0, script_count);
}

// This is the same as Factory::NewByteArray, except it doesn't retry on
// allocation failure.
AllocationResult HeapTester::AllocateByteArrayForTest(
    Heap* heap, int length, AllocationType allocation_type) {
  DCHECK(length >= 0 && length <= ByteArray::kMaxLength);
  int size = ByteArray::SizeFor(length);
  Tagged<HeapObject> result;
  {
    AllocationResult allocation = heap->AllocateRaw(size, allocation_type);
    if (!allocation.To(&result)) return allocation;
  }

  result->set_map_after_allocation(ReadOnlyRoots(heap).byte_array_map(),
                                   SKIP_WRITE_BARRIER);
  Cast<ByteArray>(result)->set_length(length);
  return AllocationResult::FromObject(result);
}

bool HeapTester::CodeEnsureLinearAllocationArea(Heap* heap, int size_in_bytes) {
  MainAllocator* allocator = heap->allocator()->code_space_allocator();
  return allocator->EnsureAllocationForTesting(
      size_in_bytes, AllocationAlignment::kTaggedAligned,
      AllocationOrigin::kRuntime);
}

HEAP_TEST(Regress587004) {
  if (v8_flags.single_generation) return;
  ManualGCScope manual_gc_scope;
#ifdef VERIFY_HEAP
  v8_flags.verify_heap = false;
#endif
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  const int N =
      (kMaxRegularHeapObjectSize - FixedArray::kHeaderSize) / kTaggedSize;
  DirectHandle<FixedArray> array =
      factory->NewFixedArray(N, AllocationType::kOld);
  CHECK(heap->old_space()->Contains(*array));
  DirectHandle<Object> number = factory->NewHeapNumber(1.0);
  CHECK(Heap::InYoungGeneration(*number));
  for (int i = 0; i < N; i++) {
    array->set(i, *number);
  }
  heap::InvokeMajorGC(heap);
  heap::SimulateFullSpace(heap->old_space());
  heap->RightTrimArray(*array, 1, N);
  heap->EnsureSweepingCompleted(Heap::SweepingForcedFinalizationMode::kV8Only);
  Tagged<ByteArray> byte_array;
  const int M = 256;
  // Don't allow old space expansion. The test works without this flag too,
  // but becomes very slow.
  heap->set_force_oom(true);
  while (
      AllocateByteArrayForTest(heap, M, AllocationType::kOld).To(&byte_array)) {
    for (int j = 0; j < M; j++) {
      byte_array->set(j, 0x31);
    }
  }
  // Re-enable old space expansion to avoid OOM crash.
  heap->set_force_oom(false);
  heap::InvokeMinorGC(heap);
}

HEAP_TEST(Regress589413) {
  if (!v8_flags.incremental_marking || v8_flags.stress_concurrent_allocation)
    return;
  v8_flags.stress_compaction = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  v8_flags.parallel_compaction = false;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  // Get the heap in clean state.
  heap::InvokeMajorGC(heap);
  heap::InvokeMajorGC(heap);
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  // Fill the new space with byte arrays with elements looking like pointers.
  const int M = 256;
  Tagged<ByteArray> byte_array;
  PageMetadata* young_page = nullptr;
  while (AllocateByteArrayForTest(heap, M, AllocationType::kYoung)
             .To(&byte_array)) {
    // Only allocate objects on one young page as a rough estimate on
    // how much memory can be promoted into the old generation.
    // Otherwise we would crash when forcing promotion of all young
    // live objects.
    if (!young_page) young_page = PageMetadata::FromHeapObject(byte_array);
    if (PageMetadata::FromHeapObject(byte_array) != young_page) break;

    for (int j = 0; j < M; j++) {
      byte_array->set(j, 0x31);
    }
    // Add the array in root set.
    handle(byte_array, isolate);
  }
  auto reset_oom = [](void* heap, size_t limit, size_t) -> size_t {
    reinterpret_cast<Heap*>(heap)->set_force_oom(false);
    return limit;
  };
  heap->AddNearHeapLimitCallback(reset_oom, heap);

  {
    // Ensure that incremental marking is not started unexpectedly.
    AlwaysAllocateScopeForTesting always_allocate(isolate->heap());

    // Make sure the byte arrays will be promoted on the next GC.
    heap::InvokeMinorGC(heap);
    // This number is close to large free list category threshold.
    const int N = 0x3EEE;

    std::vector<Tagged<FixedArray>> arrays;
    std::set<PageMetadata*> pages;
    Tagged<FixedArray> array;
    // Fill all pages with fixed arrays.
    heap->set_force_oom(true);
    while (
        AllocateFixedArrayForTest(heap, N, AllocationType::kOld).To(&array)) {
      arrays.push_back(array);
      pages.insert(PageMetadata::FromHeapObject(array));
      // Add the array in root set.
      handle(array, isolate);
    }
    heap->set_force_oom(false);
    size_t initial_pages = pages.size();
    // Expand and fill two pages with fixed array to ensure enough space both
    // the young objects and the evacuation candidate pages.
    while (
        AllocateFixedArrayForTest(heap, N, AllocationType::kOld).To(&array)) {
      arrays.push_back(array);
      pages.insert(PageMetadata::FromHeapObject(array));
      // Add the array in root set.
      handle(array, isolate);
      // Do not expand anymore.
      if (pages.size() - initial_pages == 2) {
        heap->set_force_oom(true);
      }
    }
    // Expand and mark the new page as evacuation candidate.
    heap->set_force_oom(false);
    {
      DirectHandle<HeapObject> ec_obj =
          factory->NewFixedArray(5000, AllocationType::kOld);
      PageMetadata* ec_page = PageMetadata::FromHeapObject(*ec_obj);
      heap::ForceEvacuationCandidate(ec_page);
      // Make all arrays point to evacuation candidate so that
      // slots are recorded for them.
      for (size_t j = 0; j < arrays.size(); j++) {
        array = arrays[j];
        for (int i = 0; i < N; i++) {
          array->set(i, *ec_obj);
        }
      }
    }
    CHECK(heap->incremental_marking()->IsStopped());
    heap::SimulateIncrementalMarking(heap);
    for (size_t j = 0; j < arrays.size(); j++) {
      heap->RightTrimArray(arrays[j], 1, N);
    }
  }

  // Force allocation from the free list.
  heap->set_force_oom(true);
  heap::InvokeMajorGC(heap);
  heap->RemoveNearHeapLimitCallback(reset_oom, 0);
}

TEST(Regress598319) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  // This test ensures that no white objects can cross the progress bar of large
  // objects during incremental marking. It checks this by using Shift() during
  // incremental marking.
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();

  // The size of the array should be larger than kProgressBarScanningChunk.
  const int kNumberOfObjects =
      std::max(FixedArray::kMaxRegularLength + 1, 128 * KB);

  struct Arr {
    Arr(Isolate* isolate, int number_of_objects) {
      root = isolate->factory()->NewFixedArray(1, AllocationType::kOld);
      {
        // Temporary scope to avoid getting any other objects into the root set.
        v8::HandleScope new_scope(CcTest::isolate());
        DirectHandle<FixedArray> tmp = isolate->factory()->NewFixedArray(
            number_of_objects, AllocationType::kOld);
        root->set(0, *tmp);
        for (int i = 0; i < get()->length(); i++) {
          tmp = isolate->factory()->NewFixedArray(100, AllocationType::kOld);
          get()->set(i, *tmp);
        }
      }
      global_root.Reset(CcTest::isolate(), Utils::ToLocal(Cast<Object>(root)));
    }

    Tagged<FixedArray> get() { return Cast<FixedArray>(root->get(0)); }

    Handle<FixedArray> root;

    // Store array in global as well to make it part of the root set when
    // starting incremental marking.
    v8::Global<Value> global_root;
  } arr(isolate, kNumberOfObjects);

  CHECK_EQ(arr.get()->length(), kNumberOfObjects);
  CHECK(heap->lo_space()->Contains(arr.get()));
  LargePageMetadata* page = LargePageMetadata::FromHeapObject(arr.get());
  CHECK_NOT_NULL(page);

  // GC to cleanup state
  heap::InvokeMajorGC(heap);
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }

  CHECK(heap->lo_space()->Contains(arr.get()));
  IncrementalMarking* marking = heap->incremental_marking();
  MarkingState* marking_state = heap->marking_state();
  CHECK(marking_state->IsUnmarked(arr.get()));
  for (int i = 0; i < arr.get()->length(); i++) {
    Tagged<HeapObject> arr_value = Cast<HeapObject>(arr.get()->get(i));
    CHECK(marking_state->IsUnmarked(arr_value));
  }

  // Start incremental marking.
  CHECK(marking->IsMarking() || marking->IsStopped());
  if (marking->IsStopped()) {
    heap->StartIncrementalMarking(i::GCFlag::kNoFlags,
                                  i::GarbageCollectionReason::kTesting);
  }
  CHECK(marking->IsMarking());

  // Check that we have not marked the interesting array during root scanning.
  for (int i = 0; i < arr.get()->length(); i++) {
    Tagged<HeapObject> arr_value = Cast<HeapObject>(arr.get()->get(i));
    CHECK(marking_state->IsUnmarked(arr_value));
  }

  // Now we search for a state where we are in incremental marking and have
  // only partially marked the large object.
  static constexpr auto kSmallStepSize =
      v8::base::TimeDelta::FromMillisecondsD(0.1);
  static constexpr size_t kSmallMaxBytesToMark = 100;
  while (!marking->IsMajorMarkingComplete()) {
    marking->AdvanceForTesting(kSmallStepSize, kSmallMaxBytesToMark);
    ProgressBar& progress_bar = page->ProgressBar();
    if (progress_bar.IsEnabled() && progress_bar.Value() > 0) {
      CHECK_NE(progress_bar.Value(), arr.get()->Size());
      {
        // Shift by 1, effectively moving one white object across the progress
        // bar, meaning that we will miss marking it.
        v8::HandleScope new_scope(CcTest::isolate());
        Handle<JSArray> js_array = isolate->factory()->NewJSArrayWithElements(
            Handle<FixedArray>(arr.get(), isolate));
        js_array->GetElementsAccessor()->Shift(js_array).Check();
      }
      break;
    }
  }

  IsolateSafepointScope safepoint_scope(heap);
  MarkingBarrier::PublishAll(heap);

  // Finish marking with bigger steps to speed up test.
  static constexpr auto kLargeStepSize =
      v8::base::TimeDelta::FromMilliseconds(1000);
  while (!marking->IsMajorMarkingComplete()) {
    marking->AdvanceForTesting(kLargeStepSize);
  }
  CHECK(marking->IsMajorMarkingComplete());

  // All objects need to be black after marking. If a white object crossed the
  // progress bar, we would fail here.
  for (int i = 0; i < arr.get()->length(); i++) {
    Tagged<HeapObject> arr_value = Cast<HeapObject>(arr.get()->get(i));
    CHECK(InReadOnlySpace(arr_value) || marking_state->IsMarked(arr_value));
  }
}

Handle<FixedArray> ShrinkArrayAndCheckSize(Heap* heap, int length) {
  // Make sure there is no garbage and the compilation cache is empty.
  for (int i = 0; i < 5; i++) {
    heap::InvokeMajorGC(heap);
  }
  heap->EnsureSweepingCompleted(Heap::SweepingForcedFinalizationMode::kV8Only);
  // Disable LAB, such that calculations with SizeOfObjects() and object size
  // are correct.
  heap->DisableInlineAllocation();
  size_t size_before_allocation = heap->SizeOfObjects();
  Handle<FixedArray> array =
      heap->isolate()->factory()->NewFixedArray(length, AllocationType::kOld);
  size_t size_after_allocation = heap->SizeOfObjects();
  CHECK_EQ(size_after_allocation, size_before_allocation + array->Size());
  array->RightTrim(heap->isolate(), 1);
  size_t size_after_shrinking = heap->SizeOfObjects();
  // Shrinking does not change the space size immediately.
  CHECK_EQ(size_after_allocation, size_after_shrinking);
  // GC and sweeping updates the size to acccount for shrinking.
  heap::InvokeMajorGC(heap);
  heap->EnsureSweepingCompleted(Heap::SweepingForcedFinalizationMode::kV8Only);
  intptr_t size_after_gc = heap->SizeOfObjects();
  CHECK_EQ(size_after_gc, size_before_allocation + array->Size());
  return array;
}

TEST(Regress609761) {
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  int length = kMaxRegularHeapObjectSize / kTaggedSize + 1;
  DirectHandle<FixedArray> array = ShrinkArrayAndCheckSize(heap, length);
  CHECK(heap->lo_space()->Contains(*array));
}

TEST(LiveBytes) {
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  DirectHandle<FixedArray> array = ShrinkArrayAndCheckSize(heap, 2000);
  CHECK(heap->old_space()->Contains(*array));
}

TEST(Regress615489) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();
  heap::InvokeMajorGC(heap);

  i::IncrementalMarking* marking = heap->incremental_marking();
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }
  CHECK(marking->IsMarking() || marking->IsStopped());
  if (marking->IsStopped()) {
    heap->StartIncrementalMarking(i::GCFlag::kNoFlags,
                                  i::GarbageCollectionReason::kTesting);
  }
  CHECK(marking->IsMarking());
  CHECK(marking->black_allocation());
  {
    AlwaysAllocateScopeForTesting always_allocate(heap);
    v8::HandleScope inner(CcTest::isolate());
    isolate->factory()->NewFixedArray(500, AllocationType::kOld)->Size();
  }
  static constexpr auto kStepSize = v8::base::TimeDelta::FromMilliseconds(100);
  while (!marking->IsMajorMarkingComplete()) {
    marking->AdvanceForTesting(kStepSize);
  }
  CHECK(marking->IsMajorMarkingComplete());
  intptr_t size_before = heap->SizeOfObjects();
  heap::InvokeMajorGC(heap);
  intptr_t size_after = heap->SizeOfObjects();
  // Live size does not increase after garbage collection.
  CHECK_LE(size_after, size_before);
}

class StaticOneByteResource : public v8::String::ExternalOneByteStringResource {
 public:
  explicit StaticOneByteResource(const char* data) : data_(data) {}

  ~StaticOneByteResource() override = default;

  const char* data() const override { return data_; }

  size_t length() const override { return strlen(data_); }

 private:
  const char* data_;
};

TEST(Regress631969) {
  if (!v8_flags.incremental_marking || v8_flags.separate_gc_phases) return;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  v8_flags.parallel_compaction = false;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  // Get the heap in clean state.
  heap::InvokeMajorGC(heap);
  heap::InvokeMajorGC(heap);
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  // Allocate two strings in a fresh page and mark the page as evacuation
  // candidate.
  heap::SimulateFullSpace(heap->old_space());
  Handle<String> s1 =
      factory->NewStringFromStaticChars("123456789", AllocationType::kOld);
  Handle<String> s2 =
      factory->NewStringFromStaticChars("01234", AllocationType::kOld);
  heap::ForceEvacuationCandidate(PageMetadata::FromHeapObject(*s1));

  heap::SimulateIncrementalMarking(heap, false);

  // Allocate a cons string and promote it to a fresh page in the old space.
  Handle<String> s3 = factory->NewConsString(s1, s2).ToHandleChecked();
  heap::EmptyNewSpaceUsingGC(heap);

  heap::SimulateIncrementalMarking(heap, false);

  // Finish incremental marking.
  static constexpr auto kStepSize = v8::base::TimeDelta::FromMilliseconds(100);
  IncrementalMarking* marking = heap->incremental_marking();
  while (!marking->IsMajorMarkingComplete()) {
    marking->AdvanceForTesting(kStepSize);
  }

  {
    StaticOneByteResource external_string("12345678901234");
    s3->MakeExternal(&external_string);
    heap::InvokeMajorGC(heap);
    // This avoids the GC from trying to free stack allocated resources.
    i::Cast<i::ExternalOneByteString>(s3)->SetResource(isolate, nullptr);
  }
}

TEST(ContinuousRightTrimFixedArrayInBlackArea) {
  if (!v8_flags.incremental_marking) return;
  v8_flags.stress_concurrent_allocation = false;  // For SimulateFullSpace.
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = CcTest::i_isolate();
  heap::InvokeMajorGC(heap);

  i::IncrementalMarking* marking = heap->incremental_marking();
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }
  CHECK(marking->IsMarking() || marking->IsStopped());
  if (marking->IsStopped()) {
    heap->StartIncrementalMarking(i::GCFlag::kNoFlags,
                                  i::GarbageCollectionReason::kTesting);
  }
  CHECK(marking->IsMarking());
  CHECK(marking->black_allocation());

  // Ensure that we allocate a new page, set up a bump pointer area, and
  // perform the allocation in a black area.
  heap::SimulateFullSpace(heap->old_space());
  isolate->factory()->NewFixedArray(10, AllocationType::kOld);

  // Allocate the fixed array that will be trimmed later.
  DirectHandle<FixedArray> array =
      isolate->factory()->NewFixedArray(100, AllocationType::kOld);
  Address start_address = array->address();
  Address end_address = start_address + array->Size();
  PageMetadata* page = PageMetadata::FromAddress(start_address);
  NonAtomicMarkingState* marking_state = heap->non_atomic_marking_state();
  CHECK(marking_state->IsMarked(*array));
  CHECK(page->marking_bitmap()->AllBitsSetInRange(
      MarkingBitmap::AddressToIndex(start_address),
      MarkingBitmap::LimitAddressToIndex(end_address)));
  CHECK(heap->old_space()->Contains(*array));

  // Trim it once by one word to make checking for white marking color uniform.
  Address previous = end_address - kTaggedSize;
  isolate->heap()->RightTrimArray(*array, 99, 100);

  Tagged<HeapObject> filler = HeapObject::FromAddress(previous);
  CHECK(IsFreeSpaceOrFiller(filler));

  // Trim 10 times by one, two, and three word.
  for (int i = 1; i <= 3; i++) {
    for (int j = 0; j < 10; j++) {
      previous -= kTaggedSize * i;
      int old_capacity = array->capacity();
      int new_capacity = old_capacity - i;
      isolate->heap()->RightTrimArray(*array, new_capacity, old_capacity);
      filler = HeapObject::FromAddress(previous);
      CHECK(IsFreeSpaceOrFiller(filler));
      CHECK(marking_state->IsUnmarked(filler));
    }
  }

  heap::InvokeAtomicMajorGC(heap);
}

TEST(Regress618958) {
  if (!v8_flags.incremental_marking) return;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  bool isolate_is_locked = true;
  CcTest::isolate()->AdjustAmountOfExternalAllocatedMemory(100 * MB);
  int mark_sweep_count_before = heap->ms_count();
  heap->MemoryPressureNotification(MemoryPressureLevel::kCritical,
                                   isolate_is_locked);
  int mark_sweep_count_after = heap->ms_count();
  int mark_sweeps_performed = mark_sweep_count_after - mark_sweep_count_before;
  // The memory pressuer handler either performed two GCs or performed one and
  // started incremental marking.
  CHECK(mark_sweeps_performed == 2 ||
        (mark_sweeps_performed == 1 &&
         !heap->incremental_marking()->IsStopped()));
}

TEST(YoungGenerationLargeObjectAllocationScavenge) {
  if (v8_flags.minor_ms) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();
  if (!isolate->serializer_enabled()) return;

  // TODO(hpayer): Update the test as soon as we have a tenure limit for LO.
  DirectHandle<FixedArray> array_small =
      isolate->factory()->NewFixedArray(200000);
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(*array_small);
  CHECK_EQ(NEW_LO_SPACE,
           MutablePageMetadata::cast(chunk->Metadata())->owner_identity());
  CHECK(chunk->IsFlagSet(MemoryChunk::LARGE_PAGE));
  CHECK(chunk->IsFlagSet(MemoryChunk::TO_PAGE));

  DirectHandle<Object> number = isolate->factory()->NewHeapNumber(123.456);
  array_small->set(0, *number);

  heap::InvokeMinorGC(heap);

  // After the first young generation GC array_small will be in the old
  // generation large object space.
  chunk = MemoryChunk::FromHeapObject(*array_small);
  CHECK_EQ(LO_SPACE,
           MutablePageMetadata::cast(chunk->Metadata())->owner_identity());
  CHECK(!chunk->InYoungGeneration());

  heap::InvokeMemoryReducingMajorGCs(heap);
}

TEST(YoungGenerationLargeObjectAllocationMarkCompact) {
  if (v8_flags.minor_ms) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();
  if (!isolate->serializer_enabled()) return;

  // TODO(hpayer): Update the test as soon as we have a tenure limit for LO.
  DirectHandle<FixedArray> array_small =
      isolate->factory()->NewFixedArray(200000);
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(*array_small);
  CHECK_EQ(NEW_LO_SPACE,
           MutablePageMetadata::cast(chunk->Metadata())->owner_identity());
  CHECK(chunk->IsFlagSet(MemoryChunk::LARGE_PAGE));
  CHECK(chunk->IsFlagSet(MemoryChunk::TO_PAGE));

  DirectHandle<Object> number = isolate->factory()->NewHeapNumber(123.456);
  array_small->set(0, *number);

  heap::InvokeMajorGC(heap);

  // After the first full GC array_small will be in the old generation
  // large object space.
  chunk = MemoryChunk::FromHeapObject(*array_small);
  CHECK_EQ(LO_SPACE,
           MutablePageMetadata::cast(chunk->Metadata())->owner_identity());
  CHECK(!chunk->InYoungGeneration());

  heap::InvokeMemoryReducingMajorGCs(heap);
}

TEST(YoungGenerationLargeObjectAllocationReleaseScavenger) {
  if (v8_flags.minor_ms) return;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();
  if (!isolate->serializer_enabled()) return;

  {
    HandleScope new_scope(isolate);
    for (int i = 0; i < 10; i++) {
      DirectHandle<FixedArray> array_small =
          isolate->factory()->NewFixedArray(20000);
      MemoryChunk* chunk = MemoryChunk::FromHeapObject(*array_small);
      CHECK_EQ(NEW_LO_SPACE,
               MutablePageMetadata::cast(chunk->Metadata())->owner_identity());
      CHECK(chunk->IsFlagSet(MemoryChunk::TO_PAGE));
    }
  }

  heap::InvokeMinorGC(heap);
  CHECK(isolate->heap()->new_lo_space()->IsEmpty());
  CHECK_EQ(0, isolate->heap()->new_lo_space()->Size());
  CHECK_EQ(0, isolate->heap()->new_lo_space()->SizeOfObjects());
  CHECK(isolate->heap()->lo_space()->IsEmpty());
  CHECK_EQ(0, isolate->heap()->lo_space()->Size());
  CHECK_EQ(0, isolate->heap()->lo_space()->SizeOfObjects());
}

TEST(UncommitUnusedLargeObjectMemory) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();

  DirectHandle<FixedArray> array =
      isolate->factory()->NewFixedArray(200000, AllocationType::kOld);
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(*array);
  CHECK_IMPLIES(
      !v8_flags.enable_third_party_heap,
      MutablePageMetadata::cast(chunk->Metadata())->owner_identity() ==
          LO_SPACE);

  intptr_t size_before = array->Size();
  size_t committed_memory_before =
      MutablePageMetadata::cast(chunk->Metadata())->CommittedPhysicalMemory();

  array->RightTrim(isolate, 1);
  CHECK(array->Size() < size_before);

  heap::InvokeMajorGC(heap);
  CHECK(
      MutablePageMetadata::cast(chunk->Metadata())->CommittedPhysicalMemory() <
      committed_memory_before);
  size_t shrinked_size = RoundUp(
      (array->address() - chunk->address()) + array->Size(), CommitPageSize());
  CHECK_EQ(
      shrinked_size,
      MutablePageMetadata::cast(chunk->Metadata())->CommittedPhysicalMemory());
}

template <RememberedSetType direction>
static size_t GetRememberedSetSize(Tagged<HeapObject> obj) {
  size_t count = 0;
  auto chunk = MutablePageMetadata::FromHeapObject(obj);
  RememberedSet<direction>::Iterate(
      chunk,
      [&count](MaybeObjectSlot slot) {
        count++;
        return KEEP_SLOT;
      },
      SlotSet::KEEP_EMPTY_BUCKETS);
  return count;
}

TEST(RememberedSet_InsertOnWriteBarrier) {
  if (v8_flags.single_generation) return;
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  heap::SealCurrentObjects(heap);
  HandleScope scope(isolate);

  // Allocate an object in old space.
  DirectHandle<FixedArray> arr =
      factory->NewFixedArray(3, AllocationType::kOld);

  // Add into 'arr' references to young objects.
  {
    HandleScope scope_inner(isolate);
    DirectHandle<Object> number = factory->NewHeapNumber(42);
    arr->set(0, *number);
    arr->set(1, *number);
    arr->set(2, *number);
    DirectHandle<Object> number_other = factory->NewHeapNumber(24);
    arr->set(2, *number_other);
  }
  // Remembered sets track *slots* pages with cross-generational pointers, so
  // must have recorded three of them each exactly once.
  CHECK_EQ(3, GetRememberedSetSize<OLD_TO_NEW>(*arr));
}

TEST(RememberedSet_InsertInLargePage) {
  if (v8_flags.single_generation) return;
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  heap::SealCurrentObjects(heap);
  HandleScope scope(isolate);

  // Allocate an object in Large space.
  const int count = std::max(FixedArray::kMaxRegularLength + 1, 128 * KB);
  DirectHandle<FixedArray> arr =
      factory->NewFixedArray(count, AllocationType::kOld);
  CHECK(heap->lo_space()->Contains(*arr));
  CHECK_EQ(0, GetRememberedSetSize<OLD_TO_NEW>(*arr));

  // Create OLD_TO_NEW references from the large object so that the
  // corresponding slots end up in different SlotSets.
  {
    HandleScope short_lived(isolate);
    DirectHandle<Object> number = factory->NewHeapNumber(42);
    arr->set(0, *number);
    arr->set(count - 1, *number);
  }
  CHECK_EQ(2, GetRememberedSetSize<OLD_TO_NEW>(*arr));
}

TEST(RememberedSet_RemoveStaleOnScavenge) {
  if (v8_flags.single_generation || v8_flags.stress_incremental_marking) return;
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  heap::SealCurrentObjects(heap);
  HandleScope scope(isolate);

  // Allocate an object in old space and add into it references to young.
  DirectHandle<FixedArray> arr =
      factory->NewFixedArray(3, AllocationType::kOld);
  {
    HandleScope scope_inner(isolate);
    DirectHandle<Object> number = factory->NewHeapNumber(42);
    arr->set(0, *number);  // will be trimmed away
    arr->set(1, *number);  // will be replaced with #undefined
    arr->set(2, *number);  // will be promoted into old
  }
  CHECK_EQ(3, GetRememberedSetSize<OLD_TO_NEW>(*arr));

  arr->set(1, ReadOnlyRoots(CcTest::heap()).undefined_value());
  DirectHandle<FixedArrayBase> tail(heap->LeftTrimFixedArray(*arr, 1), isolate);

  // None of the actions above should have updated the remembered set.
  CHECK_EQ(3, GetRememberedSetSize<OLD_TO_NEW>(*tail));

  // Run GC to promote the remaining young object and fixup the stale entries in
  // the remembered set.
  heap::EmptyNewSpaceUsingGC(heap);
  CHECK_EQ(0, GetRememberedSetSize<OLD_TO_NEW>(*tail));
}

// The OLD_TO_OLD remembered set is created temporary by GC and is cleared at
// the end of the pass. There is no way to observe it so the test only checks
// that compaction has happened and otherwise relies on code's self-validation.
TEST(RememberedSet_OldToOld) {
  if (v8_flags.stress_incremental_marking) return;
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  Heap* heap = isolate->heap();
  heap::SealCurrentObjects(heap);
  HandleScope scope(isolate);

  IndirectHandle<FixedArray> arr =
      factory->NewFixedArray(10, AllocationType::kOld);
  {
    HandleScope short_lived(isolate);
    factory->NewFixedArray(100, AllocationType::kOld);
  }
  IndirectHandle<Object> ref =
      factory->NewFixedArray(100, AllocationType::kOld);
  arr->set(0, *ref);

  // To force compaction of the old space, fill it with garbage and start a new
  // page (so that the page with 'arr' becomes subject to compaction).
  {
    HandleScope short_lived(isolate);
    heap::SimulateFullSpace(heap->old_space());
    factory->NewFixedArray(100, AllocationType::kOld);
  }

  heap::ForceEvacuationCandidate(PageMetadata::FromHeapObject(*arr));
  const auto prev_location = *arr;

  {
    // This GC pass will evacuate the page with 'arr'/'ref' so it will have to
    // create OLD_TO_OLD remembered set to track the reference.
    // We need to invoke GC without stack, otherwise no compaction is performed.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
    heap::InvokeMajorGC(heap);
  }
  CHECK_NE(prev_location.ptr(), arr->ptr());
}

TEST(RememberedSetRemoveRange) {
  if (v8_flags.single_generation) return;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();

  DirectHandle<FixedArray> array = isolate->factory()->NewFixedArray(
      PageMetadata::kPageSize / kTaggedSize, AllocationType::kOld);
  MutablePageMetadata* chunk = MutablePageMetadata::FromHeapObject(*array);
  CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                chunk->owner_identity() == LO_SPACE);
  Address start = array->address();
  // Maps slot to boolean indicator of whether the slot should be in the set.
  std::map<Address, bool> slots;
  slots[start + 0] = true;
  slots[start + kTaggedSize] = true;
  slots[start + PageMetadata::kPageSize - kTaggedSize] = true;
  slots[start + PageMetadata::kPageSize] = true;
  slots[start + PageMetadata::kPageSize + kTaggedSize] = true;
  slots[chunk->area_end() - kTaggedSize] = true;

  for (auto x : slots) {
    RememberedSet<OLD_TO_NEW>::Insert<AccessMode::ATOMIC>(
        chunk, chunk->Offset(x.first));
  }

  RememberedSet<OLD_TO_NEW>::Iterate(
      chunk,
      [&slots](MaybeObjectSlot slot) {
        CHECK(slots[slot.address()]);
        return KEEP_SLOT;
      },
      SlotSet::FREE_EMPTY_BUCKETS);

  RememberedSet<OLD_TO_NEW>::RemoveRange(chunk, start, start + kTaggedSize,
                                         SlotSet::FREE_EMPTY_BUCKETS);
  slots[start] = false;
  RememberedSet<OLD_TO_NEW>::Iterate(
      chunk,
      [&slots](MaybeObjectSlot slot) {
        CHECK(slots[slot.address()]);
        return KEEP_SLOT;
      },
      SlotSet::FREE_EMPTY_BUCKETS);

  RememberedSet<OLD_TO_NEW>::RemoveRange(chunk, start + kTaggedSize,
                                         start + PageMetadata::kPageSize,
                                         SlotSet::FREE_EMPTY_BUCKETS);
  slots[start + kTaggedSize] = false;
  slots[start + PageMetadata::kPageSize - kTaggedSize] = false;
  RememberedSet<OLD_TO_NEW>::Iterate(
      chunk,
      [&slots](MaybeObjectSlot slot) {
        CHECK(slots[slot.address()]);
        return KEEP_SLOT;
      },
      SlotSet::FREE_EMPTY_BUCKETS);

  RememberedSet<OLD_TO_NEW>::RemoveRange(
      chunk, start, start + PageMetadata::kPageSize + kTaggedSize,
      SlotSet::FREE_EMPTY_BUCKETS);
  slots[start + PageMetadata::kPageSize] = false;
  RememberedSet<OLD_TO_NEW>::Iterate(
      chunk,
      [&slots](MaybeObjectSlot slot) {
        CHECK(slots[slot.address()]);
        return KEEP_SLOT;
      },
      SlotSet::FREE_EMPTY_BUCKETS);

  RememberedSet<OLD_TO_NEW>::RemoveRange(chunk, chunk->area_end() - kTaggedSize,
                                         chunk->area_end(),
                                         SlotSet::FREE_EMPTY_BUCKETS);
  slots[chunk->area_end() - kTaggedSize] = false;
  RememberedSet<OLD_TO_NEW>::Iterate(
      chunk,
      [&slots](MaybeObjectSlot slot) {
        CHECK(slots[slot.address()]);
        return KEEP_SLOT;
      },
      SlotSet::FREE_EMPTY_BUCKETS);
}

HEAP_TEST(Regress670675) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();
  heap::InvokeMajorGC(heap);

  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }
  heap->tracer()->StopFullCycleIfNeeded();
  i::IncrementalMarking* marking = CcTest::heap()->incremental_marking();
  if (marking->IsStopped()) {
    IsolateSafepointScope safepoint_scope(heap);
    heap->tracer()->StartCycle(
        GarbageCollector::MARK_COMPACTOR, GarbageCollectionReason::kTesting,
        "collector cctest", GCTracer::MarkingType::kIncremental);
    marking->Start(GarbageCollector::MARK_COMPACTOR,
                   i::GarbageCollectionReason::kTesting);
  }
  size_t array_length = 128 * KB;
  size_t n = heap->OldGenerationSpaceAvailable() / array_length;
  for (size_t i = 0; i < n + 60; i++) {
    {
      HandleScope inner_scope(isolate);
      isolate->factory()->NewFixedArray(static_cast<int>(array_length),
                                        AllocationType::kOld);
    }
    if (marking->IsStopped()) break;
    marking->AdvanceForTesting(v8::base::TimeDelta::FromMillisecondsD(0.1));
  }
  DCHECK(marking->IsStopped());
}

HEAP_TEST(RegressMissingWriteBarrierInAllocate) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  LocalContext env;
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();
  heap::InvokeMajorGC(heap);
  heap::SimulateIncrementalMarking(heap, false);
  DirectHandle<Map> map;
  {
    AlwaysAllocateScopeForTesting always_allocate(heap);
    map = isolate->factory()->NewContextfulMapForCurrentContext(
        JS_OBJECT_TYPE, JSObject::kHeaderSize);
  }
  CHECK(heap->incremental_marking()->black_allocation());
  DirectHandle<JSObject> object;
  {
    AlwaysAllocateScopeForTesting always_allocate(heap);
    object = direct_handle(Cast<JSObject>(isolate->factory()->NewForTest(
                               map, AllocationType::kOld)),
                           isolate);
  }
  // Initialize backing stores to ensure object is valid.
  ReadOnlyRoots roots(isolate);
  object->set_raw_properties_or_hash(roots.empty_property_array(),
                                     SKIP_WRITE_BARRIER);
  object->set_elements(roots.empty_fixed_array(), SKIP_WRITE_BARRIER);

  // The object is black. If Factory::New sets the map without write-barrier,
  // then the map is white and will be freed prematurely.
  heap::SimulateIncrementalMarking(heap, true);
  heap::InvokeMajorGC(heap);
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }
  CHECK(IsMap(object->map()));
}

HEAP_TEST(MarkCompactEpochCounter) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  Heap* heap = CcTest::heap();
  unsigned epoch0 = heap->mark_compact_collector()->epoch();
  heap::InvokeMajorGC(heap);
  unsigned epoch1 = heap->mark_compact_collector()->epoch();
  CHECK_EQ(epoch0 + 1, epoch1);
  heap::SimulateIncrementalMarking(heap, true);
  heap::InvokeMajorGC(heap);
  unsigned epoch2 = heap->mark_compact_collector()->epoch();
  CHECK_EQ(epoch1 + 1, epoch2);
  heap::InvokeMinorGC(heap);
  unsigned epoch3 = heap->mark_compact_collector()->epoch();
  CHECK_EQ(epoch2, epoch3);
}

UNINITIALIZED_TEST(ReinitializeStringHashSeed) {
  // Enable rehashing and create an isolate and context.
  i::v8_flags.rehash_snapshot = true;
  for (int i = 1; i < 3; i++) {
    i::v8_flags.hash_seed = 1337 * i;
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    {
      v8::Isolate::Scope isolate_scope(isolate);
      CHECK_EQ(static_cast<uint64_t>(1337 * i),
               HashSeed(reinterpret_cast<i::Isolate*>(isolate)));
      v8::HandleScope handle_scope(isolate);
      v8::Local<v8::Context> context = v8::Context::New(isolate);
      CHECK(!context.IsEmpty());
      v8::Context::Scope context_scope(context);
    }
    isolate->Dispose();
  }
}

const int kHeapLimit = 100 * MB;
Isolate* oom_isolate = nullptr;

void OOMCallback(const char* location, const OOMDetails&) {
  Heap* heap = oom_isolate->heap();
  size_t kSlack = heap->new_space() ? heap->MaxSemiSpaceSize() : 0;
  CHECK_LE(heap->OldGenerationCapacity(), kHeapLimit + kSlack);
  base::OS::ExitProcess(0);
}

UNINITIALIZED_TEST(OutOfMemory) {
  if (v8_flags.stress_incremental_marking) return;
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) return;
#endif
  v8_flags.max_old_space_size = kHeapLimit / MB;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  oom_isolate = i_isolate;
  isolate->SetOOMErrorHandler(OOMCallback);
  {
    PtrComprCageAccessScope ptr_compr_cage_access_scope(i_isolate);
    Factory* factory = i_isolate->factory();
    HandleScope handle_scope(i_isolate);
    while (true) {
      factory->NewFixedArray(100);
    }
  }
}

UNINITIALIZED_TEST(OutOfMemoryIneffectiveGC) {
  if (!v8_flags.detect_ineffective_gcs_near_heap_limit) return;
  if (v8_flags.stress_incremental_marking ||
      v8_flags.stress_concurrent_allocation)
    return;
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) return;
#endif

  v8_flags.max_old_space_size = kHeapLimit / MB;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  oom_isolate = i_isolate;
  isolate->SetOOMErrorHandler(OOMCallback);
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();
  {
    PtrComprCageAccessScope ptr_compr_cage_access_scope(i_isolate);
    heap::InvokeMajorGC(heap);

    HandleScope scope(i_isolate);
    while (heap->OldGenerationSizeOfObjects() <
           heap->MaxOldGenerationSize() * 0.9) {
      factory->NewFixedArray(100, AllocationType::kOld);
    }
    {
      int initial_ms_count = heap->ms_count();
      int ineffective_ms_start = initial_ms_count;
      while (heap->ms_count() < initial_ms_count + 10) {
        HandleScope inner_scope(i_isolate);
        factory->NewFixedArray(30000, AllocationType::kOld);
        if (heap->tracer()->AverageMarkCompactMutatorUtilization() >= 0.3) {
          ineffective_ms_start = heap->ms_count() + 1;
        }
      }
      int consecutive_ineffective_ms = heap->ms_count() - ineffective_ms_start;
      CHECK_IMPLIES(
          consecutive_ineffective_ms >= 4,
          heap->tracer()->AverageMarkCompactMutatorUtilization() >= 0.3);
    }
  }
  isolate->Dispose();
}

UNINITIALIZED_TEST(OutOfMemoryIneffectiveGCRunningJS) {
  if (!v8_flags.detect_ineffective_gcs_near_heap_limit) return;
  if (v8_flags.stress_incremental_marking) return;

  v8_flags.max_old_space_size = 10;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  oom_isolate = i_isolate;

  isolate->SetOOMErrorHandler(OOMCallback);

  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::New(isolate)->Enter();

  // Test that source positions are not collected as part of a failing GC, which
  // will fail as allocation is disallowed. If the test works, this should call
  // OOMCallback and terminate without crashing.
  CompileRun(R"javascript(
      var array = [];
      for(var i = 20000; i < 40000; ++i) {
        array.push(new Array(i));
      }
      )javascript");

  FATAL("Should not get here as OOMCallback should be called");
}

HEAP_TEST(Regress779503) {
  // The following regression test ensures that the Scavenger does not allocate
  // over invalid slots. More specific, the Scavenger should not sweep a page
  // that it currently processes because it might allocate over the currently
  // processed slot.
  if (v8_flags.single_generation) return;
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  const int kArraySize = 2048;
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  heap::SealCurrentObjects(heap);
  {
    HandleScope handle_scope(isolate);
    // The byte array filled with kHeapObjectTag ensures that we cannot read
    // from the slot again and interpret it as heap value. Doing so will crash.
    DirectHandle<ByteArray> byte_array =
        isolate->factory()->NewByteArray(kArraySize);
    CHECK(Heap::InYoungGeneration(*byte_array));
    for (int i = 0; i < kArraySize; i++) {
      byte_array->set(i, kHeapObjectTag);
    }

    {
      HandleScope new_scope(isolate);
      // The FixedArray in old space serves as space for slots.
      DirectHandle<FixedArray> fixed_array =
          isolate->factory()->NewFixedArray(kArraySize, AllocationType::kOld);
      CHECK(!Heap::InYoungGeneration(*fixed_array));
      for (int i = 0; i < kArraySize; i++) {
        fixed_array->set(i, *byte_array);
      }
    }
    // Delay sweeper tasks to allow the scavenger to sweep the page it is
    // currently scavenging.
    heap->delay_sweeper_tasks_for_testing_ = true;
    heap::InvokeMajorGC(heap);
    CHECK(!Heap::InYoungGeneration(*byte_array));
  }
  // Scavenging and sweeping the same page will crash as slots will be
  // overridden.
  heap::InvokeMinorGC(heap);
  heap->delay_sweeper_tasks_for_testing_ = false;
}

struct OutOfMemoryState {
  Heap* heap;
  bool oom_triggered;
  size_t old_generation_capacity_at_oom;
  size_t memory_allocator_size_at_oom;
  size_t new_space_capacity_at_oom;
  size_t new_lo_space_size_at_oom;
  size_t current_heap_limit;
  size_t initial_heap_limit;
};

size_t NearHeapLimitCallback(void* raw_state, size_t current_heap_limit,
                             size_t initial_heap_limit) {
  OutOfMemoryState* state = static_cast<OutOfMemoryState*>(raw_state);
  Heap* heap = state->heap;
  state->oom_triggered = true;
  state->old_generation_capacity_at_oom = heap->OldGenerationCapacity();
  state->memory_allocator_size_at_oom = heap->memory_allocator()->Size();
  state->new_space_capacity_at_oom =
      heap->new_space() ? heap->new_space()->Capacity() : 0;
  state->new_lo_space_size_at_oom =
      heap->new_lo_space() ? heap->new_lo_space()->Size() : 0;
  state->current_heap_limit = current_heap_limit;
  state->initial_heap_limit = initial_heap_limit;
  return initial_heap_limit + 100 * MB;
}

size_t MemoryAllocatorSizeFromHeapCapacity(size_t capacity) {
  // Size to capacity factor.
  double factor = PageMetadata::kPageSize * 1.0 /
                  MemoryChunkLayout::AllocatableMemoryInDataPage();
  // Some tables (e.g. deoptimization table) are allocated directly with the
  // memory allocator. Allow some slack to account for them.
  size_t slack = 5 * MB;
  return static_cast<size_t>(capacity * factor) + slack;
}

UNINITIALIZED_TEST(OutOfMemorySmallObjects) {
  if (v8_flags.stress_incremental_marking) return;
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) return;
#endif
  const size_t kOldGenerationLimit = 50 * MB;
  v8_flags.max_old_space_size = kOldGenerationLimit / MB;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  Isolate* isolate =
      reinterpret_cast<Isolate*>(v8::Isolate::New(create_params));
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  OutOfMemoryState state;
  state.heap = heap;
  state.oom_triggered = false;
  heap->AddNearHeapLimitCallback(NearHeapLimitCallback, &state);
  {
    PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);

    HandleScope handle_scope(isolate);
    while (!state.oom_triggered) {
      factory->NewFixedArray(100);
    }
  }
  CHECK_LE(state.old_generation_capacity_at_oom,
           kOldGenerationLimit + heap->MaxSemiSpaceSize());
  CHECK_LE(kOldGenerationLimit,
           state.old_generation_capacity_at_oom + heap->MaxSemiSpaceSize());
  CHECK_LE(
      state.memory_allocator_size_at_oom,
      MemoryAllocatorSizeFromHeapCapacity(state.old_generation_capacity_at_oom +
                                          2 * state.new_space_capacity_at_oom));
  reinterpret_cast<v8::Isolate*>(isolate)->Dispose();
}

UNINITIALIZED_TEST(OutOfMemoryLargeObjects) {
  if (v8_flags.stress_incremental_marking) return;
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) return;
#endif
  const size_t kOldGenerationLimit = 50 * MB;
  v8_flags.max_old_space_size = kOldGenerationLimit / MB;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  Isolate* isolate =
      reinterpret_cast<Isolate*>(v8::Isolate::New(create_params));
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  OutOfMemoryState state;
  state.heap = heap;
  state.oom_triggered = false;
  heap->AddNearHeapLimitCallback(NearHeapLimitCallback, &state);
  {
    PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
    const int kFixedArrayLength = 1000000;
    {
      HandleScope handle_scope(isolate);
      while (!state.oom_triggered) {
        factory->NewFixedArray(kFixedArrayLength);
      }
    }
    CHECK_LE(state.old_generation_capacity_at_oom,
             kOldGenerationLimit + state.new_space_capacity_at_oom +
                 state.new_lo_space_size_at_oom +
                 FixedArray::SizeFor(kFixedArrayLength));
    CHECK_LE(kOldGenerationLimit, state.old_generation_capacity_at_oom +
                                      state.new_space_capacity_at_oom +
                                      state.new_lo_space_size_at_oom +
                                      FixedArray::SizeFor(kFixedArrayLength));
    CHECK_LE(state.memory_allocator_size_at_oom,
             MemoryAllocatorSizeFromHeapCapacity(
                 state.old_generation_capacity_at_oom +
                 2 * state.new_space_capacity_at_oom +
                 state.new_lo_space_size_at_oom));
  }
  reinterpret_cast<v8::Isolate*>(isolate)->Dispose();
}

UNINITIALIZED_TEST(RestoreHeapLimit) {
  if (v8_flags.stress_incremental_marking) return;
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) return;
#endif
  ManualGCScope manual_gc_scope;
  const size_t kOldGenerationLimit = 50 * MB;
  v8_flags.max_old_space_size = kOldGenerationLimit / MB;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  Isolate* isolate =
      reinterpret_cast<Isolate*>(v8::Isolate::New(create_params));
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();

  {
    PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);

    // In this test, we need to invoke GC without stack, otherwise some objects
    // may not be reclaimed because of conservative stack scanning and the heap
    // limit may be reached.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);

    OutOfMemoryState state;
    state.heap = heap;
    state.oom_triggered = false;
    heap->AddNearHeapLimitCallback(NearHeapLimitCallback, &state);
    heap->AutomaticallyRestoreInitialHeapLimit(0.5);
    const int kFixedArrayLength = 1000000;
    {
      HandleScope handle_scope(isolate);
      while (!state.oom_triggered) {
        factory->NewFixedArray(kFixedArrayLength);
      }
    }
    heap->MemoryPressureNotification(MemoryPressureLevel::kCritical, true);
    state.oom_triggered = false;
    {
      HandleScope handle_scope(isolate);
      while (!state.oom_triggered) {
        factory->NewFixedArray(kFixedArrayLength);
      }
    }
    CHECK_EQ(state.current_heap_limit, state.initial_heap_limit);
  }

  reinterpret_cast<v8::Isolate*>(isolate)->Dispose();
}

void HeapTester::UncommitUnusedMemory(Heap* heap) {
  if (!v8_flags.minor_ms) SemiSpaceNewSpace::From(heap->new_space())->Shrink();
  heap->memory_allocator()->pool()->ReleasePooledChunks();
}

class DeleteNative {
 public:
  static constexpr ExternalPointerTag kManagedTag = kGenericManagedTag;
  static void Deleter(void* arg) {
    delete reinterpret_cast<DeleteNative*>(arg);
  }
};

TEST(Regress8014) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  Heap* heap = isolate->heap();
  {
    HandleScope scope(isolate);
    for (int i = 0; i < 10000; i++) {
      auto handle = Managed<DeleteNative>::From(
          isolate, 1000000, std::make_shared<DeleteNative>());
      USE(handle);
    }
  }
  int ms_count = heap->ms_count();
  heap->MemoryPressureNotification(MemoryPressureLevel::kCritical, true);
  // Several GCs can be triggred by the above call.
  // The bad case triggers 10000 GCs.
  CHECK_LE(heap->ms_count(), ms_count + 10);
}

TEST(Regress8617) {
  if (!v8_flags.incremental_marking) return;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  HandleScope scope(isolate);
  heap::SimulateFullSpace(heap->old_space());
  // Step 1. Create a function and ensure that it is in the old space.
  DirectHandle<Object> foo =
      v8::Utils::OpenDirectHandle(*CompileRun("function foo() { return 42; };"
                                              "foo;"));
  if (heap->InYoungGeneration(*foo)) {
    heap::EmptyNewSpaceUsingGC(heap);
  }
  // Step 2. Create an object with a reference to foo in the descriptor array.
  CompileRun(
      "var obj = {};"
      "obj.method = foo;"
      "obj;");
  // Step 3. Make sure that foo moves during Mark-Compact.
  PageMetadata* ec_page = PageMetadata::FromAddress((*foo).ptr());
  heap::ForceEvacuationCandidate(ec_page);
  // Step 4. Start incremental marking.
  heap::SimulateIncrementalMarking(heap, false);
  CHECK(ec_page->Chunk()->IsEvacuationCandidate());
  // Step 5. Install a new descriptor array on the map of the object.
  // This runs the marking barrier for the descriptor array.
  // In the bad case it sets the number of marked descriptors but does not
  // change the color of the descriptor array.
  CompileRun("obj.bar = 10;");
  // Step 6. Promote the descriptor array to old space. During promotion
  // the Scavenger will not record the slot of foo in the descriptor array.
  heap::EmptyNewSpaceUsingGC(heap);
  // Step 7. Complete the Mark-Compact.
  heap::InvokeMajorGC(heap);
  // Step 8. Use the descriptor for foo, which contains a stale pointer.
  CompileRun("obj.method()");
}

HEAP_TEST(MemoryReducerActivationForSmallHeaps) {
  if (v8_flags.single_generation || !v8_flags.memory_reducer) return;
  ManualGCScope manual_gc_scope;
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  CHECK_EQ(heap->memory_reducer()->state_.id(), MemoryReducer::kUninit);
  HandleScope scope(isolate);
  const size_t kActivationThreshold = 1 * MB;
  size_t initial_capacity = heap->OldGenerationCapacity();
  while (heap->OldGenerationCapacity() <
         initial_capacity + kActivationThreshold) {
    isolate->factory()->NewFixedArray(1 * KB, AllocationType::kOld);
  }
  CHECK_EQ(heap->memory_reducer()->state_.id(), MemoryReducer::kWait);
}

TEST(AllocateExternalBackingStore) {
  ManualGCScope manual_gc_scope;
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  int initial_ms_count = heap->ms_count();
  void* result =
      heap->AllocateExternalBackingStore([](size_t) { return nullptr; }, 10);
  CHECK_NULL(result);
  // At least two GCs should happen.
  CHECK_LE(2, heap->ms_count() - initial_ms_count);
}

TEST(CodeObjectRegistry) {
  // We turn off compaction to ensure that code is not moving.
  v8_flags.compact = false;

  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  DirectHandle<InstructionStream> code1;
  HandleScope outer_scope(heap->isolate());
  Address code2_address;
  {
    // Ensure that both code objects end up on the same page.
    CHECK(HeapTester::CodeEnsureLinearAllocationArea(
        heap, MemoryChunkLayout::MaxRegularCodeObjectSize()));
    code1 = DummyOptimizedCode(isolate);
    DirectHandle<InstructionStream> code2 = DummyOptimizedCode(isolate);
    code2_address = code2->address();

    CHECK_EQ(MutablePageMetadata::FromHeapObject(*code1),
             MutablePageMetadata::FromHeapObject(*code2));
    CHECK(MutablePageMetadata::FromHeapObject(*code1)->Contains(
        code1->address()));
    CHECK(MutablePageMetadata::FromHeapObject(*code2)->Contains(
        code2->address()));
  }
  heap::InvokeMemoryReducingMajorGCs(heap);
  CHECK(
      MutablePageMetadata::FromHeapObject(*code1)->Contains(code1->address()));
  CHECK(
      MutablePageMetadata::FromAddress(code2_address)->Contains(code2_address));
}

TEST(Regress9701) {
  ManualGCScope manual_gc_scope;
  if (!v8_flags.incremental_marking || v8_flags.separate_gc_phases) return;
  CcTest::InitializeVM();
  Heap* heap = CcTest::heap();
  // Start with an empty new space.
  heap::EmptyNewSpaceUsingGC(heap);

  int mark_sweep_count_before = heap->ms_count();
  // Allocate many short living array buffers.
  for (int i = 0; i < 1000; i++) {
    HandleScope scope(heap->isolate());
    CcTest::i_isolate()->factory()->NewJSArrayBufferAndBackingStore(
        64 * KB, InitializedFlag::kZeroInitialized);
  }
  int mark_sweep_count_after = heap->ms_count();
  // We expect only scavenges, no full GCs.
  CHECK_EQ(mark_sweep_count_before, mark_sweep_count_after);
}

#if defined(V8_TARGET_ARCH_64_BIT) && !defined(V8_OS_ANDROID)
UNINITIALIZED_TEST(HugeHeapLimit) {
  uint64_t kMemoryGB = 16;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  create_params.constraints.ConfigureDefaults(kMemoryGB * GB, kMemoryGB * GB);
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
#ifdef V8_COMPRESS_POINTERS
  size_t kExpectedHeapLimit = Heap::AllocatorLimitOnMaxOldGenerationSize();
#else
  size_t kExpectedHeapLimit = size_t{4} * GB;
#endif
  CHECK_EQ(kExpectedHeapLimit, i_isolate->heap()->MaxOldGenerationSize());
  CHECK_LT(size_t{3} * GB, i_isolate->heap()->MaxOldGenerationSize());
  isolate->Dispose();
}
#endif

UNINITIALIZED_TEST(HeapLimit) {
  uint64_t kMemoryGB = 8;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  create_params.constraints.ConfigureDefaults(kMemoryGB * GB, kMemoryGB * GB);
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
#if defined(V8_TARGET_ARCH_64_BIT) && !defined(V8_OS_ANDROID)
  size_t kExpectedHeapLimit = size_t{2} * GB;
#else
  size_t kExpectedHeapLimit = size_t{1} * GB;
#endif
  CHECK_EQ(kExpectedHeapLimit, i_isolate->heap()->MaxOldGenerationSize());
  isolate->Dispose();
}

TEST(NoCodeRangeInJitlessMode) {
  if (!v8_flags.jitless) return;
  CcTest::InitializeVM();
  CHECK(CcTest::i_isolate()->heap()->code_region().is_empty());
}

TEST(GarbageCollectionWithLocalHeap) {
  ManualGCScope manual_gc_scope;
  CcTest::InitializeVM();

  LocalHeap* local_heap = CcTest::i_isolate()->main_thread_local_heap();

  heap::InvokeMajorGC(CcTest::heap());
  local_heap->ExecuteWhileParked([]() { /* nothing */ });
  heap::InvokeMajorGC(CcTest::heap());
}

TEST(Regress10698) {
  if (!v8_flags.incremental_marking) return;
  CcTest::InitializeVM();
  Heap* heap = CcTest::i_isolate()->heap();
  Factory* factory = CcTest::i_isolate()->factory();
  HandleScope handle_scope(CcTest::i_isolate());
  // This is modeled after the manual allocation folding of heap numbers in
  // JSON parser (See commit ba7b25e).
  // Step 1. Allocate a byte array in the old space.
  DirectHandle<ByteArray> array =
      factory->NewByteArray(kTaggedSize, AllocationType::kOld);
  // Step 2. Start incremental marking.
  SimulateIncrementalMarking(heap, false);
  // Step 3. Allocate another byte array. It will be black.
  factory->NewByteArray(kTaggedSize, AllocationType::kOld);
  Address address = reinterpret_cast<Address>(array->begin());
  Tagged<HeapObject> filler = HeapObject::FromAddress(address);
  // Step 4. Set the filler at the end of the first array.
  // It will have an impossible markbit pattern because the second markbit
  // will be taken from the second array.
  filler->set_map_after_allocation(*factory->one_pointer_filler_map());
}

class TestAllocationTracker : public HeapObjectAllocationTracker {
 public:
  explicit TestAllocationTracker(int expected_size)
      : expected_size_(expected_size) {}

  void AllocationEvent(Address addr, int size) {
    CHECK(expected_size_ == size);
    address_ = addr;
  }

  Address address() { return address_; }

 private:
  int expected_size_;
  Address address_;
};

HEAP_TEST(CodeLargeObjectSpace) {
  Heap* heap = CcTest::heap();
  int size_in_bytes =
      heap->MaxRegularHeapObjectSize(AllocationType::kCode) + kTaggedSize;
  TestAllocationTracker allocation_tracker{size_in_bytes};
  heap->AddHeapObjectAllocationTracker(&allocation_tracker);

  Tagged<HeapObject> obj;
  {
    AllocationResult allocation = heap->AllocateRaw(
        size_in_bytes, AllocationType::kCode, AllocationOrigin::kRuntime);
    CHECK(allocation.To(&obj));
    CHECK_EQ(allocation.ToAddress(), allocation_tracker.address());
    ThreadIsolation::RegisterInstructionStreamAllocation(obj.address(),
                                                         size_in_bytes);

    heap->CreateFillerObjectAt(obj.address(), size_in_bytes);
  }

  CHECK(Heap::IsLargeObject(obj));
  heap->RemoveHeapObjectAllocationTracker(&allocation_tracker);
}

UNINITIALIZED_HEAP_TEST(CodeLargeObjectSpace64k) {
  // Simulate having a system with 64k OS pages.
  i::v8_flags.v8_os_page_size = 64;

  // Initialize the isolate manually to make sure --v8-os-page-size is taken
  // into account.
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  i::Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  Heap* heap = i_isolate->heap();
  PtrComprCageAccessScope ptr_compr_cage_access_scope(i_isolate);

  // Allocate a regular code object.
  {
    int size_in_bytes =
        heap->MaxRegularHeapObjectSize(AllocationType::kCode) - kTaggedSize;
    TestAllocationTracker allocation_tracker{size_in_bytes};
    heap->AddHeapObjectAllocationTracker(&allocation_tracker);

    Tagged<HeapObject> obj;
    {
      AllocationResult allocation = heap->AllocateRaw(
          size_in_bytes, AllocationType::kCode, AllocationOrigin::kRuntime);
      CHECK(allocation.To(&obj));
      CHECK_EQ(allocation.ToAddress(), allocation_tracker.address());
      ThreadIsolation::RegisterInstructionStreamAllocation(obj.address(),
                                                           size_in_bytes);

      heap->CreateFillerObjectAt(obj.address(), size_in_bytes);
    }

    CHECK(!Heap::IsLargeObject(obj));
    heap->RemoveHeapObjectAllocationTracker(&allocation_tracker);
  }

  // Allocate a large code object.
  {
    int size_in_bytes =
        heap->MaxRegularHeapObjectSize(AllocationType::kCode) + kTaggedSize;
    TestAllocationTracker allocation_tracker{size_in_bytes};
    heap->AddHeapObjectAllocationTracker(&allocation_tracker);

    Tagged<HeapObject> obj;
    {
      AllocationResult allocation = heap->AllocateRaw(
          size_in_bytes, AllocationType::kCode, AllocationOrigin::kRuntime);
      CHECK(allocation.To(&obj));
      CHECK_EQ(allocation.ToAddress(), allocation_tracker.address());
      ThreadIsolation::RegisterInstructionStreamAllocation(obj.address(),
                                                           size_in_bytes);

      heap->CreateFillerObjectAt(obj.address(), size_in_bytes);
    }

    CHECK(Heap::IsLargeObject(obj));
    heap->RemoveHeapObjectAllocationTracker(&allocation_tracker);
  }

  isolate->Dispose();
}

TEST(IsPendingAllocationNewSpace) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  HandleScope handle_scope(isolate);
  DirectHandle<FixedArray> object =
      factory->NewFixedArray(5, AllocationType::kYoung);
  CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                heap->IsPendingAllocation(*object));
  heap->PublishMainThreadPendingAllocations();
  CHECK(!heap->IsPendingAllocation(*object));
}

TEST(IsPendingAllocationNewLOSpace) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  HandleScope handle_scope(isolate);
  DirectHandle<FixedArray> object = factory->NewFixedArray(
      FixedArray::kMaxRegularLength + 1, AllocationType::kYoung);
  CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                heap->IsPendingAllocation(*object));
  heap->PublishMainThreadPendingAllocations();
  CHECK(!heap->IsPendingAllocation(*object));
}

TEST(IsPendingAllocationOldSpace) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  HandleScope handle_scope(isolate);
  DirectHandle<FixedArray> object =
      factory->NewFixedArray(5, AllocationType::kOld);
  CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                heap->IsPendingAllocation(*object));
  heap->PublishMainThreadPendingAllocations();
  CHECK(!heap->IsPendingAllocation(*object));
}

TEST(IsPendingAllocationLOSpace) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  HandleScope handle_scope(isolate);
  DirectHandle<FixedArray> object = factory->NewFixedArray(
      FixedArray::kMaxRegularLength + 1, AllocationType::kOld);
  CHECK_IMPLIES(!v8_flags.enable_third_party_heap,
                heap->IsPendingAllocation(*object));
  heap->PublishMainThreadPendingAllocations();
  CHECK(!heap->IsPendingAllocation(*object));
}

TEST(Regress10900) {
  v8_flags.compact_on_every_full_gc = true;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  HandleScope handle_scope(isolate);
  uint8_t buffer[i::Assembler::kDefaultBufferSize];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
#if V8_TARGET_ARCH_ARM64
  UseScratchRegisterScope temps(&masm);
  Register tmp = temps.AcquireX();
  masm.Mov(tmp, Operand(static_cast<int32_t>(
                    ReadOnlyRoots(heap).undefined_value().ptr())));
  masm.Push(tmp, tmp);
#else
  masm.Push(ReadOnlyRoots(heap).undefined_value_handle());
#endif
  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  {
    DirectHandle<Code> code;
    for (int i = 0; i < 100; i++) {
      // Generate multiple code pages.
      code = Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    }
  }
  // Force garbage collection that compacts code pages and triggers
  // an assertion in Isolate::AddCodeMemoryRange before the bug fix.
  heap::InvokeMemoryReducingMajorGCs(heap);
}

namespace {
void GenerateGarbage() {
  const char* source =
      "let roots = [];"
      "for (let i = 0; i < 100; i++) roots.push(new Array(1000).fill(0));"
      "roots.push(new Array(1000000).fill(0));"
      "roots;";
  CompileRun(source);
}

}  // anonymous namespace

TEST(Regress11181) {
  v8_flags.compact_on_every_full_gc = true;
  CcTest::InitializeVM();
  TracingFlags::runtime_stats.store(
      v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE,
      std::memory_order_relaxed);
  v8::HandleScope scope(CcTest::isolate());
  GenerateGarbage();
  heap::InvokeMemoryReducingMajorGCs(CcTest::heap());
}

TEST(LongTaskStatsFullAtomic) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(CcTest::isolate());
  GenerateGarbage();
  v8::metrics::LongTaskStats::Reset(isolate);
  CHECK_EQ(0u, v8::metrics::LongTaskStats::Get(isolate)
                   .gc_full_atomic_wall_clock_duration_us);
  for (int i = 0; i < 10; ++i) {
    heap::InvokeMemoryReducingMajorGCs(CcTest::heap());
  }
  CHECK_LT(0u, v8::metrics::LongTaskStats::Get(isolate)
                   .gc_full_atomic_wall_clock_duration_us);
  v8::metrics::LongTaskStats::Reset(isolate);
  CHECK_EQ(0u, v8::metrics::LongTaskStats::Get(isolate)
                   .gc_full_atomic_wall_clock_duration_us);
}

TEST(LongTaskStatsFullIncremental) {
  if (!v8_flags.incremental_marking) return;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(CcTest::isolate());
  GenerateGarbage();
  v8::metrics::LongTaskStats::Reset(isolate);
  CHECK_EQ(0u, v8::metrics::LongTaskStats::Get(isolate)
                   .gc_full_incremental_wall_clock_duration_us);
  for (int i = 0; i < 10; ++i) {
    heap::SimulateIncrementalMarking(CcTest::heap());
    heap::InvokeMemoryReducingMajorGCs(CcTest::heap());
  }
  CHECK_LT(0u, v8::metrics::LongTaskStats::Get(isolate)
                   .gc_full_incremental_wall_clock_duration_us);
  v8::metrics::LongTaskStats::Reset(isolate);
  CHECK_EQ(0u, v8::metrics::LongTaskStats::Get(isolate)
                   .gc_full_incremental_wall_clock_duration_us);
}

TEST(LongTaskStatsYoung) {
  if (v8_flags.single_generation) return;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(CcTest::isolate());
  GenerateGarbage();
  v8::metrics::LongTaskStats::Reset(isolate);
  CHECK_EQ(
      0u,
      v8::metrics::LongTaskStats::Get(isolate).gc_young_wall_clock_duration_us);
  for (int i = 0; i < 10; ++i) {
    heap::InvokeMinorGC(CcTest::heap());
  }
  CHECK_LT(
      0u,
      v8::metrics::LongTaskStats::Get(isolate).gc_young_wall_clock_duration_us);
  v8::metrics::LongTaskStats::Reset(isolate);
  CHECK_EQ(
      0u,
      v8::metrics::LongTaskStats::Get(isolate).gc_young_wall_clock_duration_us);
}

}  // namespace heap
}  // namespace internal
}  // namespace v8

#undef __
