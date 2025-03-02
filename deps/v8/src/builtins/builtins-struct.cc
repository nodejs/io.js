// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include "src/builtins/builtins-utils-inl.h"
#include "src/objects/js-struct-inl.h"
#include "src/objects/property-details.h"

namespace v8 {
namespace internal {

constexpr int kMaxJSStructFields = 999;
// Note: For Wasm structs, we currently allow 2000 fields, because there was
// specific demand for that. Ideally we'd have the same limit, but JS structs
// rely on DescriptorArrays and are hence limited to 1020 fields at most.
static_assert(kMaxJSStructFields <= kMaxNumberOfDescriptors);

namespace {

struct NameHandleHasher {
  size_t operator()(IndirectHandle<Name> name) const { return name->hash(); }
};

struct UniqueNameHandleEqual {
  bool operator()(IndirectHandle<Name> x, IndirectHandle<Name> y) const {
    DCHECK(IsUniqueName(*x));
    DCHECK(IsUniqueName(*y));
    return *x == *y;
  }
};

using UniqueNameHandleSet =
    std::unordered_set<IndirectHandle<Name>, NameHandleHasher,
                       UniqueNameHandleEqual>;

}  // namespace

BUILTIN(SharedSpaceJSObjectHasInstance) {
  HandleScope scope(isolate);
  DirectHandle<Object> constructor = args.receiver();
  if (!IsJSFunction(*constructor)) {
    return *isolate->factory()->false_value();
  }

  bool result;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      AlwaysSharedSpaceJSObject::HasInstance(isolate,
                                             Cast<JSFunction>(constructor),
                                             args.atOrUndefined(isolate, 1)));
  return *isolate->factory()->ToBoolean(result);
}

namespace {
Maybe<bool> CollectFieldsAndElements(Isolate* isolate,
                                     DirectHandle<JSReceiver> property_names,
                                     int num_properties,
                                     DirectHandleVector<Name>& field_names,
                                     std::set<uint32_t>& element_names) {
  Handle<Object> raw_property_name;
  Handle<Name> property_name;
  UniqueNameHandleSet field_names_set;
  for (int i = 0; i < num_properties; i++) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, raw_property_name,
        JSReceiver::GetElement(isolate, property_names, i), Nothing<bool>());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, property_name,
                                     Object::ToName(isolate, raw_property_name),
                                     Nothing<bool>());

    bool is_duplicate;
    size_t index;
    if (!property_name->AsIntegerIndex(&index) ||
        index > JSObject::kMaxElementIndex) {
      property_name = isolate->factory()->InternalizeName(property_name);

      // TODO(v8:12547): Support Symbols?
      if (IsSymbol(*property_name)) {
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate, NewTypeError(MessageTemplate::kSymbolToString),
            Nothing<bool>());
      }

      is_duplicate = !field_names_set.insert(property_name).second;
      // Keep the field names in the original order.
      if (!is_duplicate) field_names.push_back(property_name);
    } else {
      is_duplicate = !element_names.insert(static_cast<uint32_t>(index)).second;
    }

    if (is_duplicate) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewTypeError(MessageTemplate::kDuplicateTemplateProperty,
                       property_name),
          Nothing<bool>());
    }
  }

  return Just(true);
}
}  // namespace

BUILTIN(SharedStructTypeConstructor) {
  DCHECK(v8_flags.shared_string_table);

  HandleScope scope(isolate);
  auto* factory = isolate->factory();

  DirectHandle<Map> instance_map;

  {
    // Step 1: Collect the struct's property names and create the instance map.

    DirectHandle<JSReceiver> property_names_arg;
    if (!IsJSReceiver(*args.atOrUndefined(isolate, 1))) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate,
          NewTypeError(MessageTemplate::kArgumentIsNonObject,
                       factory->NewStringFromAsciiChecked("property names")));
    }
    property_names_arg = args.at<JSReceiver>(1);

    // Treat property_names_arg as arraylike.
    DirectHandle<Object> raw_length_number;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, raw_length_number,
        Object::GetLengthFromArrayLike(isolate, property_names_arg));
    double num_properties_double = Object::NumberValue(*raw_length_number);
    if (num_properties_double < 0 ||
        num_properties_double > kMaxJSStructFields) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewRangeError(MessageTemplate::kStructFieldCountOutOfRange));
    }
    int num_properties = static_cast<int>(num_properties_double);

    DirectHandleVector<Name> field_names(isolate);
    std::set<uint32_t> element_names;
    if (num_properties != 0) {
      MAYBE_RETURN(
          CollectFieldsAndElements(isolate, property_names_arg, num_properties,
                                   field_names, element_names),
          ReadOnlyRoots(isolate).exception());
    }

    if (IsUndefined(*args.atOrUndefined(isolate, 2), isolate)) {
      // Create a new instance map if this type isn't registered.
      instance_map = JSSharedStruct::CreateInstanceMap(
          isolate, base::VectorOf(field_names), element_names, {});
    } else {
      // Otherwise, get the canonical map.
      if (!IsString(*args.atOrUndefined(isolate, 2))) {
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate, NewTypeError(MessageTemplate::kArgumentIsNonString,
                                  factory->NewStringFromAsciiChecked(
                                      "type registry key")));
      }
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, instance_map,
          isolate->shared_struct_type_registry()->Register(
              isolate, args.at<String>(2), base::VectorOf(field_names),
              element_names));
    }
  }

  // Step 2: Creat the JSFunction constructor. This is always created anew,
  // regardless of whether the type is registered.
  DirectHandle<SharedFunctionInfo> info =
      isolate->factory()->NewSharedFunctionInfoForBuiltin(
          isolate->factory()->empty_string(), Builtin::kSharedStructConstructor,
          0, kAdapt);

  DirectHandle<JSFunction> constructor =
      Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
          .set_map(isolate->strict_function_with_readonly_prototype_map())
          .Build();
  constructor->set_prototype_or_initial_map(*instance_map, kReleaseStore);

  JSObject::AddProperty(
      isolate, constructor, factory->has_instance_symbol(),
      direct_handle(
          isolate->native_context()->shared_space_js_object_has_instance(),
          isolate),
      ALL_ATTRIBUTES_MASK);

  return *constructor;
}

BUILTIN(SharedStructConstructor) {
  HandleScope scope(isolate);
  DirectHandle<JSFunction> constructor(args.target());
  DirectHandle<Map> instance_map(constructor->initial_map(), isolate);
  return *isolate->factory()->NewJSSharedStruct(
      args.target(),
      JSSharedStruct::GetElementsTemplate(isolate, *instance_map));
}

BUILTIN(SharedStructTypeIsSharedStruct) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSSharedStruct(*args.atOrUndefined(isolate, 1)));
}

BUILTIN(AtomicsMutexIsMutex) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSAtomicsMutex(*args.atOrUndefined(isolate, 1)));
}

BUILTIN(AtomicsConditionIsCondition) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSAtomicsCondition(*args.atOrUndefined(isolate, 1)));
}

}  // namespace internal
}  // namespace v8
