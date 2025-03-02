// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/flags/flags.h"
#include "src/heap/read-only-spaces.h"
#include "test/cctest/cctest.h"
#include "tools/debug_helper/debug-helper.h"

namespace v8 {
namespace internal {

namespace {

namespace d = v8::debug_helper;

uintptr_t memory_fail_start = 0;
uintptr_t memory_fail_end = 0;

class MemoryFailureRegion {
 public:
  MemoryFailureRegion(uintptr_t start, uintptr_t end) {
    memory_fail_start = start;
    memory_fail_end = end;
  }
  ~MemoryFailureRegion() {
    memory_fail_start = 0;
    memory_fail_end = 0;
  }
};

// Implement the memory-reading callback. This one just fetches memory from the
// current process, but a real implementation for a debugging extension would
// fetch memory from the debuggee process or crash dump.
d::MemoryAccessResult ReadMemory(uintptr_t address, void* destination,
                                 size_t byte_count) {
  if (address >= memory_fail_start && address <= memory_fail_end) {
    // Simulate failure to read debuggee memory.
    return d::MemoryAccessResult::kAddressValidButInaccessible;
  }
  memcpy(destination, reinterpret_cast<void*>(address), byte_count);
  return d::MemoryAccessResult::kOk;
}

void CheckPropBase(const d::PropertyBase& property, const char* expected_type,
                   const char* expected_name) {
  CHECK(property.type == std::string("v8::internal::TaggedValue") ||
        property.type == std::string(expected_type));
  CHECK(property.name == std::string(expected_name));
}

void CheckProp(const d::ObjectProperty& property, const char* expected_type,
               const char* expected_name,
               d::PropertyKind expected_kind = d::PropertyKind::kSingle,
               size_t expected_num_values = 1) {
  CheckPropBase(property, expected_type, expected_name);
  CHECK_EQ(property.num_values, expected_num_values);
  CHECK(property.kind == expected_kind);
}

template <typename TValue>
void CheckProp(const d::ObjectProperty& property, const char* expected_type,
               const char* expected_name, TValue expected_value) {
  CheckProp(property, expected_type, expected_name);
  CHECK(*reinterpret_cast<TValue*>(property.address) == expected_value);
}

bool StartsWith(const std::string& full_string, const std::string& prefix) {
  return full_string.substr(0, prefix.size()) == prefix;
}

bool Contains(const std::string& full_string, const std::string& substr) {
  return full_string.find(substr) != std::string::npos;
}

void CheckStructProp(const d::StructProperty& property,
                     const char* expected_type, const char* expected_name,
                     size_t expected_offset, uint8_t expected_num_bits = 0,
                     uint8_t expected_shift_bits = 0) {
  CheckPropBase(property, expected_type, expected_name);
  CHECK_EQ(property.offset, expected_offset);
  CHECK_EQ(property.num_bits, expected_num_bits);
  CHECK_EQ(property.shift_bits, expected_shift_bits);
}

const d::ObjectProperty& FindProp(const d::ObjectPropertiesResult& props,
                                  std::string name) {
  for (size_t i = 0; i < props.num_properties; ++i) {
    if (name == props.properties[i]->name) {
      return *props.properties[i];
    }
  }
  CHECK_WITH_MSG(false, ("property '" + name + "' not found").c_str());
  UNREACHABLE();
}

template <typename TValue>
TValue ReadProp(const d::ObjectPropertiesResult& props, std::string name) {
  const d::ObjectProperty& prop = FindProp(props, name);
  return *reinterpret_cast<TValue*>(prop.address);
}

// A simple implementation of ExternalStringResource that lets us control the
// result of IsCacheable().
class StringResource : public v8::String::ExternalStringResource {
 public:
  explicit StringResource(bool cacheable) : cacheable_(cacheable) {}
  const uint16_t* data() const override {
    return reinterpret_cast<const uint16_t*>(u"abcde");
  }
  size_t length() const override { return 5; }
  bool IsCacheable() const override { return cacheable_; }

 private:
  bool cacheable_;
};

}  // namespace

class TestDebugHelper {
 public:
  static Address MetadataTableAddress() {
#ifdef V8_ENABLE_SANDBOX
    return reinterpret_cast<Address>(MemoryChunk::MetadataTableAddress());
#else
    return 0;
#endif
  }
};

TEST(GetObjectProperties) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  PtrComprCageAccessScope ptr_compr_cage_access_scope(i_isolate);
  v8::HandleScope scope(isolate);
  LocalContext context;
  // Claim we don't know anything about the heap layout.
  d::HeapAddresses heap_addresses{0, 0, 0, 0, 0};

