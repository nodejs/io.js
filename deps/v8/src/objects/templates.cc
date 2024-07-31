// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/templates.h"

#include <algorithm>
#include <cstdint>

#include "src/api/api-inl.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/contexts.h"
#include "src/objects/function-kind.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/name-inl.h"
#include "src/objects/objects.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/string-inl.h"

namespace v8 {
namespace internal {

bool FunctionTemplateInfo::HasInstanceType() {
  return instance_type() != kNoJSApiObjectType;
}

Handle<SharedFunctionInfo> FunctionTemplateInfo::GetOrCreateSharedFunctionInfo(
    Isolate* isolate, Handle<FunctionTemplateInfo> info,
    MaybeHandle<Name> maybe_name) {
  Tagged<Object> current_info = info->shared_function_info();
  if (IsSharedFunctionInfo(current_info)) {
    return handle(SharedFunctionInfo::cast(current_info), isolate);
  }
  Handle<Name> name;
  Handle<String> name_string;
  if (maybe_name.ToHandle(&name) && IsString(*name)) {
    name_string = Handle<String>::cast(name);
  } else if (IsString(info->class_name())) {
    name_string = handle(String::cast(info->class_name()), isolate);
  } else {
    name_string = isolate->factory()->empty_string();
  }
  FunctionKind function_kind;
  if (info->remove_prototype()) {
    function_kind = FunctionKind::kConciseMethod;
  } else {
    function_kind = FunctionKind::kNormalFunction;
  }
  Handle<SharedFunctionInfo> sfi =
      isolate->factory()->NewSharedFunctionInfoForApiFunction(name_string, info,
                                                              function_kind);
  {
    DisallowGarbageCollection no_gc;
    Tagged<SharedFunctionInfo> raw_sfi = *sfi;
    Tagged<FunctionTemplateInfo> raw_template = *info;
    raw_sfi->set_length(raw_template->length());
    raw_sfi->DontAdaptArguments();
    DCHECK(raw_sfi->IsApiFunction());
    raw_template->set_shared_function_info(raw_sfi);
  }
  return sfi;
}

bool FunctionTemplateInfo::IsTemplateFor(Tagged<Map> map) const {
  RCS_SCOPE(
      LocalHeap::Current() == nullptr
          ? GetIsolateChecked()->counters()->runtime_call_stats()
          : LocalIsolate::FromHeap(LocalHeap::Current())->runtime_call_stats(),
      RuntimeCallCounterId::kIsTemplateFor);

  // There is a constraint on the object; check.
  if (!IsJSObjectMap(map)) return false;

  if (v8_flags.embedder_instance_types) {
    DCHECK_IMPLIES(allowed_receiver_instance_type_range_start() == 0,
                   allowed_receiver_instance_type_range_end() == 0);
    if (base::IsInRange(map->instance_type(),
                        allowed_receiver_instance_type_range_start(),
                        allowed_receiver_instance_type_range_end())) {
      return true;
    }
  }

  // Fetch the constructor function of the object.
  Tagged<Object> cons_obj = map->GetConstructor();
  Tagged<Object> type;
  if (IsJSFunction(cons_obj)) {
    Tagged<JSFunction> fun = JSFunction::cast(cons_obj);
    if (!fun->shared()->IsApiFunction()) return false;
    type = fun->shared()->api_func_data();
  } else if (IsFunctionTemplateInfo(cons_obj)) {
    type = FunctionTemplateInfo::cast(cons_obj);
  } else {
    return false;
  }
  DCHECK(IsFunctionTemplateInfo(type));
  // Iterate through the chain of inheriting function templates to
  // see if the required one occurs.
  while (IsFunctionTemplateInfo(type)) {
    if (type == *this) return true;
    type = FunctionTemplateInfo::cast(type)->GetParentTemplate();
  }
  // Didn't find the required type in the inheritance chain.
  return false;
}

bool FunctionTemplateInfo::IsLeafTemplateForApiObject(
    Tagged<Object> object) const {
  i::DisallowGarbageCollection no_gc;

  if (!IsJSApiObject(object)) {
    return false;
  }

  bool result = false;
  Tagged<Map> map = HeapObject::cast(object)->map();
  Tagged<Object> constructor_obj = map->GetConstructor();
  if (IsJSFunction(constructor_obj)) {
    Tagged<JSFunction> fun = JSFunction::cast(constructor_obj);
    result = (*this == fun->shared()->api_func_data());
  } else if (IsFunctionTemplateInfo(constructor_obj)) {
    result = (*this == constructor_obj);
  }
  DCHECK_IMPLIES(result, IsTemplateFor(map));
  return result;
}

// static
Tagged<FunctionTemplateRareData>
FunctionTemplateInfo::AllocateFunctionTemplateRareData(
    Isolate* isolate, Handle<FunctionTemplateInfo> function_template_info) {
  DCHECK(IsUndefined(function_template_info->rare_data(kAcquireLoad), isolate));
  Handle<FunctionTemplateRareData> rare_data =
      isolate->factory()->NewFunctionTemplateRareData();
  function_template_info->set_rare_data(*rare_data, kReleaseStore);
  return *rare_data;
}

base::Optional<Tagged<Name>> FunctionTemplateInfo::TryGetCachedPropertyName(
    Isolate* isolate, Tagged<Object> getter) {
  DisallowGarbageCollection no_gc;
  if (!IsFunctionTemplateInfo(getter)) {
    if (!IsJSFunction(getter)) return {};
    Tagged<SharedFunctionInfo> info = JSFunction::cast(getter)->shared();
    if (!info->IsApiFunction()) return {};
    getter = info->api_func_data();
  }
  // Check if the accessor uses a cached property.
  Tagged<Object> maybe_name =
      FunctionTemplateInfo::cast(getter)->cached_property_name();
  if (IsTheHole(maybe_name, isolate)) return {};
  return Name::cast(maybe_name);
}

int FunctionTemplateInfo::GetCFunctionsCount() const {
  i::DisallowHeapAllocation no_gc;
  return FixedArray::cast(GetCFunctionOverloads())->length() /
         kFunctionOverloadEntrySize;
}

Address FunctionTemplateInfo::GetCFunction(int index) const {
  i::DisallowHeapAllocation no_gc;
  return v8::ToCData<Address>(FixedArray::cast(GetCFunctionOverloads())
                                  ->get(index * kFunctionOverloadEntrySize));
}

const CFunctionInfo* FunctionTemplateInfo::GetCSignature(int index) const {
  i::DisallowHeapAllocation no_gc;
  return v8::ToCData<CFunctionInfo*>(
      FixedArray::cast(GetCFunctionOverloads())
          ->get(index * kFunctionOverloadEntrySize + 1));
}

// static
Handle<DictionaryTemplateInfo> DictionaryTemplateInfo::Create(
    Isolate* isolate, const v8::MemorySpan<const std::string_view>& names) {
  Handle<FixedArray> property_names = isolate->factory()->NewFixedArray(
      static_cast<int>(names.size()), AllocationType::kOld);
  int index = 0;
  uint32_t unused_array_index;
  for (const std::string_view& name : names) {
    Handle<String> internalized_name = isolate->factory()->InternalizeString(
        base::Vector<const char>(name.data(), name.length()));
    // Check that property name cannot be used as index.
    CHECK(!internalized_name->AsArrayIndex(&unused_array_index));
    property_names->set(index, *internalized_name);
    ++index;
  }
  return isolate->factory()->NewDictionaryTemplateInfo(property_names);
}

namespace {

Handle<JSObject> CreateSlowJSObjectWithProperties(
    Isolate* isolate, Handle<FixedArray> property_names,
    const MemorySpan<MaybeLocal<Value>>& property_values,
    int num_properties_set) {
  Handle<JSObject> object = isolate->factory()->NewSlowJSObjectFromMap(
      isolate->slow_object_with_object_prototype_map(), num_properties_set,
      AllocationType::kYoung);
  Handle<Object> properties = handle(object->raw_properties_or_hash(), isolate);
  for (int i = 0; i < static_cast<int>(property_values.size()); ++i) {
    Local<Value> property_value;
    if (!property_values[i].ToLocal(&property_value)) {
      continue;
    }
    properties = PropertyDictionary::Add(
        isolate, Handle<PropertyDictionary>::cast(properties),
        Handle<String>::cast(handle(property_names->get(i), isolate)),
        Utils::OpenHandle(*property_value), PropertyDetails::Empty());
  }
  object->set_raw_properties_or_hash(*properties);
  return object;
}

}  // namespace

// static
Handle<JSObject> DictionaryTemplateInfo::NewInstance(
    DirectHandle<NativeContext> context,
    DirectHandle<DictionaryTemplateInfo> self,
    const MemorySpan<MaybeLocal<Value>>& property_values) {
  Isolate* isolate = context->GetIsolate();
  Handle<FixedArray> property_names = handle(self->property_names(), isolate);

  const int property_names_len = property_names->length();
  CHECK_EQ(property_names_len, static_cast<int>(property_values.size()));
  const int num_properties_set = static_cast<int>(std::count_if(
      property_values.begin(), property_values.end(),
      [](const auto& maybe_value) { return !maybe_value.IsEmpty(); }));

  if (V8_UNLIKELY(num_properties_set > JSObject::kMaxInObjectProperties)) {
    return CreateSlowJSObjectWithProperties(
        isolate, property_names, property_values, num_properties_set);
  }

  const bool can_use_map_cache = num_properties_set == property_names_len;
  MaybeHandle<Map> maybe_cached_map;
  if (V8_LIKELY(can_use_map_cache)) {
    maybe_cached_map = TemplateInfo::ProbeInstantiationsCache<Map>(
        isolate, context, self->serial_number(),
        TemplateInfo::CachingMode::kUnlimited);
  }
  Handle<Map> cached_map;
  if (V8_LIKELY(can_use_map_cache && maybe_cached_map.ToHandle(&cached_map))) {
    DCHECK(!cached_map->is_dictionary_map());
    bool can_use_cached_map = !cached_map->is_deprecated();
    if (V8_LIKELY(can_use_cached_map)) {
      // Verify that the cached map can be reused.
      auto descriptors = handle(cached_map->instance_descriptors(), isolate);
      for (int i = 0; i < static_cast<int>(property_values.size()); ++i) {
        Handle<Object> value =
            Utils::OpenHandle(*property_values[i].ToLocalChecked());
        InternalIndex descriptor{static_cast<size_t>(i)};
        const auto details = descriptors->GetDetails(descriptor);

        if (!Object::FitsRepresentation(*value, details.representation()) ||
            !FieldType::NowContains(descriptors->GetFieldType(descriptor),
                                    value)) {
          can_use_cached_map = false;
          break;
        }
        // Double representation means mutable heap number. In this case we need
        // to allocate a new heap number to put in the dictionary.
        if (details.representation().Equals(Representation::Double())) {
          // We allowed coercion in `FitsRepresentation` above which means that
          // we may deal with a Smi here.
          property_values[i] = ToApiHandle<v8::Object>(
              isolate->factory()->NewHeapNumber(Object::Number(*value)));
        }
      }
      if (V8_LIKELY(can_use_cached_map)) {
        // Create the object from the cached map.
        CHECK(!cached_map->is_deprecated());
        CHECK_EQ(context->object_function_prototype(), cached_map->prototype());
        auto object = isolate->factory()->NewJSObjectFromMap(
            cached_map, AllocationType::kYoung);
        DisallowGarbageCollection no_gc;
        for (int i = 0; i < static_cast<int>(property_values.size()); ++i) {
          Local<Value> property_value = property_values[i].ToLocalChecked();
          Handle<Object> value = Utils::OpenHandle(*property_value);
          const FieldIndex index = FieldIndex::ForPropertyIndex(
              *cached_map, i, Representation::Tagged());
          object->FastPropertyAtPut(index, *value,
                                    WriteBarrierMode::SKIP_WRITE_BARRIER);
        }
        return object;
      }
    }
    // A cached map was either deprecated or the descriptors changed in
    // incompatible ways. We clear the cached map and continue with the generic
    // path.
    TemplateInfo::UncacheTemplateInstantiation(
        isolate, context, self, TemplateInfo::CachingMode::kUnlimited);
  }

  // General case: We either don't have a cached map, or it is unusuable for the
  // values provided.
  Handle<Map> current_map = isolate->factory()->ObjectLiteralMapFromCache(
      context, num_properties_set);
  Handle<JSObject> object = isolate->factory()->NewJSObjectFromMap(current_map);
  int current_property_index = 0;
  for (int i = 0; i < static_cast<int>(property_values.size()); ++i) {
    Local<Value> property_value;
    if (!property_values[i].ToLocal(&property_value)) {
      continue;
    }
    Handle<String> name =
        Handle<String>::cast(handle(property_names->get(i), isolate));
    Handle<Object> value = Utils::OpenHandle(*property_value);
    constexpr PropertyAttributes attributes = PropertyAttributes::NONE;
    constexpr PropertyConstness constness = PropertyConstness::kConst;
    current_map = Map::TransitionToDataProperty(isolate, current_map, name,
                                                value, attributes, constness,
                                                StoreOrigin::kNamed);
    if (current_map->is_dictionary_map()) {
      return CreateSlowJSObjectWithProperties(
          isolate, property_names, property_values, num_properties_set);
    }
    JSObject::MigrateToMap(isolate, object, current_map);
    PropertyDetails details = current_map->GetLastDescriptorDetails(isolate);
    object->WriteToField(InternalIndex(current_property_index), details,
                         *value);
    current_property_index++;
  }
  if (V8_LIKELY(can_use_map_cache)) {
    TemplateInfo::CacheTemplateInstantiation(
        isolate, context, self, TemplateInfo::CachingMode::kUnlimited,
        handle(object->map(), isolate));
  }
  return object;
}

}  // namespace internal
}  // namespace v8
