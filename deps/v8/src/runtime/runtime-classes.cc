// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <limits>

#include "src/builtins/accessors.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/logging/log.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/lookup-inl.h"
#include "src/objects/smi.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {


RUNTIME_FUNCTION(Runtime_ThrowUnsupportedSuperError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewReferenceError(MessageTemplate::kUnsupportedSuper));
}


RUNTIME_FUNCTION(Runtime_ThrowConstructorNonCallableError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> constructor = args.at<JSFunction>(0);
  Handle<String> name(constructor->shared()->Name(), isolate);

  DirectHandle<Context> context(constructor->native_context(), isolate);
  DCHECK(IsNativeContext(*context));
  Handle<JSFunction> realm_type_error_function(
      Cast<JSFunction>(context->get(Context::TYPE_ERROR_FUNCTION_INDEX)),
      isolate);
  if (name->length() == 0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewError(realm_type_error_function,
                          MessageTemplate::kAnonymousConstructorNonCallable));
  }
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewError(realm_type_error_function,
                        MessageTemplate::kConstructorNonCallable, name));
}


RUNTIME_FUNCTION(Runtime_ThrowStaticPrototypeError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kStaticPrototype));
}

RUNTIME_FUNCTION(Runtime_ThrowSuperAlreadyCalledError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewReferenceError(MessageTemplate::kSuperAlreadyCalled));
}

RUNTIME_FUNCTION(Runtime_ThrowSuperNotCalled) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewReferenceError(MessageTemplate::kSuperNotCalled));
}

namespace {

Tagged<Object> ThrowNotSuperConstructor(Isolate* isolate,
                                        Handle<Object> constructor,
                                        DirectHandle<JSFunction> function) {
  DirectHandle<String> super_name;
  if (IsJSFunction(*constructor)) {
    super_name =
        direct_handle(Cast<JSFunction>(constructor)->shared()->Name(), isolate);
  } else if (IsOddball(*constructor)) {
    DCHECK(IsNull(*constructor, isolate));
    super_name = isolate->factory()->null_string();
  } else {
    super_name = Object::NoSideEffectsToString(isolate, constructor);
  }
  // null constructor
  if (super_name->length() == 0) {
    super_name = isolate->factory()->null_string();
  }
  Handle<String> function_name(function->shared()->Name(), isolate);
  // anonymous class
  if (function_name->length() == 0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kNotSuperConstructorAnonymousClass,
                     super_name));
  }
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kNotSuperConstructor, super_name,
                            function_name));
}

}  // namespace

RUNTIME_FUNCTION(Runtime_ThrowNotSuperConstructor) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> constructor = args.at(0);
  DirectHandle<JSFunction> function = args.at<JSFunction>(1);
  return ThrowNotSuperConstructor(isolate, constructor, function);
}