  v8::Local<v8::Value> v = CompileRun("42");
  DirectHandle<Object> o = v8::Utils::OpenDirectHandle(*v);
  d::ObjectPropertiesResultPtr props =
      d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
  CHECK(props->type_check_result == d::TypeCheckResult::kSmi);
  CHECK(props->brief == std::string("42 (0x2a)"));
  CHECK(props->type == std::string("v8::internal::Smi"));
  CHECK_EQ(props->num_properties, 0);

  v = CompileRun("[\"a\", \"bc\"]");
  o = v8::Utils::OpenDirectHandle(*v);
  props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
  CHECK(props->type_check_result == d::TypeCheckResult::kUsedMap);
  CHECK(props->type == std::string("v8::internal::JSArray"));
  CHECK_EQ(props->num_properties, 4);
  CheckProp(*props->properties[0],
            "v8::internal::TaggedMember<v8::internal::Map>", "map");
  CheckProp(*props->properties[1],
            "v8::internal::TaggedMember<v8::internal::Object>",
            "properties_or_hash");
  CheckProp(*props->properties[2],
            "v8::internal::TaggedMember<v8::internal::FixedArrayBase>",
            "elements");
  CheckProp(*props->properties[3],
            "v8::internal::TaggedMember<v8::internal::Object>", "length",
            static_cast<i::Tagged_t>(IntToSmi(2)));

  // We need to supply some valid address for decompression before reading the
  // elements from the JSArray.
  heap_addresses.any_heap_pointer = (*o).ptr();

  i::Tagged_t properties_or_hash =
      *reinterpret_cast<i::Tagged_t*>(props->properties[1]->address);
  i::Tagged_t elements =
      *reinterpret_cast<i::Tagged_t*>(props->properties[2]->address);

  // The properties_or_hash_code field should be an empty fixed array. Since
  // that is at a known offset, we should be able to detect it even without
  // any ability to read memory.
  {
    MemoryFailureRegion failure(0, UINTPTR_MAX);
    props =
        d::GetObjectProperties(properties_or_hash, &ReadMemory, heap_addresses);
    CHECK(props->type_check_result ==
          d::TypeCheckResult::kObjectPointerValidButInaccessible);
    CHECK(props->type == std::string("v8::internal::HeapObject"));
    CHECK_EQ(props->num_properties, 1);
    CheckProp(*props->properties[0],
              "v8::internal::TaggedMember<v8::internal::Map>", "map");
    // "maybe" prefix indicates that GetObjectProperties recognized the offset
    // within the page as matching a known object, but didn't know whether the
    // object is on the right page. This response can only happen in builds
    // without pointer compression, because otherwise heap addresses would be at
    // deterministic locations within the heap reservation.
    CHECK(COMPRESS_POINTERS_BOOL
              ? StartsWith(props->brief, "EmptyFixedArray")
              : Contains(props->brief, "maybe EmptyFixedArray"));

    // Provide a heap first page so the API can be more sure.
    heap_addresses.read_only_space_first_page =
        i_isolate->heap()->read_only_space()->FirstPageAddress();
    props =
        d::GetObjectProperties(properties_or_hash, &ReadMemory, heap_addresses);
    CHECK(props->type_check_result ==
          d::TypeCheckResult::kObjectPointerValidButInaccessible);
    CHECK(props->type == std::string("v8::internal::HeapObject"));
    CHECK_EQ(props->num_properties, 1);
    CheckProp(*props->properties[0],
              "v8::internal::TaggedMember<v8::internal::Map>", "map");
    CHECK(StartsWith(props->brief, "EmptyFixedArray"));
  }

  props = d::GetObjectProperties(elements, &ReadMemory, heap_addresses);
  CHECK(props->type_check_result == d::TypeCheckResult::kUsedMap);
  CHECK(props->type == std::string("v8::internal::FixedArray"));
  CHECK_EQ(props->num_properties, 3);
  CheckProp(*props->properties[0],
            "v8::internal::TaggedMember<v8::internal::Map>", "map");
  CheckProp(*props->properties[1],
            "v8::internal::TaggedMember<v8::internal::Object>", "length",
            static_cast<i::Tagged_t>(IntToSmi(2)));
  CheckProp(*props->properties[2],
            "v8::internal::TaggedMember<v8::internal::Object>", "objects",
            d::PropertyKind::kArrayOfKnownSize, 2);

