// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <iostream>
#include <limits>

#include "src/api/api-inl.h"
#include "src/codegen/compiler.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/string-set.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

namespace {

bool IsInStringInstanceTypeList(InstanceType instance_type) {
  switch (instance_type) {
#define ASSERT_INSTANCE_TYPE(type, ...) \
  static_assert(InstanceType::type < InstanceType::FIRST_NONSTRING_TYPE);
    STRING_TYPE_LIST(ASSERT_INSTANCE_TYPE)
#undef ASSERT_INSTANCE_TYPE
#define TEST_INSTANCE_TYPE(type, ...) case InstanceType::type:
    STRING_TYPE_LIST(TEST_INSTANCE_TYPE)
#undef TEST_INSTANCE_TYPE
    return true;
    default:
      EXPECT_LE(InstanceType::FIRST_NONSTRING_TYPE, instance_type);
      return false;
  }
}

void CheckOneInstanceType(InstanceType instance_type) {
  if (IsInStringInstanceTypeList(instance_type)) {
    EXPECT_TRUE((instance_type & kIsNotStringMask) == kStringTag)
        << "Failing IsString mask check for " << instance_type;
  } else {
    EXPECT_FALSE((instance_type & kIsNotStringMask) == kStringTag)
        << "Failing !IsString mask check for " << instance_type;
  }
}

}  // namespace

TEST(Object, InstanceTypeList) {
#define TEST_INSTANCE_TYPE(type) CheckOneInstanceType(InstanceType::type);

  INSTANCE_TYPE_LIST(TEST_INSTANCE_TYPE)
#undef TEST_INSTANCE_TYPE
}

TEST(Object, InstanceTypeListOrder) {
  int current = 0;
  int prev = -1;
  InstanceType current_type = static_cast<InstanceType>(current);
  EXPECT_EQ(current_type, InstanceType::FIRST_TYPE);
  EXPECT_EQ(current_type, InstanceType::INTERNALIZED_TWO_BYTE_STRING_TYPE);
#define TEST_INSTANCE_TYPE(type)                                           \
  current_type = InstanceType::type;                                       \
  current = static_cast<int>(current_type);                                \
  if (current > static_cast<int>(LAST_NAME_TYPE)) {                        \
    EXPECT_LE(prev + 1, current);                                          \
  }                                                                        \
  EXPECT_LT(prev, current) << " INSTANCE_TYPE_LIST is not ordered: "       \
                           << "last = " << static_cast<InstanceType>(prev) \
                           << " vs. current = " << current_type;           \
  prev = current;

  // Only test hand-written portion of instance type list. The generated portion
  // doesn't run the same risk of getting out of order, and it does emit type
  // names out of numerical order in one case: JS_OBJECT_TYPE is emitted before
  // its subclass types, because types are emitted in depth-first pre-order
  // traversal order, and some of its subclass types are numerically earlier.
  INSTANCE_TYPE_LIST_BASE(TEST_INSTANCE_TYPE)
#undef TEST_INSTANCE_TYPE
}

TEST(Object, StructListOrder) {
  int current = static_cast<int>(InstanceType::FIRST_STRUCT_TYPE);
  int prev = current - 1;
  ASSERT_LT(0, prev);
  InstanceType current_type = static_cast<InstanceType>(current);
#define TEST_STRUCT(TYPE, class, name)                 \
  current_type = InstanceType::TYPE;                   \
  current = static_cast<int>(current_type);            \
  EXPECT_LE(prev + 1, current)                         \
      << " STRUCT_LIST is not ordered: "               \
      << " last = " << static_cast<InstanceType>(prev) \
      << " vs. current = " << current_type;            \
  prev = current;

  STRUCT_LIST_GENERATOR(STRUCT_LIST_ADAPTER, TEST_STRUCT)
#undef TEST_STRUCT
}

using ObjectWithIsolate = TestWithIsolate;