namespace {

template <typename Dictionary>
Handle<Name> KeyToName(Isolate* isolate, Handle<Object> key) {
  static_assert((std::is_same<Dictionary, SwissNameDictionary>::value ||
                 std::is_same<Dictionary, NameDictionary>::value));
  DCHECK(IsName(*key));
  return Cast<Name>(key);
}

template <>
Handle<Name> KeyToName<NumberDictionary>(Isolate* isolate, Handle<Object> key) {
  DCHECK(IsNumber(*key));
  return isolate->factory()->NumberToString(key);
}

// Gets |index|'th argument which may be a class constructor object, a class
// prototype object or a class method. In the latter case the following
// post-processing may be required:
// 1) set method's name to a concatenation of |name_prefix| and |key| if the
//    method's shared function info indicates that method does not have a
//    shared name.
template <typename Dictionary>
MaybeHandle<Object> GetMethodAndSetName(Isolate* isolate,
                                        RuntimeArguments& args,
                                        Tagged<Smi> index,
                                        DirectHandle<String> name_prefix,
                                        Handle<Object> key) {
  int int_index = index.value();

  // Class constructor and prototype values do not require post processing.
  if (int_index < ClassBoilerplate::kFirstDynamicArgumentIndex) {
    return args.at<Object>(int_index);
  }

  Handle<JSFunction> method = args.at<JSFunction>(int_index);

  if (!method->shared()->HasSharedName()) {
    // TODO(ishell): method does not have a shared name at this point only if
    // the key is a computed property name. However, the bytecode generator
    // explicitly generates ToName bytecodes to ensure that the computed
    // property name is properly converted to Name. So, we can actually be smart
    // here and avoid converting Smi keys back to Name.
    Handle<Name> name = KeyToName<Dictionary>(isolate, key);
    if (!JSFunction::SetName(method, name, name_prefix)) {
      return MaybeHandle<Object>();
    }
  }
  return method;
}

// Gets |index|'th argument which may be a class constructor object, a class
// prototype object or a class method.
// This is a simplified version of GetMethodAndSetName()
// function above that is used when it's guaranteed that the method has
// shared name.
Tagged<Object> GetMethodWithSharedName(Isolate* isolate, RuntimeArguments& args,
                                       Tagged<Object> index) {
  DisallowGarbageCollection no_gc;
  int int_index = Smi::ToInt(index);

  // Class constructor and prototype values do not require post processing.
  if (int_index < ClassBoilerplate::kFirstDynamicArgumentIndex) {
    return args[int_index];
  }

  DirectHandle<JSFunction> method = args.at<JSFunction>(int_index);
  DCHECK(method->shared()->HasSharedName());
  return *method;
}

template <typename Dictionary>
Handle<Dictionary> ShallowCopyDictionaryTemplate(
    Isolate* isolate, Handle<Dictionary> dictionary_template) {
  Handle<Dictionary> dictionary =
      Dictionary::ShallowCopy(isolate, dictionary_template);
  // Clone all AccessorPairs in the dictionary.
  for (InternalIndex i : dictionary->IterateEntries()) {
    Tagged<Object> value = dictionary->ValueAt(i);
    if (IsAccessorPair(value)) {
      DirectHandle<AccessorPair> pair(Cast<AccessorPair>(value), isolate);
      pair = AccessorPair::Copy(isolate, pair);
      dictionary->ValueAtPut(i, *pair);
    }
  }
  return dictionary;
}

template <typename Dictionary>
bool SubstituteValues(Isolate* isolate, Handle<Dictionary> dictionary,
                      RuntimeArguments& args) {
  // Replace all indices with proper methods.
  ReadOnlyRoots roots(isolate);
  for (InternalIndex i : dictionary->IterateEntries()) {
    Tagged<Object> maybe_key = dictionary->KeyAt(i);
    if (!Dictionary::IsKey(roots, maybe_key)) continue;
    Handle<Object> key(maybe_key, isolate);
    Handle<Object> value(dictionary->ValueAt(i), isolate);
    if (IsAccessorPair(*value)) {
      auto pair = Cast<AccessorPair>(value);
      Tagged<Object> tmp = pair->getter();
      if (IsSmi(tmp)) {
        Handle<Object> result;
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, result,
            GetMethodAndSetName<Dictionary>(isolate, args, Cast<Smi>(tmp),
                                            isolate->factory()->get_string(),
                                            key),
            false);
        pair->set_getter(*result);
      }
      tmp = pair->setter();
      if (IsSmi(tmp)) {
        Handle<Object> result;
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, result,
            GetMethodAndSetName<Dictionary>(isolate, args, Cast<Smi>(tmp),
                                            isolate->factory()->set_string(),
                                            key),
            false);
        pair->set_setter(*result);
      }
    } else if (IsSmi(*value)) {
      Handle<Object> result;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, result,
          GetMethodAndSetName<Dictionary>(isolate, args, Cast<Smi>(*value),
                                          isolate->factory()->empty_string(),
                                          key),
          false);
      dictionary->ValueAtPut(i, *result);
    }
  }
  return true;
}

template <typename Dictionary>
void UpdateProtectors(Isolate* isolate, Handle<JSObject> receiver,
                      DirectHandle<Dictionary> properties_dictionary) {
  ReadOnlyRoots roots(isolate);
  for (InternalIndex i : properties_dictionary->IterateEntries()) {
    Tagged<Object> maybe_key = properties_dictionary->KeyAt(i);
    if (!Dictionary::IsKey(roots, maybe_key)) continue;
    DirectHandle<Name> name(Cast<Name>(maybe_key), isolate);
    LookupIterator::UpdateProtector(isolate, receiver, name);
  }
}