  // Get the second string value from the FixedArray.
  i::Tagged_t second_string_address =
      reinterpret_cast<i::Tagged_t*>(props->properties[2]->address)[1];
  props = d::GetObjectProperties(second_string_address, &ReadMemory,
                                 heap_addresses);
  CHECK(props->type_check_result == d::TypeCheckResult::kUsedMap);
  CHECK(props->type == std::string("v8::internal::SeqOneByteString"));
  CHECK_EQ(props->num_properties, 4);
  CheckProp(*props->properties[0],
            "v8::internal::TaggedMember<v8::internal::Map>", "map");
  CheckProp(*props->properties[1], "uint32_t", "raw_hash_field");
  CheckProp(*props->properties[2], "int32_t", "length", 2);
  CheckProp(*props->properties[3], "char", "chars",
            d::PropertyKind::kArrayOfKnownSize, 2);
  CHECK_EQ(
      strncmp("bc",
              reinterpret_cast<const char*>(props->properties[3]->address), 2),
      0);

  // Read the second string again, using a type hint instead of the map. All of
  // its properties should match what we read last time.
  d::ObjectPropertiesResultPtr props2;
  {
    d::HeapAddresses heap_addresses_without_ro_space = heap_addresses;
    heap_addresses_without_ro_space.read_only_space_first_page = 0;
    uintptr_t map_ptr = props->properties[0]->address;
    uintptr_t map_map_ptr = *reinterpret_cast<i::Tagged_t*>(map_ptr);
#if V8_MAP_PACKING
    map_map_ptr = reinterpret_cast<i::MapWord*>(&map_map_ptr)->ToMap().ptr();
#endif
    uintptr_t map_address =
        d::GetObjectProperties(map_map_ptr, &ReadMemory,
                               heap_addresses_without_ro_space)
            ->properties[0]
            ->address;
    MemoryFailureRegion failure(map_address, map_address + i::Map::kSize);
    props2 = d::GetObjectProperties(second_string_address, &ReadMemory,
                                    heap_addresses_without_ro_space,
                                    "v8::internal::String");
    if (COMPRESS_POINTERS_BOOL) {
      // The first page of each heap space can be automatically detected when
      // pointer compression is active, so we expect to use known maps instead
      // of the type hint.
      CHECK_EQ(props2->type_check_result, d::TypeCheckResult::kKnownMapPointer);
      CHECK(props2->type == std::string("v8::internal::SeqOneByteString"));
      CHECK_EQ(props2->num_properties, 4);
      CheckProp(*props2->properties[3], "char", "chars",
                d::PropertyKind::kArrayOfKnownSize, 2);
      CHECK_EQ(props2->num_guessed_types, 0);
    } else {
      CHECK_EQ(props2->type_check_result, d::TypeCheckResult::kUsedTypeHint);
      CHECK(props2->type == std::string("v8::internal::String"));
      CHECK_EQ(props2->num_properties, 3);

      // The type hint we provided was the abstract class String, but
      // GetObjectProperties should have recognized that the Map pointer looked
      // like the right value for a SeqOneByteString.
      CHECK_EQ(props2->num_guessed_types, 1);
      CHECK(std::string(props2->guessed_types[0]) ==
            std::string("v8::internal::SeqOneByteString"));
    }
    CheckProp(*props2->properties[0],
              "v8::internal::TaggedMember<v8::internal::Map>", "map",
              *reinterpret_cast<i::Tagged_t*>(props->properties[0]->address));
    CheckProp(*props2->properties[1], "uint32_t", "raw_hash_field",
              *reinterpret_cast<int32_t*>(props->properties[1]->address));
    CheckProp(*props2->properties[2], "int32_t", "length", 2);
  }

