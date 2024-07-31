// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/logging.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/common/globals.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/property-details.h"
#include "src/objects/string-inl.h"
#include "src/objects/transitions-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/function-tester.h"
#include "test/cctest/test-transitions.h"
#include "test/common/code-assembler-tester.h"

namespace v8 {
namespace internal {

namespace {

using Label = compiler::CodeAssemblerLabel;
template <class T>
using TVariable = compiler::TypedCodeAssemblerVariable<T>;

Handle<Name> NewNameWithHash(Isolate* isolate, const char* str, uint32_t hash,
                             bool is_integer) {
  uint32_t hash_field = Name::CreateHashFieldValue(
      hash, is_integer ? Name::HashFieldType::kIntegerIndex
                       : Name::HashFieldType::kHash);

  Handle<Name> name = isolate->factory()->NewOneByteInternalizedString(
      base::OneByteVector(str), hash_field);
  name->set_raw_hash_field(hash_field);
  CHECK(IsUniqueName(*name));
  return name;
}

template <typename... Args>
MaybeHandle<Object> Call(Isolate* isolate, Handle<JSFunction> function,
                         Args... args) {
  const int nof_args = sizeof...(Args);
  Handle<Object> call_args[] = {args...};
  Handle<Object> receiver = isolate->factory()->undefined_value();
  return Execution::Call(isolate, function, receiver, nof_args, call_args);
}

void CheckDescriptorArrayLookups(Isolate* isolate, Handle<Map> map,
                                 std::vector<Handle<Name>>& names,
                                 Handle<JSFunction> csa_lookup) {
  // Test C++ implementation.
  {
    DisallowGarbageCollection no_gc;
    Tagged<DescriptorArray> descriptors = map->instance_descriptors(isolate);
    DCHECK(descriptors->IsSortedNoDuplicates());
    int nof_descriptors = descriptors->number_of_descriptors();

    for (size_t i = 0; i < names.size(); ++i) {
      Tagged<Name> name = *names[i];
      InternalIndex index = descriptors->Search(name, nof_descriptors, false);
      CHECK(index.is_found());
      CHECK_EQ(i, index.as_uint32());
    }
  }

  // Test CSA implementation.
  if (!v8_flags.jitless) {
    for (size_t i = 0; i < names.size(); ++i) {
      Handle<Object> name_index =
          Call(isolate, csa_lookup, map, names[i]).ToHandleChecked();
      CHECK(IsSmi(*name_index));
      CHECK_EQ(DescriptorArray::ToKeyIndex(static_cast<int>(i)),
               Smi::ToInt(*name_index));
    }
  }
}

void CheckTransitionArrayLookups(Isolate* isolate,
                                 Handle<TransitionArray> transitions,
                                 std::vector<Handle<Map>>& maps,
                                 Handle<JSFunction> csa_lookup) {
  // Test C++ implementation.
  {
    DisallowGarbageCollection no_gc;
    DCHECK(transitions->IsSortedNoDuplicates());

    for (size_t i = 0; i < maps.size(); ++i) {
      Tagged<Map> expected_map = *maps[i];
      Tagged<Name> name = expected_map->instance_descriptors(isolate)->GetKey(
          expected_map->LastAdded());

      Tagged<Map> map = transitions->SearchAndGetTargetForTesting(
          PropertyKind::kData, name, NONE);
      CHECK(!map.is_null());
      CHECK_EQ(expected_map, map);
    }
  }

  // Test CSA implementation.
  if (!v8_flags.jitless) {
    for (size_t i = 0; i < maps.size(); ++i) {
      Handle<Map> expected_map = maps[i];
      Handle<Name> name(expected_map->instance_descriptors(isolate)->GetKey(
                            expected_map->LastAdded()),
                        isolate);

      Handle<Object> transition_map =
          Call(isolate, csa_lookup, transitions, name).ToHandleChecked();
      CHECK(IsMap(*transition_map));
      CHECK_EQ(*expected_map, *transition_map);
    }
  }
}

// Creates function with (Map, Name) arguments. Returns Smi with the index of
// the name value of the found descriptor (DescriptorArray::ToKeyIndex())
// or null otherwise.
Handle<JSFunction> CreateCsaDescriptorArrayLookup(Isolate* isolate) {
  // We are not allowed to generate code in jitless mode.
  if (v8_flags.jitless) return Handle<JSFunction>();

  // Preallocate handle for the result in the current handle scope.
  Handle<JSFunction> result_function(JSFunction{}, isolate);

  const int kNumParams = 2;

  compiler::CodeAssemblerTester asm_tester(
      isolate, JSParameterCount(kNumParams), CodeKind::FOR_TESTING);
  {
    CodeStubAssembler m(asm_tester.state());

    auto map = m.Parameter<Map>(1);
    auto unique_name = m.Parameter<Name>(2);

    Label passed(&m), failed(&m);
    Label if_found(&m), if_not_found(&m);
    TVariable<IntPtrT> var_name_index(&m);

    TNode<Uint32T> bit_field3 = m.LoadMapBitField3(map);
    TNode<DescriptorArray> descriptors = m.LoadMapDescriptors(map);

    m.DescriptorLookup(unique_name, descriptors, bit_field3, &if_found,
                       &var_name_index, &if_not_found);

    m.BIND(&if_found);
    m.Return(m.SmiTag(var_name_index.value()));

    m.BIND(&if_not_found);
    m.Return(m.NullConstant());
  }

  {
    compiler::FunctionTester ft(asm_tester.GenerateCode(), kNumParams);
    // Copy function value to a handle created in the outer handle scope.
    result_function.PatchValue(*ft.function);
  }

  return result_function;
}

// Creates function with (TransitionArray, Name) arguments. Returns transition
// map if transition is found or null otherwise.
Handle<JSFunction> CreateCsaTransitionArrayLookup(Isolate* isolate) {
  // We are not allowed to generate code in jitless mode.
  if (v8_flags.jitless) return Handle<JSFunction>();

  // Preallocate handle for the result in the current handle scope.
  Handle<JSFunction> result_function(JSFunction{}, isolate);

  const int kNumParams = 2;
  compiler::CodeAssemblerTester asm_tester(
      isolate, JSParameterCount(kNumParams), CodeKind::FOR_TESTING);
  {
    CodeStubAssembler m(asm_tester.state());

    auto transitions = m.Parameter<TransitionArray>(1);
    auto unique_name = m.Parameter<Name>(2);

    Label passed(&m), failed(&m);
    Label if_found(&m), if_not_found(&m);
    TVariable<IntPtrT> var_name_index(&m);

    m.TransitionLookup(unique_name, transitions, &if_found, &var_name_index,
                       &if_not_found);

    m.BIND(&if_found);
    {
      static_assert(static_cast<int>(PropertyKind::kData) == 0);
      static_assert(NONE == 0);
      const int kKeyToTargetOffset = (TransitionArray::kEntryTargetIndex -
                                      TransitionArray::kEntryKeyIndex) *
                                     kTaggedSize;
      TNode<Map> transition_map = m.CAST(m.GetHeapObjectAssumeWeak(
          m.LoadArrayElement(transitions, WeakFixedArray::kHeaderSize,
                             var_name_index.value(), kKeyToTargetOffset)));
      m.Return(transition_map);
    }

    m.BIND(&if_not_found);
    m.Return(m.NullConstant());
  }

  {
    compiler::FunctionTester ft(asm_tester.GenerateCode(), kNumParams);
    // Copy function value to a handle created in the outer handle scope.
    result_function.PatchValue(*ft.function);
  }

  return result_function;
}

}  // namespace

TEST(DescriptorArrayHashCollisionMassive) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);