void UpdateProtectors(Isolate* isolate, Handle<JSObject> receiver,
                      DirectHandle<DescriptorArray> properties_template) {
  int nof_descriptors = properties_template->number_of_descriptors();
  for (InternalIndex i : InternalIndex::Range(nof_descriptors)) {
    DirectHandle<Name> name(properties_template->GetKey(i), isolate);
    LookupIterator::UpdateProtector(isolate, receiver, name);
  }
}

bool AddDescriptorsByTemplate(
    Isolate* isolate, DirectHandle<Map> map,
    DirectHandle<DescriptorArray> descriptors_template,
    Handle<NumberDictionary> elements_dictionary_template,
    Handle<JSObject> receiver, RuntimeArguments& args) {
  int nof_descriptors = descriptors_template->number_of_descriptors();

  DirectHandle<DescriptorArray> descriptors =
      DescriptorArray::Allocate(isolate, nof_descriptors, 0);

  Handle<NumberDictionary> elements_dictionary =
      *elements_dictionary_template ==
              ReadOnlyRoots(isolate).empty_slow_element_dictionary()
          ? elements_dictionary_template
          : ShallowCopyDictionaryTemplate(isolate,
                                          elements_dictionary_template);

  // Count the number of properties that must be in the instance and
  // create the property array to hold the constants.
  int count = 0;
  for (InternalIndex i : InternalIndex::Range(nof_descriptors)) {
    PropertyDetails details = descriptors_template->GetDetails(i);
    if (details.location() == PropertyLocation::kDescriptor &&
        details.kind() == PropertyKind::kData) {
      count++;
    }
  }
  DirectHandle<PropertyArray> property_array =
      isolate->factory()->NewPropertyArray(count);

  // Read values from |descriptors_template| and store possibly post-processed
  // values into "instantiated" |descriptors| array.
  int field_index = 0;
  for (InternalIndex i : InternalIndex::Range(nof_descriptors)) {
    Tagged<Object> value = descriptors_template->GetStrongValue(i);
    if (IsAccessorPair(value)) {
      DirectHandle<AccessorPair> pair = AccessorPair::Copy(
          isolate, handle(Cast<AccessorPair>(value), isolate));
      value = *pair;
    }
    DisallowGarbageCollection no_gc;
    Tagged<Name> name = descriptors_template->GetKey(i);
    // TODO(v8:5799): consider adding a ClassBoilerplate flag
    // "has_interesting_properties".
    if (name->IsInteresting(isolate)) {
      map->set_may_have_interesting_properties(true);
    }
    DCHECK(IsUniqueName(name));
    PropertyDetails details = descriptors_template->GetDetails(i);
    if (details.location() == PropertyLocation::kDescriptor) {
      if (details.kind() == PropertyKind::kData) {
        if (IsSmi(value)) {
          value = GetMethodWithSharedName(isolate, args, value);
        }
        details = details.CopyWithRepresentation(
            Object::OptimalRepresentation(value, isolate));
      } else {
        DCHECK_EQ(PropertyKind::kAccessor, details.kind());
        if (IsAccessorPair(value)) {
          Tagged<AccessorPair> pair = Cast<AccessorPair>(value);
          Tagged<Object> tmp = pair->getter();
          if (IsSmi(tmp)) {
            pair->set_getter(GetMethodWithSharedName(isolate, args, tmp));
          }
          tmp = pair->setter();
          if (IsSmi(tmp)) {
            pair->set_setter(GetMethodWithSharedName(isolate, args, tmp));
          }
        }
      }
    } else {
      UNREACHABLE();
    }
    DCHECK(Object::FitsRepresentation(value, details.representation()));
    if (details.location() == PropertyLocation::kDescriptor &&
        details.kind() == PropertyKind::kData) {
      details =
          PropertyDetails(details.kind(), details.attributes(),
                          PropertyLocation::kField, PropertyConstness::kConst,
                          details.representation(), field_index)
              .set_pointer(details.pointer());

      property_array->set(field_index, value);
      field_index++;
      descriptors->Set(i, name, FieldType::Any(), details);
    } else {
      descriptors->Set(i, name, value, details);
    }
  }

  UpdateProtectors(isolate, receiver, descriptors_template);

  map->InitializeDescriptors(isolate, *descriptors);
  if (elements_dictionary->NumberOfElements() > 0) {
    if (!SubstituteValues<NumberDictionary>(isolate, elements_dictionary,
                                            args)) {
      return false;
    }
    map->set_elements_kind(DICTIONARY_ELEMENTS);
  }

  // Atomically commit the changes.
  receiver->set_map(*map, kReleaseStore);
  if (elements_dictionary->NumberOfElements() > 0) {
    receiver->set_elements(*elements_dictionary);
  }
  if (property_array->length() > 0) {
    receiver->SetProperties(*property_array);
  }
  return true;
}

