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

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/builtins/accessors.h"
#include "src/heap/heap-inl.h"
#include "src/init/v8.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace heap {

Handle<Object> HeapTester::TestAllocateAfterFailures() {
  // Similar to what the factory's retrying logic does in the last-resort case,
  // we wrap the allocator function in an AlwaysAllocateScope.  Test that
  // all allocations succeed immediately without any retry.
  Heap* heap = CcTest::heap();
  heap::InvokeMemoryReducingMajorGCs(heap);
  AlwaysAllocateScopeForTesting scope(heap);
  int size = FixedArray::SizeFor(100);
  // Young generation.
  Tagged<HeapObject> obj =
      heap->AllocateRaw(size, AllocationType::kYoung).ToObjectChecked();
  // In order to pass heap verification on Isolate teardown, mark the
  // allocated area as a filler.
  heap->CreateFillerObjectAt(obj.address(), size);

  // Old generation.
  heap::SimulateFullSpace(heap->old_space());
  obj = heap->AllocateRaw(size, AllocationType::kOld).ToObjectChecked();
  heap->CreateFillerObjectAt(obj.address(), size);

  // Large object space.
  static const size_t kLargeObjectSpaceFillerLength =
      3 * (PageMetadata::kPageSize / 10);
  static const size_t kLargeObjectSpaceFillerSize =
      FixedArray::SizeFor(kLargeObjectSpaceFillerLength);
  CHECK_GT(kLargeObjectSpaceFillerSize,
           static_cast<size_t>(heap->old_space()->AreaSize()));
  while (heap->OldGenerationSpaceAvailable() > kLargeObjectSpaceFillerSize) {
    obj = heap->AllocateRaw(kLargeObjectSpaceFillerSize, AllocationType::kOld)
              .ToObjectChecked();
    heap->CreateFillerObjectAt(obj.address(), size);
  }
  obj = heap->AllocateRaw(kLargeObjectSpaceFillerSize, AllocationType::kOld)
            .ToObjectChecked();
  heap->CreateFillerObjectAt(obj.address(), size);

  // Map space.
  heap::SimulateFullSpace(heap->old_space());
  obj = heap->AllocateRaw(Map::kSize, AllocationType::kMap).ToObjectChecked();
  heap->CreateFillerObjectAt(obj.address(), Map::kSize);

  // Code space.
  heap::SimulateFullSpace(heap->code_space());
  size = CcTest::i_isolate()->builtins()->code(Builtin::kIllegal)->Size();
  obj =
      heap->AllocateRaw(size, AllocationType::kCode, AllocationOrigin::kRuntime)
          .ToObjectChecked();
  heap->CreateFillerObjectAt(obj.address(), size);
  return CcTest::i_isolate()->factory()->true_value();
}


HEAP_TEST(StressHandles) {
  // For TestAllocateAfterFailures.
  v8_flags.stress_concurrent_allocation = false;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = v8::Context::New(CcTest::isolate());
  env->Enter();
  DirectHandle<Object> o = TestAllocateAfterFailures();
  CHECK(IsTrue(*o, CcTest::i_isolate()));
  env->Exit();
}


void TestGetter(
    v8::Local<v8::Name> name,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  HandleScope scope(isolate);
  info.GetReturnValue().Set(
      v8::Utils::ToLocal(HeapTester::TestAllocateAfterFailures()));
}

void TestSetter(v8::Local<v8::Name> name, v8::Local<v8::Value> value,
                const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  UNREACHABLE();
}


Handle<AccessorInfo> TestAccessorInfo(
      Isolate* isolate, PropertyAttributes attributes) {
  Handle<String> name = isolate->factory()->NewStringFromStaticChars("get");
  return Accessors::MakeAccessor(isolate, name, &TestGetter, &TestSetter);
}


TEST(StressJS) {
  // For TestAllocateAfterFailures in TestGetter.
  v8_flags.stress_concurrent_allocation = false;
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = v8::Context::New(CcTest::isolate());
  env->Enter();

  Handle<NativeContext> context(isolate->native_context());
  Handle<SharedFunctionInfo> info = factory->NewSharedFunctionInfoForBuiltin(
      factory->function_string(), Builtin::kEmptyFunction);
  info->set_language_mode(LanguageMode::kStrict);
  Handle<JSFunction> function =
      Factory::JSFunctionBuilder{isolate, info, context}.Build();
  CHECK(!function->shared()->construct_as_builtin());

  // Force the creation of an initial map.
  factory->NewJSObject(function);

  // Patch the map to have an accessor for "get".
  DirectHandle<Map> map(function->initial_map(), isolate);
  DirectHandle<DescriptorArray> instance_descriptors(
      map->instance_descriptors(isolate), isolate);
  CHECK_EQ(0, instance_descriptors->number_of_descriptors());

  PropertyAttributes attrs = NONE;
  Handle<AccessorInfo> foreign = TestAccessorInfo(isolate, attrs);
  Map::EnsureDescriptorSlack(isolate, map, 1);

  Descriptor d = Descriptor::AccessorConstant(
      Handle<Name>(Cast<Name>(foreign->name()), isolate), foreign, attrs);
  map->AppendDescriptor(isolate, &d);

  // Add the Foo constructor the global object.
  CHECK(env->Global()
            ->Set(env, v8::String::NewFromUtf8Literal(CcTest::isolate(), "Foo"),
                  v8::Utils::CallableToLocal(function))
            .FromJust());
  // Call the accessor through JavaScript.
  v8::Local<v8::Value> result =
      v8::Script::Compile(env, v8::String::NewFromUtf8Literal(CcTest::isolate(),
                                                              "(new Foo).get"))
          .ToLocalChecked()
          ->Run(env)
          .ToLocalChecked();
  CHECK_EQ(true, result->BooleanValue(CcTest::isolate()));
  env->Exit();
}

}  // namespace heap
}  // namespace internal
}  // namespace v8