TEST_F(ObjectWithIsolate, DictionaryGrowth) {
  Handle<NumberDictionary> dict = NumberDictionary::New(isolate(), 1);
  DirectHandle<Object> value = isolate()->factory()->null_value();
  PropertyDetails details = PropertyDetails::Empty();

  // This test documents the expected growth behavior of a dictionary getting
  // elements added to it one by one.
  static_assert(HashTableBase::kMinCapacity == 4);
  uint32_t i = 1;
  // 3 elements fit into the initial capacity.
  for (; i <= 3; i++) {
    dict = NumberDictionary::Add(isolate(), dict, i, value, details);
    CHECK_EQ(4, dict->Capacity());
  }
  // 4th element triggers growth.
  DCHECK_EQ(4, i);
  for (; i <= 5; i++) {
    dict = NumberDictionary::Add(isolate(), dict, i, value, details);
    CHECK_EQ(8, dict->Capacity());
  }
  // 6th element triggers growth.
  DCHECK_EQ(6, i);
  for (; i <= 11; i++) {
    dict = NumberDictionary::Add(isolate(), dict, i, value, details);
    CHECK_EQ(16, dict->Capacity());
  }
  // 12th element triggers growth.
  DCHECK_EQ(12, i);
  for (; i <= 21; i++) {
    dict = NumberDictionary::Add(isolate(), dict, i, value, details);
    CHECK_EQ(32, dict->Capacity());
  }
  // 22nd element triggers growth.
  DCHECK_EQ(22, i);
  for (; i <= 43; i++) {
    dict = NumberDictionary::Add(isolate(), dict, i, value, details);
    CHECK_EQ(64, dict->Capacity());
  }
  // 44th element triggers growth.
  DCHECK_EQ(44, i);
  for (; i <= 50; i++) {
    dict = NumberDictionary::Add(isolate(), dict, i, value, details);
    CHECK_EQ(128, dict->Capacity());
  }

  // If we grow by larger chunks, the next (sufficiently big) power of 2 is
  // chosen as the capacity.
  dict = NumberDictionary::New(isolate(), 1);
  dict = NumberDictionary::EnsureCapacity(isolate(), dict, 65);
  CHECK_EQ(128, dict->Capacity());

  dict = NumberDictionary::New(isolate(), 1);
  dict = NumberDictionary::EnsureCapacity(isolate(), dict, 30);
  CHECK_EQ(64, dict->Capacity());
}

TEST_F(TestWithNativeContext, EmptyFunctionScopeInfo) {
  // Check that the empty_function has a properly set up ScopeInfo.
  DirectHandle<JSFunction> function = RunJS<JSFunction>("(function(){})");

  DirectHandle<ScopeInfo> scope_info(function->shared()->scope_info(),
                                     function->GetIsolate());
  DirectHandle<ScopeInfo> empty_function_scope_info(
      isolate()->empty_function()->shared()->scope_info(),
      function->GetIsolate());

  EXPECT_EQ(scope_info->Flags(), empty_function_scope_info->Flags());
  EXPECT_EQ(scope_info->ParameterCount(),
            empty_function_scope_info->ParameterCount());
  EXPECT_EQ(scope_info->ContextLocalCount(),
            empty_function_scope_info->ContextLocalCount());
}

using ObjectTest = TestWithContext;

static void CheckObject(Isolate* isolate, DirectHandle<Object> obj,
                        const char* string) {
  DirectHandle<String> print_string = String::Flatten(
      isolate,
      indirect_handle(Object::NoSideEffectsToString(isolate, obj), isolate));
  CHECK(print_string->IsOneByteEqualTo(base::CStrVector(string)));
}

static void CheckSmi(Isolate* isolate, int value, const char* string) {
  DirectHandle<Object> handle(Smi::FromInt(value), isolate);
  CheckObject(isolate, handle, string);
}

static void CheckString(Isolate* isolate, const char* value,
                        const char* string) {
  DirectHandle<String> handle(
      isolate->factory()->NewStringFromAsciiChecked(value));
  CheckObject(isolate, handle, string);
}

static void CheckNumber(Isolate* isolate, double value, const char* string) {
  DirectHandle<Object> number = isolate->factory()->NewNumber(value);
  CHECK(IsNumber(*number));
  CheckObject(isolate, number, string);
}