// TODO(v8:7569): This is a workaround for the Handle vs MaybeHandle difference
// in the return types of the different Add functions:
// OrderedNameDictionary::Add returns MaybeHandle, NameDictionary::Add returns
// Handle.
template <typename T>
Handle<T> ToHandle(Handle<T> h) {
  return h;
}
template <typename T>
Handle<T> ToHandle(MaybeHandle<T> h) {
  return h.ToHandleChecked();
}

template <typename Dictionary>
bool AddDescriptorsByTemplate(
    Isolate* isolate, DirectHandle<Map> map,
    Handle<Dictionary> properties_dictionary_template,
    Handle<NumberDictionary> elements_dictionary_template,
    DirectHandle<FixedArray> computed_properties, Handle<JSObject> receiver,
    RuntimeArguments& args) {
  int computed_properties_length = computed_properties->length();

  // Shallow-copy properties template.
  Handle<Dictionary> properties_dictionary =
      ShallowCopyDictionaryTemplate(isolate, properties_dictionary_template);
  Handle<NumberDictionary> elements_dictionary =
      ShallowCopyDictionaryTemplate(isolate, elements_dictionary_template);

  using ValueKind = ClassBoilerplate::ValueKind;
  using ComputedEntryFlags = ClassBoilerplate::ComputedEntryFlags;

  // Merge computed properties with properties and elements dictionary
  // templates.
  int i = 0;
  while (i < computed_properties_length) {
    int flags = Smi::ToInt(computed_properties->get(i++));

    ValueKind value_kind = ComputedEntryFlags::ValueKindBits::decode(flags);
    int key_index = ComputedEntryFlags::KeyIndexBits::decode(flags);
    Tagged<Smi> value = Smi::FromInt(key_index + 1);  // Value follows name.

    Handle<Object> key = args.at(key_index);
    DCHECK(IsName(*key));
    uint32_t element;
    Handle<Name> name = Cast<Name>(key);
    if (name->AsArrayIndex(&element)) {
      ClassBoilerplate::AddToElementsTemplate(
          isolate, elements_dictionary, element, key_index, value_kind, value);

    } else {
      name = isolate->factory()->InternalizeName(name);
      ClassBoilerplate::AddToPropertiesTemplate(
          isolate, properties_dictionary, name, key_index, value_kind, value);
      if (name->IsInteresting(isolate)) {
        // TODO(pthier): Add flags to swiss dictionaries.
        if constexpr (!V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
          properties_dictionary->set_may_have_interesting_properties(true);
        }
      }
    }
  }

  // Replace all indices with proper methods.
  if (!SubstituteValues<Dictionary>(isolate, properties_dictionary, args)) {
    return false;
  }

  UpdateProtectors(isolate, receiver,
                   DirectHandle<Dictionary>(properties_dictionary));

  if (elements_dictionary->NumberOfElements() > 0) {
    if (!SubstituteValues<NumberDictionary>(isolate, elements_dictionary,
                                            args)) {
      return false;
    }
    map->set_elements_kind(DICTIONARY_ELEMENTS);
  }

  // Atomically commit the changes.
  receiver->set_map(*map, kReleaseStore);
  receiver->set_raw_properties_or_hash(*properties_dictionary, kRelaxedStore);
  if (elements_dictionary->NumberOfElements() > 0) {
    receiver->set_elements(*elements_dictionary);
  }
  return true;
}