  // Try a weak reference.
  props2 = d::GetObjectProperties(second_string_address | kWeakHeapObjectMask,
                                  &ReadMemory, heap_addresses);
  std::string weak_ref_prefix = "weak ref to ";
  CHECK(weak_ref_prefix + props->brief == props2->brief);
  CHECK(props2->type_check_result == d::TypeCheckResult::kUsedMap);
  CHECK(props2->type == std::string("v8::internal::SeqOneByteString"));
  CHECK_EQ(props2->num_properties, 4);
  CheckProp(*props2->properties[0],
            "v8::internal::TaggedMember<v8::internal::Map>", "map",
            *reinterpret_cast<i::Tagged_t*>(props->properties[0]->address));
  CheckProp(*props2->properties[1], "uint32_t", "raw_hash_field",
            *reinterpret_cast<i::Tagged_t*>(props->properties[1]->address));
  CheckProp(*props2->properties[2], "int32_t", "length", 2);

  // Build a complicated string (multi-level cons with slices inside) to test
  // string printing.
  v = CompileRun(R"(
    const alphabet = "abcdefghijklmnopqrstuvwxyz";
    alphabet.substr(3,20) + alphabet.toUpperCase().substr(5,15) + "7")");
  o = v8::Utils::OpenDirectHandle(*v);
  props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
  CHECK(Contains(props->brief, "\"defghijklmnopqrstuvwFGHIJKLMNOPQRST7\""));

  // Cause a failure when reading the "second" pointer within the top-level
  // ConsString.
  {
    CheckProp(*props->properties[4],
              "v8::internal::TaggedMember<v8::internal::String>", "second");
    uintptr_t second_address = props->properties[4]->address;
    MemoryFailureRegion failure(second_address, second_address + 4);
    props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
    CHECK(Contains(props->brief, "\"defghijklmnopqrstuvwFGHIJKLMNOPQRST...\""));
  }

  // Build a very long string.
  v = CompileRun("'a'.repeat(1000)");
  o = v8::Utils::OpenDirectHandle(*v);
  props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
  CHECK(Contains(props->brief, "\"" + std::string(80, 'a') + "...\""));

  // GetObjectProperties can read cacheable external strings.
  heap_addresses.metadata_pointer_table =
      TestDebugHelper::MetadataTableAddress();
  StringResource* string_resource = new StringResource(true);
  auto cachable_external_string =
      v8::String::NewExternalTwoByte(isolate, string_resource);
  o = v8::Utils::OpenDirectHandle(*cachable_external_string.ToLocalChecked());
  props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
  CHECK(Contains(props->brief, "\"abcde\""));
  CheckProp(*props->properties[5], "char16_t", "raw_characters",
            d::PropertyKind::kArrayOfKnownSize, string_resource->length());
  CHECK_EQ(props->properties[5]->address,
           reinterpret_cast<uintptr_t>(string_resource->data()));

  // GetObjectProperties cannot read uncacheable external strings.
  auto external_string =
      v8::String::NewExternalTwoByte(isolate, new StringResource(false));
  o = v8::Utils::OpenDirectHandle(*external_string.ToLocalChecked());
  props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
  CHECK_EQ(std::string(props->brief).find("\""), std::string::npos);

  // Build a basic JS object and get its properties.
  v = CompileRun("({a: 1, b: 2})");
  o = v8::Utils::OpenDirectHandle(*v);
  props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);

  // Objects constructed from literals get their properties placed inline, so
  // the GetObjectProperties response should include an array.
  const d::ObjectProperty& prop = FindProp(*props, "in-object properties");
  CheckProp(prop, "v8::internal::TaggedMember<v8::internal::Object>",
            "in-object properties", d::PropertyKind::kArrayOfKnownSize, 2);
  // The second item in that array is the SMI value 2 from the object literal.
  props2 =
      d::GetObjectProperties(reinterpret_cast<i::Tagged_t*>(prop.address)[1],
                             &ReadMemory, heap_addresses);
  CHECK(props2->brief == std::string("2 (0x2)"));