static void CheckBoolean(Isolate* isolate, bool value, const char* string) {
  CheckObject(isolate, isolate->factory()->ToBoolean(value), string);
}

TEST_F(ObjectTest, NoSideEffectsToString) {
  Factory* factory = i_isolate()->factory();

  HandleScope scope(i_isolate());

  CheckString(i_isolate(), "fisk hest", "fisk hest");
  CheckNumber(i_isolate(), 42.3, "42.3");
  CheckSmi(i_isolate(), 42, "42");
  CheckBoolean(i_isolate(), true, "true");
  CheckBoolean(i_isolate(), false, "false");
  CheckBoolean(i_isolate(), false, "false");
  DirectHandle<Object> smi_42(Smi::FromInt(42), i_isolate());
  CheckObject(i_isolate(),
              BigInt::FromNumber(i_isolate(), smi_42).ToHandleChecked(), "42");
  CheckObject(i_isolate(), factory->undefined_value(), "undefined");
  CheckObject(i_isolate(), factory->null_value(), "null");

  CheckObject(i_isolate(), factory->error_to_string(), "[object Error]");
  CheckObject(i_isolate(), factory->unscopables_symbol(),
              "Symbol(Symbol.unscopables)");
  CheckObject(
      i_isolate(),
      factory->NewError(i_isolate()->error_function(), factory->empty_string()),
      "Error");
  CheckObject(
      i_isolate(),
      factory->NewError(i_isolate()->error_function(),
                        factory->NewStringFromAsciiChecked("fisk hest")),
      "Error: fisk hest");
  CheckObject(i_isolate(), factory->NewJSObject(i_isolate()->object_function()),
              "#<Object>");
  CheckObject(
      i_isolate(),
      factory->NewJSProxy(factory->NewJSObject(i_isolate()->object_function()),
                          factory->NewJSObject(i_isolate()->object_function())),
      "#<Object>");
}