Handle<JSObject> CreateClassPrototype(Isolate* isolate) {
  // For constant tracking we want to avoid the hassle of handling
  // in-object properties, so create a map with no in-object
  // properties.

  // TODO(ishell) Support caching of zero in-object properties map
  // by ObjectLiteralMapFromCache().
  DirectHandle<Map> map = Map::Create(isolate, 0);
  return isolate->factory()->NewJSObjectFromMap(map);
}

bool InitClassPrototype(Isolate* isolate,
                        DirectHandle<ClassBoilerplate> class_boilerplate,
                        Handle<JSObject> prototype,
                        Handle<HeapObject> prototype_parent,
                        DirectHandle<JSFunction> constructor,
                        RuntimeArguments& args) {
  Handle<Map> map(prototype->map(), isolate);
  map = Map::CopyDropDescriptors(isolate, map);
  map->set_is_prototype_map(true);
  Map::SetPrototype(isolate, map, prototype_parent);
  isolate->UpdateProtectorsOnSetPrototype(prototype, prototype_parent);
  constructor->set_prototype_or_initial_map(*prototype, kReleaseStore);
  map->SetConstructor(*constructor);
  DirectHandle<FixedArray> computed_properties(
      class_boilerplate->instance_computed_properties(), isolate);
  Handle<NumberDictionary> elements_dictionary_template(
      Cast<NumberDictionary>(class_boilerplate->instance_elements_template()),
      isolate);

  Handle<Object> properties_template(
      class_boilerplate->instance_properties_template(), isolate);

  if (IsDescriptorArray(*properties_template)) {
    auto descriptors_template = Cast<DescriptorArray>(properties_template);

    // The size of the prototype object is known at this point.
    // So we can create it now and then add the rest instance methods to the
    // map.
    return AddDescriptorsByTemplate(isolate, map, descriptors_template,
                                    elements_dictionary_template, prototype,
                                    args);
  } else {
    map->set_is_dictionary_map(true);
    map->set_is_migration_target(false);
    map->set_may_have_interesting_properties(true);
    map->set_construction_counter(Map::kNoSlackTracking);

    auto properties_dictionary_template =
        Cast<PropertyDictionary>(properties_template);
    return AddDescriptorsByTemplate(
        isolate, map, properties_dictionary_template,
        elements_dictionary_template, computed_properties, prototype, args);
  }
}

bool InitClassConstructor(Isolate* isolate,
                          DirectHandle<ClassBoilerplate> class_boilerplate,
                          Handle<HeapObject> constructor_parent,
                          Handle<JSFunction> constructor,
                          RuntimeArguments& args) {
  Handle<Map> map(constructor->map(), isolate);
  map = Map::CopyDropDescriptors(isolate, map);
  DCHECK(map->is_prototype_map());

  if (!constructor_parent.is_null()) {
    // Set map's prototype without enabling prototype setup mode for superclass
    // because it does not make sense.
    Map::SetPrototype(isolate, map, constructor_parent, false);
    // Ensure that setup mode will never be enabled for superclass.
    JSObject::MakePrototypesFast(constructor_parent, kStartAtReceiver, isolate);
  }

  Handle<NumberDictionary> elements_dictionary_template(
      Cast<NumberDictionary>(class_boilerplate->static_elements_template()),
      isolate);
  DirectHandle<FixedArray> computed_properties(
      class_boilerplate->static_computed_properties(), isolate);

  Handle<Object> properties_template(
      class_boilerplate->static_properties_template(), isolate);

  if (IsDescriptorArray(*properties_template)) {
    auto descriptors_template = Cast<DescriptorArray>(properties_template);

    return AddDescriptorsByTemplate(isolate, map, descriptors_template,
                                    elements_dictionary_template, constructor,
                                    args);
  } else {
    map->set_is_dictionary_map(true);
    map->InitializeDescriptors(isolate,
                               ReadOnlyRoots(isolate).empty_descriptor_array());
    map->set_is_migration_target(false);
    map->set_may_have_interesting_properties(true);
    map->set_construction_counter(Map::kNoSlackTracking);

    if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
      auto properties_dictionary_template =
          Cast<SwissNameDictionary>(properties_template);

      return AddDescriptorsByTemplate(
          isolate, map, properties_dictionary_template,
          elements_dictionary_template, computed_properties, constructor, args);
    } else {
      auto properties_dictionary_template =
          Cast<NameDictionary>(properties_template);
      return AddDescriptorsByTemplate(
          isolate, map, properties_dictionary_template,
          elements_dictionary_template, computed_properties, constructor, args);
    }
  }
}