  // Verify the result for a heap object field which is itself a struct: the
  // "descriptors" field on a DescriptorArray.
  // Start by getting the object's map and the map's descriptor array.
  uintptr_t map_ptr = ReadProp<i::Tagged_t>(*props, "map");
#if V8_MAP_PACKING
  map_ptr = reinterpret_cast<i::MapWord*>(&map_ptr)->ToMap().ptr();
#endif
  props = d::GetObjectProperties(map_ptr, &ReadMemory, heap_addresses);
  props = d::GetObjectProperties(
      ReadProp<i::Tagged_t>(*props, "instance_descriptors"), &ReadMemory,
      heap_addresses);
  CHECK_EQ(props->num_properties, 6);
  // It should have at least two descriptors (possibly plus slack).
  CheckProp(*props->properties[1], "uint16_t", "number_of_all_descriptors");
  uint16_t number_of_all_descriptors =
      *reinterpret_cast<uint16_t*>(props->properties[1]->address);
  CHECK_GE(number_of_all_descriptors, 2);
  // The "descriptors" property should describe the struct layout for each
  // element in the array.
  const d::ObjectProperty& descriptors = *props->properties[5];
  // No C++ type is reported directly because there may not be an actual C++
  // struct with this layout, hence the empty string in this check.
  CheckProp(descriptors, /*type=*/"", "descriptors",
            d::PropertyKind::kArrayOfKnownSize, number_of_all_descriptors);
  CHECK_EQ(descriptors.size, 3 * i::kTaggedSize);
  CHECK_EQ(descriptors.num_struct_fields, 3);
  CheckStructProp(
      *descriptors.struct_fields[0],
      "v8::internal::TaggedMember<v8::internal::PrimitiveHeapObject>", "key",
      0 * i::kTaggedSize);
  CheckStructProp(*descriptors.struct_fields[1],
                  "v8::internal::TaggedMember<v8::internal::Object>", "details",
                  1 * i::kTaggedSize);
  CheckStructProp(*descriptors.struct_fields[2],
                  "v8::internal::TaggedMember<v8::internal::Object>", "value",
                  2 * i::kTaggedSize);

  // Build a basic JS function and get its properties. This will allow us to
  // exercise bitfield functionality.
  v = CompileRun("(function () {})");
  o = v8::Utils::OpenDirectHandle(*v);
  props = d::GetObjectProperties((*o).ptr(), &ReadMemory, heap_addresses);
  props = d::GetObjectProperties(
      ReadProp<i::Tagged_t>(*props, "shared_function_info"), &ReadMemory,
      heap_addresses);
  const d::ObjectProperty& flags = FindProp(*props, "flags");
  CHECK_GE(flags.num_struct_fields, 3);
  CheckStructProp(*flags.struct_fields[0], "FunctionKind", "function_kind", 0,
                  5, 0);
  CheckStructProp(*flags.struct_fields[1], "bool", "is_native", 0, 1, 5);
  CheckStructProp(*flags.struct_fields[2], "bool", "is_strict", 0, 1, 6);

  // Get data about a different bitfield struct which is contained within a smi.
  DirectHandle<i::JSFunction> function = Cast<i::JSFunction>(o);
  DirectHandle<i::SharedFunctionInfo> shared(function->shared(), i_isolate);
  DirectHandle<i::DebugInfo> debug_info =
      i_isolate->debug()->GetOrCreateDebugInfo(shared);
  props =
      d::GetObjectProperties(debug_info->ptr(), &ReadMemory, heap_addresses);
  const d::ObjectProperty& debug_flags = FindProp(*props, "flags");
  CHECK_GE(debug_flags.num_struct_fields, 5);
  CheckStructProp(*debug_flags.struct_fields[0], "bool", "has_break_info", 0, 1,
                  i::kSmiTagSize + i::kSmiShiftSize);
  CheckStructProp(*debug_flags.struct_fields[4], "bool", "can_break_at_entry",
                  0, 1, i::kSmiTagSize + i::kSmiShiftSize + 4);
}