TEST_F(ObjectTest, EnumCache) {
  i::Factory* factory = i_isolate()->factory();
  v8::HandleScope scope(isolate());

  // Create a nice transition tree:
  // (a) --> (b) --> (c)   shared DescriptorArray 1
  //          |
  //          +---> (cc)   shared DescriptorArray 2
  RunJS(
      "function O(a) { this.a = 1 };"

      "a = new O();"

      "b = new O();"
      "b.b = 2;"

      "c = new O();"
      "c.b = 2;"
      "c.c = 3;"

      "cc = new O();"
      "cc.b = 2;"
      "cc.cc = 4;");

  DirectHandle<JSObject> a = Cast<JSObject>(v8::Utils::OpenDirectHandle(
      *context()->Global()->Get(context(), NewString("a")).ToLocalChecked()));
  DirectHandle<JSObject> b = Cast<JSObject>(v8::Utils::OpenDirectHandle(
      *context()->Global()->Get(context(), NewString("b")).ToLocalChecked()));
  DirectHandle<JSObject> c = Cast<JSObject>(v8::Utils::OpenDirectHandle(
      *context()->Global()->Get(context(), NewString("c")).ToLocalChecked()));
  DirectHandle<JSObject> cc = Cast<JSObject>(v8::Utils::OpenDirectHandle(
      *context()->Global()->Get(context(), NewString("cc")).ToLocalChecked()));

  // Check the transition tree.
  CHECK_EQ(a->map()->instance_descriptors(), b->map()->instance_descriptors());
  CHECK_EQ(b->map()->instance_descriptors(), c->map()->instance_descriptors());
  CHECK_NE(c->map()->instance_descriptors(), cc->map()->instance_descriptors());
  CHECK_NE(b->map()->instance_descriptors(), cc->map()->instance_descriptors());

  // Check that the EnumLength is unset.
  CHECK_EQ(a->map()->EnumLength(), kInvalidEnumCacheSentinel);
  CHECK_EQ(b->map()->EnumLength(), kInvalidEnumCacheSentinel);
  CHECK_EQ(c->map()->EnumLength(), kInvalidEnumCacheSentinel);
  CHECK_EQ(cc->map()->EnumLength(), kInvalidEnumCacheSentinel);

  // Check that the EnumCache is empty.
  CHECK_EQ(a->map()->instance_descriptors()->enum_cache(),
           *factory->empty_enum_cache());
  CHECK_EQ(b->map()->instance_descriptors()->enum_cache(),
           *factory->empty_enum_cache());
  CHECK_EQ(c->map()->instance_descriptors()->enum_cache(),
           *factory->empty_enum_cache());
  CHECK_EQ(cc->map()->instance_descriptors()->enum_cache(),
           *factory->empty_enum_cache());

  // The EnumCache is shared on the DescriptorArray, creating it on {cc} has no
  // effect on the other maps.
  RunJS("var s = 0; for (let key in cc) { s += cc[key] };");
  {
    CHECK_EQ(a->map()->EnumLength(), kInvalidEnumCacheSentinel);
    CHECK_EQ(b->map()->EnumLength(), kInvalidEnumCacheSentinel);
    CHECK_EQ(c->map()->EnumLength(), kInvalidEnumCacheSentinel);
    CHECK_EQ(cc->map()->EnumLength(), 3);

    CHECK_EQ(a->map()->instance_descriptors()->enum_cache(),
             *factory->empty_enum_cache());
    CHECK_EQ(b->map()->instance_descriptors()->enum_cache(),
             *factory->empty_enum_cache());
    CHECK_EQ(c->map()->instance_descriptors()->enum_cache(),
             *factory->empty_enum_cache());

    Tagged<EnumCache> enum_cache =
        cc->map()->instance_descriptors()->enum_cache();
    CHECK_NE(enum_cache, *factory->empty_enum_cache());
    CHECK_EQ(enum_cache->keys()->length(), 3);
    CHECK_EQ(enum_cache->indices()->length(), 3);
  }

  // Initializing the EnumCache for the the topmost map {a} will not create the
  // cache for the other maps.
  RunJS("var s = 0; for (let key in a) { s += a[key] };");
  {
    CHECK_EQ(a->map()->EnumLength(), 1);
    CHECK_EQ(b->map()->EnumLength(), kInvalidEnumCacheSentinel);
    CHECK_EQ(c->map()->EnumLength(), kInvalidEnumCacheSentinel);
    CHECK_EQ(cc->map()->EnumLength(), 3);

    // The enum cache is shared on the descriptor array of maps {a}, {b} and
    // {c} only.
    Tagged<EnumCache> enum_cache =
        a->map()->instance_descriptors()->enum_cache();
    CHECK_NE(enum_cache, *factory->empty_enum_cache());
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(),
             *factory->empty_enum_cache());
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_EQ(a->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_EQ(b->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_EQ(c->map()->instance_descriptors()->enum_cache(), enum_cache);

    CHECK_EQ(enum_cache->keys()->length(), 1);
    CHECK_EQ(enum_cache->indices()->length(), 1);
  }

  // Creating the EnumCache for {c} will create a new EnumCache on the shared
  // DescriptorArray.
  DirectHandle<EnumCache> previous_enum_cache(
      a->map()->instance_descriptors()->enum_cache(), a->GetIsolate());
  DirectHandle<FixedArray> previous_keys(previous_enum_cache->keys(),
                                         a->GetIsolate());
  DirectHandle<FixedArray> previous_indices(previous_enum_cache->indices(),
                                            a->GetIsolate());
  RunJS("var s = 0; for (let key in c) { s += c[key] };");
  {
    CHECK_EQ(a->map()->EnumLength(), 1);
    CHECK_EQ(b->map()->EnumLength(), kInvalidEnumCacheSentinel);
    CHECK_EQ(c->map()->EnumLength(), 3);
    CHECK_EQ(cc->map()->EnumLength(), 3);

    Tagged<EnumCache> enum_cache =
        c->map()->instance_descriptors()->enum_cache();
    CHECK_NE(enum_cache, *factory->empty_enum_cache());
    // The keys and indices caches are updated.
    CHECK_EQ(enum_cache, *previous_enum_cache);
    CHECK_NE(enum_cache->keys(), *previous_keys);
    CHECK_NE(enum_cache->indices(), *previous_indices);
    CHECK_EQ(previous_keys->length(), 1);
    CHECK_EQ(previous_indices->length(), 1);
    CHECK_EQ(enum_cache->keys()->length(), 3);
    CHECK_EQ(enum_cache->indices()->length(), 3);

    // The enum cache is shared on the descriptor array of maps {a}, {b} and
    // {c} only.
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(),
             *factory->empty_enum_cache());
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(),
             *previous_enum_cache);
    CHECK_EQ(a->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_EQ(b->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_EQ(c->map()->instance_descriptors()->enum_cache(), enum_cache);
  }

  // {b} can reuse the existing EnumCache, hence we only need to set the correct
  // EnumLength on the map without modifying the cache itself.
  previous_enum_cache = direct_handle(
      a->map()->instance_descriptors()->enum_cache(), a->GetIsolate());
  previous_keys = direct_handle(previous_enum_cache->keys(), a->GetIsolate());
  previous_indices =
      direct_handle(previous_enum_cache->indices(), a->GetIsolate());
  RunJS("var s = 0; for (let key in b) { s += b[key] };");
  {
    CHECK_EQ(a->map()->EnumLength(), 1);
    CHECK_EQ(b->map()->EnumLength(), 2);
    CHECK_EQ(c->map()->EnumLength(), 3);
    CHECK_EQ(cc->map()->EnumLength(), 3);

    Tagged<EnumCache> enum_cache =
        c->map()->instance_descriptors()->enum_cache();
    CHECK_NE(enum_cache, *factory->empty_enum_cache());
    // The keys and indices caches are not updated.
    CHECK_EQ(enum_cache, *previous_enum_cache);
    CHECK_EQ(enum_cache->keys(), *previous_keys);
    CHECK_EQ(enum_cache->indices(), *previous_indices);
    CHECK_EQ(enum_cache->keys()->length(), 3);
    CHECK_EQ(enum_cache->indices()->length(), 3);

    // The enum cache is shared on the descriptor array of maps {a}, {b} and
    // {c} only.
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(),
             *factory->empty_enum_cache());
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_NE(cc->map()->instance_descriptors()->enum_cache(),
             *previous_enum_cache);
    CHECK_EQ(a->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_EQ(b->map()->instance_descriptors()->enum_cache(), enum_cache);
    CHECK_EQ(c->map()->instance_descriptors()->enum_cache(), enum_cache);
  }
}

TEST_F(ObjectTest, ObjectMethodsThatTruncateMinusZero) {
  Factory* factory = i_isolate()->factory();

  Handle<Object> minus_zero = factory->NewNumber(-1.0 * 0.0);
  CHECK(IsMinusZero(*minus_zero));

  DirectHandle<Object> result =
      Object::ToInteger(i_isolate(), minus_zero).ToHandleChecked();
  CHECK(IsZero(*result));

  result = Object::ToLength(i_isolate(), minus_zero).ToHandleChecked();
  CHECK(IsZero(*result));

  // Choose an error message template, doesn't matter which.
  result = Object::ToIndex(i_isolate(), minus_zero,
                           MessageTemplate::kInvalidAtomicAccessIndex)
               .ToHandleChecked();
  CHECK(IsZero(*result));
}

#define TEST_FUNCTION_KIND(Name)                                            \
  TEST_F(ObjectTest, Name) {                                                \
    for (uint32_t i = 0;                                                    \
         i < static_cast<uint32_t>(FunctionKind::kLastFunctionKind); i++) { \
      FunctionKind kind = static_cast<FunctionKind>(i);                     \
      CHECK_EQ(FunctionKind##Name(kind), Name(kind));                       \
    }                                                                       \
  }

bool FunctionKindIsArrowFunction(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kArrowFunction:
    case FunctionKind::kAsyncArrowFunction:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsArrowFunction)

bool FunctionKindIsAsyncGeneratorFunction(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kAsyncConciseGeneratorMethod:
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
    case FunctionKind::kAsyncGeneratorFunction:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsAsyncGeneratorFunction)

bool FunctionKindIsGeneratorFunction(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kConciseGeneratorMethod:
    case FunctionKind::kStaticConciseGeneratorMethod:
    case FunctionKind::kAsyncConciseGeneratorMethod:
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
    case FunctionKind::kGeneratorFunction:
    case FunctionKind::kAsyncGeneratorFunction:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsGeneratorFunction)

bool FunctionKindIsAsyncFunction(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kAsyncFunction:
    case FunctionKind::kAsyncArrowFunction:
    case FunctionKind::kAsyncConciseMethod:
    case FunctionKind::kStaticAsyncConciseMethod:
    case FunctionKind::kAsyncConciseGeneratorMethod:
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
    case FunctionKind::kAsyncGeneratorFunction:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsAsyncFunction)

bool FunctionKindIsConciseMethod(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kConciseMethod:
    case FunctionKind::kStaticConciseMethod:
    case FunctionKind::kConciseGeneratorMethod:
    case FunctionKind::kStaticConciseGeneratorMethod:
    case FunctionKind::kAsyncConciseMethod:
    case FunctionKind::kStaticAsyncConciseMethod:
    case FunctionKind::kAsyncConciseGeneratorMethod:
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
    case FunctionKind::kClassMembersInitializerFunction:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsConciseMethod)

bool FunctionKindIsAccessorFunction(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kGetterFunction:
    case FunctionKind::kStaticGetterFunction:
    case FunctionKind::kSetterFunction:
    case FunctionKind::kStaticSetterFunction:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsAccessorFunction)

bool FunctionKindIsDefaultConstructor(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kDefaultBaseConstructor:
    case FunctionKind::kDefaultDerivedConstructor:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsDefaultConstructor)

bool FunctionKindIsBaseConstructor(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kBaseConstructor:
    case FunctionKind::kDefaultBaseConstructor:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsBaseConstructor)

bool FunctionKindIsDerivedConstructor(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kDefaultDerivedConstructor:
    case FunctionKind::kDerivedConstructor:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsDerivedConstructor)

bool FunctionKindIsClassConstructor(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kBaseConstructor:
    case FunctionKind::kDefaultBaseConstructor:
    case FunctionKind::kDefaultDerivedConstructor:
    case FunctionKind::kDerivedConstructor:
      return true;
    default:
      return false;
  }
}
TEST_FUNCTION_KIND(IsClassConstructor)

bool FunctionKindIsConstructable(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kGetterFunction:
    case FunctionKind::kStaticGetterFunction:
    case FunctionKind::kSetterFunction:
    case FunctionKind::kStaticSetterFunction:
    case FunctionKind::kArrowFunction:
    case FunctionKind::kAsyncArrowFunction:
    case FunctionKind::kAsyncFunction:
    case FunctionKind::kAsyncConciseMethod:
    case FunctionKind::kStaticAsyncConciseMethod:
    case FunctionKind::kAsyncConciseGeneratorMethod:
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
    case FunctionKind::kAsyncGeneratorFunction:
    case FunctionKind::kGeneratorFunction:
    case FunctionKind::kConciseGeneratorMethod:
    case FunctionKind::kStaticConciseGeneratorMethod:
    case FunctionKind::kConciseMethod:
    case FunctionKind::kStaticConciseMethod:
    case FunctionKind::kClassMembersInitializerFunction:
      return false;
    default:
      return true;
  }
}
TEST_FUNCTION_KIND(IsConstructable)