  static_assert(Name::HashFieldTypeBits::kSize == 2,
                "This test might require updating if more HashFieldType values "
                "are introduced");

  std::vector<Handle<Name>> names;

  // Use the same hash value for all names.
  uint32_t hash = static_cast<uint32_t>(
      isolate->GenerateIdentityHash(Name::HashBits::kMax));

  for (int i = 0; i < kMaxNumberOfDescriptors / 2; ++i) {
    // Add pairs of names having the same base hash value but having different
    // values of is_integer bit.
    bool first_is_integer = (i & 1) != 0;
    bool second_is_integer = (i & 2) != 0;

    names.push_back(NewNameWithHash(isolate, "a", hash, first_is_integer));
    names.push_back(NewNameWithHash(isolate, "b", hash, second_is_integer));
  }

  // Create descriptor array with the created names by appending fields to some
  // map. DescriptorArray marking relies on the fact that it's attached to an
  // owning map.
  Handle<Map> map = Map::Create(isolate, 0);

  Handle<FieldType> any_type = FieldType::Any(isolate);

  for (size_t i = 0; i < names.size(); ++i) {
    map = Map::CopyWithField(isolate, map, names[i], any_type, NONE,
                             PropertyConstness::kMutable,
                             Representation::Tagged(), OMIT_TRANSITION)
              .ToHandleChecked();
  }

  Handle<JSFunction> csa_lookup = CreateCsaDescriptorArrayLookup(isolate);

  CheckDescriptorArrayLookups(isolate, map, names, csa_lookup);

  // Sort descriptor array and check it again.
  map->instance_descriptors(isolate)->Sort();
  CheckDescriptorArrayLookups(isolate, map, names, csa_lookup);
}

