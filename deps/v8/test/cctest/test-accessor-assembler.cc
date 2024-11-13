// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/utils/random-number-generator.h"
#include "src/ic/accessor-assembler.h"
#include "src/ic/stub-cache.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/function-tester.h"
#include "test/common/code-assembler-tester.h"

namespace v8 {
namespace internal {

using compiler::CodeAssemblerTester;
using compiler::FunctionTester;
using compiler::Node;

namespace {

void TestStubCacheOffsetCalculation(StubCache::Table table) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int kNumParams = 2;
  CodeAssemblerTester data(isolate, JSParameterCount(kNumParams));
  AccessorAssembler m(data.state());

  {
    auto name = m.Parameter<Name>(1);
    auto map = m.Parameter<Map>(2);
    TNode<IntPtrT> primary_offset =
        m.StubCachePrimaryOffsetForTesting(name, map);
    TNode<IntPtrT> result;
    if (table == StubCache::kPrimary) {
      result = primary_offset;
    } else {
      CHECK_EQ(StubCache::kSecondary, table);
      result = m.StubCacheSecondaryOffsetForTesting(name, map);
    }
    m.Return(m.SmiTag(result));
  }

  Handle<Code> code = data.GenerateCode();
  FunctionTester ft(code, kNumParams);

  Factory* factory = isolate->factory();
  Handle<Name> names[] = {
      factory->NewSymbol(),
      factory->InternalizeUtf8String("a"),
      factory->InternalizeUtf8String("bb"),
      factory->InternalizeUtf8String("ccc"),
      factory->NewPrivateSymbol(),
      factory->InternalizeUtf8String("dddd"),
      factory->InternalizeUtf8String("eeeee"),
      factory->InternalizeUtf8String("name"),
      factory->NewSymbol(),
      factory->NewPrivateSymbol(),
  };

  Handle<Map> maps[] = {
      factory->cell_map(),     Map::Create(isolate, 0),
      factory->meta_map(),     factory->instruction_stream_map(),
      Map::Create(isolate, 0), factory->hash_table_map(),
      factory->symbol_map(),   factory->seq_two_byte_string_map(),
      Map::Create(isolate, 0), factory->sloppy_arguments_elements_map(),
  };

  for (size_t name_index = 0; name_index < arraysize(names); name_index++) {
    Handle<Name> name = names[name_index];
    for (size_t map_index = 0; map_index < arraysize(maps); map_index++) {
      Handle<Map> map = maps[map_index];

      int expected_result;
      {
        int primary_offset = StubCache::PrimaryOffsetForTesting(*name, *map);
        if (table == StubCache::kPrimary) {
          expected_result = primary_offset;
        } else {
          expected_result = StubCache::SecondaryOffsetForTesting(*name, *map);
        }
      }
      DirectHandle<Object> result = ft.Call(name, map).ToHandleChecked();

      Tagged<Smi> expected = Smi::FromInt(expected_result & Smi::kMaxValue);
      CHECK_EQ(expected, Cast<Smi>(*result));
    }
  }
}

}  // namespace

TEST(StubCachePrimaryOffset) {
  TestStubCacheOffsetCalculation(StubCache::kPrimary);
}

TEST(StubCacheSecondaryOffset) {
  TestStubCacheOffsetCalculation(StubCache::kSecondary);
}

namespace {

Handle<Code> CreateCodeOfKind(CodeKind kind) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester data(isolate, kind);
  CodeStubAssembler m(data.state());
  m.Return(m.UndefinedConstant());
  return data.GenerateCodeCloseAndEscape();
}

}  // namespace