bool FunctionKindIsStrictFunctionWithoutPrototype(FunctionKind kind) {
  return IsArrowFunction(kind) || IsConciseMethod(kind) ||
         IsAccessorFunction(kind);
}
TEST_FUNCTION_KIND(IsStrictFunctionWithoutPrototype)

#undef TEST_FUNCTION_KIND

TEST_F(ObjectTest, ConstructorInstanceTypes) {
  bool flag_was_enabled = i::v8_flags.js_float16array;
  i::v8_flags.js_float16array = true;
  v8::HandleScope scope(isolate());

  DirectHandle<NativeContext> context = i_isolate()->native_context();

  DisallowGarbageCollection no_gc;
  for (int i = 0; i < Context::NATIVE_CONTEXT_SLOTS; i++) {
    Tagged<Object> value = context->get(i);
    if (!IsJSFunction(value)) continue;
    InstanceType instance_type =
        Cast<JSFunction>(value)->map()->instance_type();

    switch (i) {
      case Context::ARRAY_FUNCTION_INDEX:
        CHECK_EQ(instance_type, JS_ARRAY_CONSTRUCTOR_TYPE);
        break;
      case Context::REGEXP_FUNCTION_INDEX:
        CHECK_EQ(instance_type, JS_REG_EXP_CONSTRUCTOR_TYPE);
        break;
      case Context::PROMISE_FUNCTION_INDEX:
        CHECK_EQ(instance_type, JS_PROMISE_CONSTRUCTOR_TYPE);
        break;

#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype)                 \
  case Context::TYPE##_ARRAY_FUN_INDEX:                           \
    CHECK_EQ(instance_type, TYPE##_TYPED_ARRAY_CONSTRUCTOR_TYPE); \
    break;
        TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

      default:
        // All the other functions must have the default instance type.
        CHECK_EQ(instance_type, JS_FUNCTION_TYPE);
        break;
    }
  }
  i::v8_flags.js_float16array = flag_was_enabled;
}

TEST_F(ObjectTest, AddDataPropertyNameCollision) {
  v8::HandleScope scope(isolate());
  Factory* factory = i_isolate()->factory();

  DirectHandle<JSObject> object =
      factory->NewJSObject(i_isolate()->object_function());

  DirectHandle<String> key = factory->NewStringFromStaticChars("key_string");
  DirectHandle<Object> value1(Smi::FromInt(0), i_isolate());
  DirectHandle<Object> value2 = factory->NewStringFromAsciiChecked("corrupt");

  LookupIterator outer_it(i_isolate(), object, key, object,
                          LookupIterator::OWN_SKIP_INTERCEPTOR);
  {
    LookupIterator inner_it(i_isolate(), object, key, object,
                            LookupIterator::OWN_SKIP_INTERCEPTOR);

    CHECK(Object::AddDataProperty(&inner_it, value1, NONE,
                                  Just(ShouldThrow::kThrowOnError),
                                  StoreOrigin::kNamed)
              .IsJust());
  }
  EXPECT_DEATH_IF_SUPPORTED(
      Object::AddDataProperty(&outer_it, value2, NONE,
                              Just(ShouldThrow::kThrowOnError),
                              StoreOrigin::kNamed)
          .IsJust(),
      "");
}

TEST_F(ObjectTest, AddDataPropertyNameCollisionDeprecatedMap) {
  v8::HandleScope scope(isolate());
  Factory* factory = i_isolate()->factory();

  // Create two identical maps
  RunJS(
      "a = {'regular_prop':5};"
      "b = {'regular_prop':5};");

  DirectHandle<JSObject> a = Cast<JSObject>(v8::Utils::OpenHandle(
      *context()->Global()->Get(context(), NewString("a")).ToLocalChecked()));
  DirectHandle<JSObject> b = Cast<JSObject>(v8::Utils::OpenHandle(
      *context()->Global()->Get(context(), NewString("b")).ToLocalChecked()));

  CHECK(a->map() == b->map());

  DirectHandle<String> key =
      factory->NewStringFromStaticChars("corrupted_prop");
  DirectHandle<Object> value = factory->NewStringFromAsciiChecked("corrupt");
  LookupIterator it(i_isolate(), a, key, a,
                    LookupIterator::OWN_SKIP_INTERCEPTOR);

  // Transition `a`'s map to deprecated
  RunJS(
      "a.corrupted_prop = 1;"
      "b.regular_prop = 5.5;");

  CHECK(a->map()->is_deprecated());

  EXPECT_DEATH_IF_SUPPORTED(
      Object::AddDataProperty(&it, value, NONE,
                              Just(ShouldThrow::kThrowOnError),
                              StoreOrigin::kNamed)
          .IsJust(),
      "");
}

}  // namespace internal
}  // namespace v8