TEST(DescriptorArrayHashCollision) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);

  static_assert(Name::HashFieldTypeBits::kSize == 2,
                "This test might require updating if more HashFieldType values "
                "are introduced");

  std::vector<Handle<Name>> names;
  uint32_t hash = 0;

  for (int i = 0; i < kMaxNumberOfDescriptors / 2; ++i) {
    if (i % 2 == 0) {
      // Change hash value for every pair of names.
      hash = static_cast<uint32_t>(
          isolate->GenerateIdentityHash(Name::HashBits::kMax));
    }

    // Add pairs of names having the same base hash value but having different
    // values of is_integer bit.
    bool first_is_integer = (i & 1) != 0;
    bool second_is_integer = (i & 2) != 0;

    names.push_back(NewNameWithHash(isolate, "a", hash, first_is_integer));
    names.push_back(NewNameWithHash(isolate, "b", hash, second_is_integer));
  }

  // Create descriptor array with the created names by appending fields to some
  // map. DescriptorArray marking relies on the fact that it's attached to an
  // owning map.
  Handle<Map> map = Map::Create(isolate, 0);

  Handle<FieldType> any_type = FieldType::Any(isolate);

  for (size_t i = 0; i < names.size(); ++i) {
    map = Map::CopyWithField(isolate, map, names[i], any_type, NONE,
                             PropertyConstness::kMutable,
                             Representation::Tagged(), OMIT_TRANSITION)
              .ToHandleChecked();
  }

  Handle<JSFunction> csa_lookup = CreateCsaDescriptorArrayLookup(isolate);

  CheckDescriptorArrayLookups(isolate, map, names, csa_lookup);

  // Sort descriptor array and check it again.
  map->instance_descriptors(isolate)->Sort();
  CheckDescriptorArrayLookups(isolate, map, names, csa_lookup);
}

TEST(TransitionArrayHashCollisionMassive) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);

  static_assert(Name::HashFieldTypeBits::kSize == 2,
                "This test might require updating if more HashFieldType values "
                "are introduced");

  std::vector<Handle<Name>> names;

  // Use the same hash value for all names.
  uint32_t hash = static_cast<uint32_t>(
      isolate->GenerateIdentityHash(Name::HashBits::kMax));

  for (int i = 0; i < TransitionsAccessor::kMaxNumberOfTransitions / 2; ++i) {
    // Add pairs of names having the same base hash value but having different
    // values of is_integer bit.
    bool first_is_integer = (i & 1) != 0;
    bool second_is_integer = (i & 2) != 0;

    names.push_back(NewNameWithHash(isolate, "a", hash, first_is_integer));
    names.push_back(NewNameWithHash(isolate, "b", hash, second_is_integer));
  }

  // Create transitions for each name.
  Handle<Map> root_map = Map::Create(isolate, 0);

  std::vector<Handle<Map>> maps;

  Handle<FieldType> any_type = FieldType::Any(isolate);

  for (size_t i = 0; i < names.size(); ++i) {
    Handle<Map> map =
        Map::CopyWithField(isolate, root_map, names[i], any_type, NONE,
                           PropertyConstness::kMutable,
                           Representation::Tagged(), INSERT_TRANSITION)
            .ToHandleChecked();
    maps.push_back(map);
  }

  Handle<JSFunction> csa_lookup = CreateCsaTransitionArrayLookup(isolate);

  Handle<TransitionArray> transition_array(
      TestTransitionsAccessor(isolate, root_map).transitions(), isolate);

  CheckTransitionArrayLookups(isolate, transition_array, maps, csa_lookup);

  // Sort transition array and check it again.
  transition_array->Sort();
  CheckTransitionArrayLookups(isolate, transition_array, maps, csa_lookup);
}

TEST(TransitionArrayHashCollision) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);

  static_assert(Name::HashFieldTypeBits::kSize == 2,
                "This test might require updating if more HashFieldType values "
                "are introduced");

  std::vector<Handle<Name>> names;

  // Use the same hash value for all names.
  uint32_t hash = static_cast<uint32_t>(
      isolate->GenerateIdentityHash(Name::HashBits::kMax));

  for (int i = 0; i < TransitionsAccessor::kMaxNumberOfTransitions / 2; ++i) {
    if (i % 2 == 0) {
      // Change hash value for every pair of names.
      hash = static_cast<uint32_t>(
          isolate->GenerateIdentityHash(Name::HashBits::kMax));
    }
    // Add pairs of names having the same base hash value but having different
    // values of is_integer bit.
    bool first_is_integer = (i & 1) != 0;
    bool second_is_integer = (i & 2) != 0;

    names.push_back(NewNameWithHash(isolate, "a", hash, first_is_integer));
    names.push_back(NewNameWithHash(isolate, "b", hash, second_is_integer));
  }

  // Create transitions for each name.
  Handle<Map> root_map = Map::Create(isolate, 0);

  std::vector<Handle<Map>> maps;

  Handle<FieldType> any_type = FieldType::Any(isolate);

  for (size_t i = 0; i < names.size(); ++i) {
    Handle<Map> map =
        Map::CopyWithField(isolate, root_map, names[i], any_type, NONE,
                           PropertyConstness::kMutable,
                           Representation::Tagged(), INSERT_TRANSITION)
            .ToHandleChecked();
    maps.push_back(map);
  }

  Handle<JSFunction> csa_lookup = CreateCsaTransitionArrayLookup(isolate);

  Handle<TransitionArray> transition_array(
      TestTransitionsAccessor(isolate, root_map).transitions(), isolate);

  CheckTransitionArrayLookups(isolate, transition_array, maps, csa_lookup);

  // Sort transition array and check it again.
  transition_array->Sort();
  CheckTransitionArrayLookups(isolate, transition_array, maps, csa_lookup);
}

}  // namespace internal
}  // namespace v8