TEST(TryProbeStubCache) {
  using Label = CodeStubAssembler::Label;
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int kNumParams = 3;
  CodeAssemblerTester data(isolate, JSParameterCount(kNumParams));
  AccessorAssembler m(data.state());

  StubCache stub_cache(isolate);
  stub_cache.Clear();

  {
    auto receiver = m.Parameter<Object>(1);
    auto name = m.Parameter<Name>(2);
    TNode<MaybeObject> expected_handler = m.UncheckedParameter<MaybeObject>(3);

    Label passed(&m), failed(&m);

    CodeStubAssembler::TVariable<MaybeObject> var_handler(&m);
    Label if_handler(&m), if_miss(&m);

    m.TryProbeStubCache(&stub_cache, receiver, name, &if_handler, &var_handler,
                        &if_miss);
    m.BIND(&if_handler);
    m.Branch(m.TaggedEqual(expected_handler, var_handler.value()), &passed,
             &failed);

    m.BIND(&if_miss);
    m.Branch(m.TaggedEqual(expected_handler, m.SmiConstant(0)), &passed,
             &failed);

    m.BIND(&passed);
    m.Return(m.BooleanConstant(true));

    m.BIND(&failed);
    m.Return(m.BooleanConstant(false));
  }

  Handle<Code> code = data.GenerateCode();
  FunctionTester ft(code, kNumParams);

  std::vector<Handle<Name>> names;
  std::vector<Handle<JSObject>> receivers;
  std::vector<Handle<Code>> handlers;

  base::RandomNumberGenerator rand_gen(v8_flags.random_seed);

  Factory* factory = isolate->factory();

  // Generate some number of names.
  for (int i = 0; i < StubCache::kPrimaryTableSize / 7; i++) {
    Handle<Name> name;
    switch (rand_gen.NextInt(3)) {
      case 0: {
        // Generate string.
        std::stringstream ss;
        ss << "s" << std::hex
           << (rand_gen.NextInt(Smi::kMaxValue) % StubCache::kPrimaryTableSize);
        name = factory->InternalizeUtf8String(ss.str().c_str());
        break;
      }
      case 1: {
        // Generate number string.
        std::stringstream ss;
        ss << (rand_gen.NextInt(Smi::kMaxValue) % StubCache::kPrimaryTableSize);
        name = factory->InternalizeUtf8String(ss.str().c_str());
        break;
      }
      case 2: {
        // Generate symbol.
        name = factory->NewSymbol();
        break;
      }
      default:
        UNREACHABLE();
    }
    names.push_back(name);
  }

  // Generate some number of receiver maps and receivers.
  for (int i = 0; i < StubCache::kSecondaryTableSize / 2; i++) {
    DirectHandle<Map> map = Map::Create(isolate, 0);
    receivers.push_back(factory->NewJSObjectFromMap(map));
  }

  // Generate some number of handlers.
  for (int i = 0; i < 30; i++) {
    handlers.push_back(CreateCodeOfKind(CodeKind::FOR_TESTING));
  }

  // Ensure that GC does happen because from now on we are going to fill our
  // own stub cache instance with raw values.
  DisallowGarbageCollection no_gc;

  // Populate {stub_cache}.
  const int N = StubCache::kPrimaryTableSize + StubCache::kSecondaryTableSize;
  for (int i = 0; i < N; i++) {
    int index = rand_gen.NextInt();
    DirectHandle<Name> name = names[index % names.size()];
    DirectHandle<JSObject> receiver = receivers[index % receivers.size()];
    DirectHandle<Code> handler = handlers[index % handlers.size()];
    stub_cache.Set(*name, receiver->map(), *handler);
  }

  // Perform some queries.
  bool queried_existing = false;
  bool queried_non_existing = false;
  for (int i = 0; i < N; i++) {
    int index = rand_gen.NextInt();
    Handle<Name> name = names[index % names.size()];
    Handle<JSObject> receiver = receivers[index % receivers.size()];
    Tagged<MaybeObject> handler = stub_cache.Get(*name, receiver->map());
    if (handler.ptr() == kNullAddress) {
      queried_non_existing = true;
    } else {
      queried_existing = true;
    }

    Handle<Object> expected_handler(handler.GetHeapObjectOrSmi(), isolate);
    ft.CheckTrue(receiver, name, expected_handler);
  }

  for (int i = 0; i < N; i++) {
    int index1 = rand_gen.NextInt();
    int index2 = rand_gen.NextInt();
    Handle<Name> name = names[index1 % names.size()];
    Handle<JSObject> receiver = receivers[index2 % receivers.size()];
    Tagged<MaybeObject> handler = stub_cache.Get(*name, receiver->map());
    if (handler.ptr() == kNullAddress) {
      queried_non_existing = true;
    } else {
      queried_existing = true;
    }

    Handle<Object> expected_handler(handler.GetHeapObjectOrSmi(), isolate);
    ft.CheckTrue(receiver, name, expected_handler);
  }
  // Ensure we performed both kind of queries.
  CHECK(queried_existing && queried_non_existing);
}

}  // namespace internal
}  // namespace v8