MaybeHandle<Object> DefineClass(
    Isolate* isolate, DirectHandle<ClassBoilerplate> class_boilerplate,
    Handle<Object> super_class, Handle<JSFunction> constructor,
    RuntimeArguments& args) {
  Handle<Object> prototype_parent;
  Handle<HeapObject> constructor_parent;

  if (IsTheHole(*super_class, isolate)) {
    prototype_parent = isolate->initial_object_prototype();
  } else {
    if (IsNull(*super_class, isolate)) {
      prototype_parent = isolate->factory()->null_value();
    } else if (IsConstructor(*super_class)) {
      DCHECK(!IsJSFunction(*super_class) ||
             !IsResumableFunction(
                 Cast<JSFunction>(super_class)->shared()->kind()));
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, prototype_parent,
          Runtime::GetObjectProperty(isolate, super_class,
                                     isolate->factory()->prototype_string()));
      if (!IsNull(*prototype_parent, isolate) &&
          !IsJSReceiver(*prototype_parent)) {
        THROW_NEW_ERROR(
            isolate, NewTypeError(MessageTemplate::kPrototypeParentNotAnObject,
                                  prototype_parent));
      }
      // Create new handle to avoid |constructor_parent| corruption because of
      // |super_class| handle value overwriting via storing to
      // args[ClassBoilerplate::kPrototypeArgumentIndex] below.
      constructor_parent = handle(Cast<HeapObject>(*super_class), isolate);
    } else {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kExtendsValueNotConstructor,
                                   super_class));
    }
  }

  Handle<JSObject> prototype = CreateClassPrototype(isolate);
  DCHECK_EQ(*constructor, args[ClassBoilerplate::kConstructorArgumentIndex]);
  // Temporarily change ClassBoilerplate::kPrototypeArgumentIndex for the
  // subsequent calls, but use a scope to make sure to change it back before
  // returning, to not corrupt the caller's argument frame (in particular, for
  // the interpreter, to not clobber the register frame).
  RuntimeArguments::ChangeValueScope set_prototype_value_scope(
      isolate, &args, ClassBoilerplate::kPrototypeArgumentIndex, *prototype);

  if (!InitClassConstructor(isolate, class_boilerplate, constructor_parent,
                            constructor, args) ||
      !InitClassPrototype(isolate, class_boilerplate, prototype,
                          Cast<HeapObject>(prototype_parent), constructor,
                          args)) {
    DCHECK(isolate->has_exception());
    return MaybeHandle<Object>();
  }
  if (v8_flags.log_maps) {
    Handle<Map> empty_map;
    LOG(isolate,
        MapEvent("InitialMap", empty_map, handle(constructor->map(), isolate),
                 "init class constructor",
                 SharedFunctionInfo::DebugName(
                     isolate, handle(constructor->shared(), isolate))));
    LOG(isolate,
        MapEvent("InitialMap", empty_map, handle(prototype->map(), isolate),
                 "init class prototype"));
  }

  return prototype;
}

}  // namespace

RUNTIME_FUNCTION(Runtime_DefineClass) {
  HandleScope scope(isolate);
  DCHECK_LE(ClassBoilerplate::kFirstDynamicArgumentIndex, args.length());
  DirectHandle<ClassBoilerplate> class_boilerplate =
      args.at<ClassBoilerplate>(0);
  Handle<JSFunction> constructor = args.at<JSFunction>(1);
  Handle<Object> super_class = args.at(2);
  DCHECK_EQ(class_boilerplate->arguments_count(), args.length());

  RETURN_RESULT_OR_FAILURE(
      isolate,
      DefineClass(isolate, class_boilerplate, super_class, constructor, args));
}