static void FrameIterationCheck(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::StackFrameIterator iter(reinterpret_cast<i::Isolate*>(info.GetIsolate()));
  for (int i = 0; !iter.done(); i++) {
    i::StackFrame* frame = iter.frame();
    CHECK(i != 0 || (frame->type() == i::StackFrame::EXIT));
    d::StackFrameResultPtr props = d::GetStackFrame(frame->fp(), &ReadMemory);
    if (frame->is_javascript()) {
      JavaScriptFrame* js_frame = JavaScriptFrame::cast(frame);
      CHECK_EQ(props->num_properties, 5);
      auto js_function = js_frame->function();
      // This one is Tagged, not TaggedMember, because it's from the stack.
      CheckProp(*props->properties[0],
                "v8::internal::Tagged<v8::internal::JSFunction>",
                "currently_executing_jsfunction", js_function.ptr());
      auto shared_function_info = js_function->shared();
      auto script = i::Cast<i::Script>(shared_function_info->script());
      CheckProp(*props->properties[1],
                "v8::internal::TaggedMember<v8::internal::Object>",
                "script_name", static_cast<i::Tagged_t>(script->name().ptr()));
      CheckProp(*props->properties[2],
                "v8::internal::TaggedMember<v8::internal::Object>",
                "script_source",
                static_cast<i::Tagged_t>(script->source().ptr()));

      auto scope_info = shared_function_info->scope_info();
      CheckProp(*props->properties[3],
                "v8::internal::TaggedMember<v8::internal::Object>",
                "function_name",
                static_cast<i::Tagged_t>(scope_info->FunctionName().ptr()));

      CheckProp(*props->properties[4], "", "function_character_offset");
      const d::ObjectProperty& function_character_offset =
          *props->properties[4];
      CHECK_EQ(function_character_offset.num_struct_fields, 2);
      CheckStructProp(*function_character_offset.struct_fields[0],
                      "v8::internal::TaggedMember<v8::internal::Object>",
                      "start", 0);
      CheckStructProp(*function_character_offset.struct_fields[1],
                      "v8::internal::TaggedMember<v8::internal::Object>", "end",
                      4);
    } else {
      CHECK_EQ(props->num_properties, 0);
    }
    iter.Advance();
  }
}

THREADED_TEST(GetFrameStack) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  PtrComprCageAccessScope ptr_compr_cage_access_scope(i_isolate);
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = v8::ObjectTemplate::New(isolate);
  obj->SetNativeDataProperty(v8_str("xxx"), FrameIterationCheck);
  CHECK(env->Global()
            ->Set(env.local(), v8_str("obj"),
                  obj->NewInstance(env.local()).ToLocalChecked())
            .FromJust());
  v8::Script::Compile(env.local(), v8_str("function foo() {"
                                          "  return obj.xxx;"
                                          "}"
                                          "foo();"))
      .ToLocalChecked()
      ->Run(env.local())
      .ToLocalChecked();
}

TEST(SmallOrderedHashSetGetObjectProperties) {
  LocalContext context;
  Isolate* isolate = reinterpret_cast<Isolate*>((*context)->GetIsolate());
  Factory* factory = isolate->factory();
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
  HandleScope scope(isolate);

  DirectHandle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  const size_t number_of_buckets = 2;
  CHECK_EQ(number_of_buckets, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());

  // Verify with the definition of SmallOrderedHashSet in
  // src\objects\ordered-hash-table.tq.
  d::HeapAddresses heap_addresses{0, 0, 0, 0, 0};
  d::ObjectPropertiesResultPtr props =
      d::GetObjectProperties(set->ptr(), &ReadMemory, heap_addresses);
  CHECK_EQ(props->type_check_result, d::TypeCheckResult::kUsedMap);
  CHECK_EQ(props->type, std::string("v8::internal::SmallOrderedHashSet"));
  CHECK_EQ(props->num_properties, 8);

  CheckProp(*props->properties[0],
            "v8::internal::TaggedMember<v8::internal::Map>", "map");
  CheckProp(*props->properties[1], "uint8_t", "number_of_elements");
  CheckProp(*props->properties[2], "uint8_t", "number_of_deleted_elements");
  CheckProp(*props->properties[3], "uint8_t", "number_of_buckets");
#if TAGGED_SIZE_8_BYTES
  CheckProp(*props->properties[4], "uint8_t", "padding",
            d::PropertyKind::kArrayOfKnownSize, 5);
#else
  CheckProp(*props->properties[4], "uint8_t", "padding",
            d::PropertyKind::kArrayOfKnownSize, 1);
#endif
  CheckProp(*props->properties[5],
            "v8::internal::TaggedMember<v8::internal::Object>", "data_table",
            d::PropertyKind::kArrayOfKnownSize,
            number_of_buckets * OrderedHashMap::kLoadFactor);
  CheckProp(*props->properties[6], "uint8_t", "hash_table",
            d::PropertyKind::kArrayOfKnownSize, number_of_buckets);
  CheckProp(*props->properties[7], "uint8_t", "chain_table",
            d::PropertyKind::kArrayOfKnownSize,
            number_of_buckets * OrderedHashMap::kLoadFactor);
}

}  // namespace internal
}  // namespace v8