namespace {

enum class SuperMode { kLoad, kStore };

MaybeHandle<JSReceiver> GetSuperHolder(Isolate* isolate,
                                       Handle<JSObject> home_object,
                                       SuperMode mode, PropertyKey* key) {
  if (IsAccessCheckNeeded(*home_object) &&
      !isolate->MayAccess(isolate->native_context(), home_object)) {
    RETURN_ON_EXCEPTION(isolate, isolate->ReportFailedAccessCheck(home_object));
    UNREACHABLE();
  }

  PrototypeIterator iter(isolate, home_object);
  Handle<Object> proto = PrototypeIterator::GetCurrent(iter);
  if (!IsJSReceiver(*proto)) {
    MessageTemplate message =
        mode == SuperMode::kLoad
            ? MessageTemplate::kNonObjectPropertyLoadWithProperty
            : MessageTemplate::kNonObjectPropertyStoreWithProperty;
    Handle<Name> name = key->GetName(isolate);
    THROW_NEW_ERROR(isolate, NewTypeError(message, proto, name));
  }
  return Cast<JSReceiver>(proto);
}

MaybeHandle<Object> LoadFromSuper(Isolate* isolate, Handle<Object> receiver,
                                  Handle<JSObject> home_object,
                                  PropertyKey* key) {
  Handle<JSReceiver> holder;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, holder,
      GetSuperHolder(isolate, home_object, SuperMode::kLoad, key));
  LookupIterator it(isolate, receiver, *key, holder);
  Handle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result, Object::GetProperty(&it));
  return result;
}

}  // anonymous namespace

RUNTIME_FUNCTION(Runtime_LoadFromSuper) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> receiver = args.at(0);
  Handle<JSObject> home_object = args.at<JSObject>(1);
  Handle<Name> name = args.at<Name>(2);

  PropertyKey key(isolate, name);

  RETURN_RESULT_OR_FAILURE(isolate,
                           LoadFromSuper(isolate, receiver, home_object, &key));
}


RUNTIME_FUNCTION(Runtime_LoadKeyedFromSuper) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> receiver = args.at(0);
  Handle<JSObject> home_object = args.at<JSObject>(1);
  // TODO(ishell): To improve performance, consider performing the to-string
  // conversion of {key} before calling into the runtime.
  Handle<Object> key = args.at(2);

  bool success;
  PropertyKey lookup_key(isolate, key, &success);
  if (!success) return ReadOnlyRoots(isolate).exception();

  RETURN_RESULT_OR_FAILURE(
      isolate, LoadFromSuper(isolate, receiver, home_object, &lookup_key));
}

namespace {

MaybeHandle<Object> StoreToSuper(Isolate* isolate, Handle<JSObject> home_object,
                                 Handle<Object> receiver, PropertyKey* key,
                                 Handle<Object> value,
                                 StoreOrigin store_origin) {
  Handle<JSReceiver> holder;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, holder,
      GetSuperHolder(isolate, home_object, SuperMode::kStore, key));
  LookupIterator it(isolate, receiver, *key, holder);
  MAYBE_RETURN(Object::SetSuperProperty(&it, value, store_origin),
               MaybeHandle<Object>());
  return value;
}

}  // anonymous namespace

RUNTIME_FUNCTION(Runtime_StoreToSuper) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<Object> receiver = args.at(0);
  Handle<JSObject> home_object = args.at<JSObject>(1);
  Handle<Name> name = args.at<Name>(2);
  Handle<Object> value = args.at(3);

  PropertyKey key(isolate, name);

  RETURN_RESULT_OR_FAILURE(
      isolate, StoreToSuper(isolate, home_object, receiver, &key, value,
                            StoreOrigin::kNamed));
}

RUNTIME_FUNCTION(Runtime_StoreKeyedToSuper) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Handle<Object> receiver = args.at(0);
  Handle<JSObject> home_object = args.at<JSObject>(1);
  // TODO(ishell): To improve performance, consider performing the to-string
  // conversion of {key} before calling into the runtime.
  Handle<Object> key = args.at(2);
  Handle<Object> value = args.at(3);

  bool success;
  PropertyKey lookup_key(isolate, key, &success);
  if (!success) return ReadOnlyRoots(isolate).exception();

  RETURN_RESULT_OR_FAILURE(
      isolate, StoreToSuper(isolate, home_object, receiver, &lookup_key, value,
                            StoreOrigin::kMaybeKeyed));
}

}  // namespace internal
}  // namespace v8
