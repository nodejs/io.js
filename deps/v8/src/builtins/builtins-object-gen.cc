// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-object-gen.h"

#include "src/builtins/builtins-inl.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/common/globals.h"
#include "src/heap/factory-inl.h"
#include "src/ic/accessor-assembler.h"
#include "src/ic/keyed-store-generic.h"
#include "src/objects/js-generator.h"
#include "src/objects/js-objects.h"
#include "src/objects/property-descriptor-object.h"
#include "src/objects/property-details.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

class ObjectEntriesValuesBuiltinsAssembler : public ObjectBuiltinsAssembler {
 public:
  explicit ObjectEntriesValuesBuiltinsAssembler(
      compiler::CodeAssemblerState* state)
      : ObjectBuiltinsAssembler(state) {}

 protected:
  enum CollectType { kEntries, kValues };

  TNode<BoolT> IsPropertyEnumerable(TNode<Uint32T> details);

  TNode<BoolT> IsPropertyKindAccessor(TNode<Uint32T> kind);

  TNode<BoolT> IsPropertyKindData(TNode<Uint32T> kind);

  TNode<Uint32T> LoadPropertyKind(TNode<Uint32T> details) {
    return DecodeWord32<PropertyDetails::KindField>(details);
  }

  void GetOwnValuesOrEntries(TNode<Context> context, TNode<Object> maybe_object,
                             CollectType collect_type);

  TNode<JSArray> FastGetOwnValuesOrEntries(
      TNode<Context> context, TNode<JSObject> object,
      Label* if_call_runtime_with_fast_path, Label* if_no_properties,
      CollectType collect_type);

  TNode<JSArray> FinalizeValuesOrEntriesJSArray(
      TNode<Context> context, TNode<FixedArray> values_or_entries,
      TNode<IntPtrT> size, TNode<Map> array_map, Label* if_empty);
};

void ObjectBuiltinsAssembler::ReturnToStringFormat(TNode<Context> context,
                                                   TNode<String> string) {
  TNode<String> lhs = StringConstant("[object ");
  TNode<String> rhs = StringConstant("]");

  Builtin builtin = Builtins::StringAdd(STRING_ADD_CHECK_NONE);

  Return(CallBuiltin(builtin, context,
                     CallBuiltin(builtin, context, lhs, string), rhs));
}

TNode<JSObject> ObjectBuiltinsAssembler::ConstructAccessorDescriptor(
    TNode<Context> context, TNode<Object> getter, TNode<Object> setter,
    TNode<BoolT> enumerable, TNode<BoolT> configurable) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> map = CAST(LoadContextElement(
      native_context, Context::ACCESSOR_PROPERTY_DESCRIPTOR_MAP_INDEX));
  TNode<JSObject> js_desc = AllocateJSObjectFromMap(map);

  StoreObjectFieldNoWriteBarrier(
      js_desc, JSAccessorPropertyDescriptor::kGetOffset, getter);
  StoreObjectFieldNoWriteBarrier(
      js_desc, JSAccessorPropertyDescriptor::kSetOffset, setter);
  StoreObjectFieldNoWriteBarrier(
      js_desc, JSAccessorPropertyDescriptor::kEnumerableOffset,
      SelectBooleanConstant(enumerable));
  StoreObjectFieldNoWriteBarrier(
      js_desc, JSAccessorPropertyDescriptor::kConfigurableOffset,
      SelectBooleanConstant(configurable));

  return js_desc;
}

TNode<JSObject> ObjectBuiltinsAssembler::ConstructDataDescriptor(
    TNode<Context> context, TNode<Object> value, TNode<BoolT> writable,
    TNode<BoolT> enumerable, TNode<BoolT> configurable) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> map = CAST(LoadContextElement(
      native_context, Context::DATA_PROPERTY_DESCRIPTOR_MAP_INDEX));
  TNode<JSObject> js_desc = AllocateJSObjectFromMap(map);

  StoreObjectFieldNoWriteBarrier(js_desc,
                                 JSDataPropertyDescriptor::kValueOffset, value);
  StoreObjectFieldNoWriteBarrier(js_desc,
                                 JSDataPropertyDescriptor::kWritableOffset,
                                 SelectBooleanConstant(writable));
  StoreObjectFieldNoWriteBarrier(js_desc,
                                 JSDataPropertyDescriptor::kEnumerableOffset,
                                 SelectBooleanConstant(enumerable));
  StoreObjectFieldNoWriteBarrier(js_desc,
                                 JSDataPropertyDescriptor::kConfigurableOffset,
                                 SelectBooleanConstant(configurable));

  return js_desc;
}

TNode<BoolT> ObjectEntriesValuesBuiltinsAssembler::IsPropertyEnumerable(
    TNode<Uint32T> details) {
  TNode<Uint32T> attributes =
      DecodeWord32<PropertyDetails::AttributesField>(details);
  return IsNotSetWord32(attributes, PropertyAttributes::DONT_ENUM);
}

TNode<BoolT> ObjectEntriesValuesBuiltinsAssembler::IsPropertyKindAccessor(
    TNode<Uint32T> kind) {
  return Word32Equal(kind,
                     Int32Constant(static_cast<int>(PropertyKind::kAccessor)));
}

TNode<BoolT> ObjectEntriesValuesBuiltinsAssembler::IsPropertyKindData(
    TNode<Uint32T> kind) {
  return Word32Equal(kind,
                     Int32Constant(static_cast<int>(PropertyKind::kData)));
}

void ObjectEntriesValuesBuiltinsAssembler::GetOwnValuesOrEntries(
    TNode<Context> context, TNode<Object> maybe_object,
    CollectType collect_type) {
  TNode<JSReceiver> receiver = ToObject_Inline(context, maybe_object);

  Label if_call_runtime_with_fast_path(this, Label::kDeferred),
      if_call_runtime(this, Label::kDeferred),
      if_no_properties(this, Label::kDeferred);

  TNode<Map> map = LoadMap(receiver);
  GotoIfNot(IsJSObjectMap(map), &if_call_runtime);
  GotoIfMapHasSlowProperties(map, &if_call_runtime);

  TNode<JSObject> object = CAST(receiver);
  TNode<FixedArrayBase> elements = LoadElements(object);
  // If the object has elements, we treat it as slow case.
  // So, we go to runtime call.
  GotoIfNot(IsEmptyFixedArray(elements), &if_call_runtime_with_fast_path);

  TNode<JSArray> result = FastGetOwnValuesOrEntries(
      context, object, &if_call_runtime_with_fast_path, &if_no_properties,
      collect_type);
  Return(result);

  BIND(&if_no_properties);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> array_map =
        LoadJSArrayElementsMap(PACKED_ELEMENTS, native_context);
    TNode<JSArray> empty_array = AllocateJSArray(
        PACKED_ELEMENTS, array_map, IntPtrConstant(0), SmiConstant(0));
    Return(empty_array);
  }

  BIND(&if_call_runtime_with_fast_path);
  {
    // In slow case, we simply call runtime.
    if (collect_type == CollectType::kEntries) {
      Return(CallRuntime(Runtime::kObjectEntries, context, object));
    } else {
      DCHECK(collect_type == CollectType::kValues);
      Return(CallRuntime(Runtime::kObjectValues, context, object));
    }
  }

  BIND(&if_call_runtime);
  {
    // In slow case, we simply call runtime.
    if (collect_type == CollectType::kEntries) {
      Return(
          CallRuntime(Runtime::kObjectEntriesSkipFastPath, context, receiver));
    } else {
      DCHECK(collect_type == CollectType::kValues);
      Return(
          CallRuntime(Runtime::kObjectValuesSkipFastPath, context, receiver));
    }
  }
}

TNode<JSArray> ObjectEntriesValuesBuiltinsAssembler::FastGetOwnValuesOrEntries(
    TNode<Context> context, TNode<JSObject> object,
    Label* if_call_runtime_with_fast_path, Label* if_no_properties,
    CollectType collect_type) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> array_map =
      LoadJSArrayElementsMap(PACKED_ELEMENTS, native_context);
  TNode<Map> map = LoadMap(object);
  TNode<Uint32T> bit_field3 = LoadMapBitField3(map);

  Label if_has_enum_cache(this), if_not_has_enum_cache(this),
      collect_entries(this);
  TNode<IntPtrT> object_enum_length =
      Signed(DecodeWordFromWord32<Map::Bits3::EnumLengthBits>(bit_field3));
  TNode<BoolT> has_enum_cache = WordNotEqual(
      object_enum_length, IntPtrConstant(kInvalidEnumCacheSentinel));

  // In case, we found enum_cache in object,
  // we use it as array_length because it has same size for
  // Object.(entries/values) result array object length.
  // So object_enum_length use less memory space than
  // NumberOfOwnDescriptorsBits value.
  // And in case, if enum_cache_not_found,
  // we call runtime and initialize enum_cache for subsequent call of
  // CSA fast path.
  Branch(has_enum_cache, &if_has_enum_cache, if_call_runtime_with_fast_path);

  BIND(&if_has_enum_cache);
  {
    GotoIf(WordEqual(object_enum_length, IntPtrConstant(0)), if_no_properties);
    TNode<FixedArray> values_or_entries =
        CAST(AllocateFixedArray(PACKED_ELEMENTS, object_enum_length));

    // If in case we have enum_cache,
    // we can't detect accessor of object until loop through descriptors.
    // So if object might have accessor,
    // we will remain invalid addresses of FixedArray.
    // Because in that case, we need to jump to runtime call.
    // So the array filled by the-hole even if enum_cache exists.
    FillFixedArrayWithValue(PACKED_ELEMENTS, values_or_entries,
                            IntPtrConstant(0), object_enum_length,
                            RootIndex::kTheHoleValue);

    TVARIABLE(IntPtrT, var_result_index, IntPtrConstant(0));
    TVARIABLE(IntPtrT, var_descriptor_number, IntPtrConstant(0));
    // Let desc be ? O.[[GetOwnProperty]](key).
    TNode<DescriptorArray> descriptors = LoadMapDescriptors(map);
    Label loop(this, {&var_descriptor_number, &var_result_index}),
        after_loop(this), next_descriptor(this);
    Branch(IntPtrEqual(var_descriptor_number.value(), object_enum_length),
           &after_loop, &loop);

    // We dont use BuildFastLoop.
    // Instead, we use hand-written loop
    // because of we need to use 'continue' functionality.
    BIND(&loop);
    {
      // Currently, we will not invoke getters,
      // so, map will not be changed.
      CSA_DCHECK(this, TaggedEqual(map, LoadMap(object)));
      TNode<IntPtrT> descriptor_entry = var_descriptor_number.value();
      TNode<Name> next_key =
          LoadKeyByDescriptorEntry(descriptors, descriptor_entry);

      // Skip Symbols.
      GotoIf(IsSymbol(next_key), &next_descriptor);

      TNode<Uint32T> details =
          LoadDetailsByDescriptorEntry(descriptors, descriptor_entry);

      TNode<Uint32T> kind = LoadPropertyKind(details);

      // If property is accessor, we escape fast path and call runtime.
      GotoIf(IsPropertyKindAccessor(kind), if_call_runtime_with_fast_path);
      CSA_DCHECK(this, IsPropertyKindData(kind));

      // If desc is not undefined and desc.[[Enumerable]] is true, then skip to
      // the next descriptor.
      GotoIfNot(IsPropertyEnumerable(details), &next_descriptor);

      TVARIABLE(Object, var_property_value, UndefinedConstant());
      TNode<IntPtrT> descriptor_name_index = ToKeyIndex<DescriptorArray>(
          Unsigned(TruncateIntPtrToInt32(var_descriptor_number.value())));

      // Let value be ? Get(O, key).
      LoadPropertyFromFastObject(object, map, descriptors,
                                 descriptor_name_index, details,
                                 &var_property_value);

      // If kind is "value", append value to properties.
      TNode<Object> value = var_property_value.value();

      if (collect_type == CollectType::kEntries) {
        // Let entry be CreateArrayFromList(« key, value »).
        TNode<JSArray> array;
        TNode<FixedArrayBase> elements;
        std::tie(array, elements) = AllocateUninitializedJSArrayWithElements(
            PACKED_ELEMENTS, array_map, SmiConstant(2), base::nullopt,
            IntPtrConstant(2));
        StoreFixedArrayElement(CAST(elements), 0, next_key, SKIP_WRITE_BARRIER);
        StoreFixedArrayElement(CAST(elements), 1, value, SKIP_WRITE_BARRIER);
        value = array;
      }

      StoreFixedArrayElement(values_or_entries, var_result_index.value(),
                             value);
      Increment(&var_result_index);
      Goto(&next_descriptor);

      BIND(&next_descriptor);
      {
        Increment(&var_descriptor_number);
        Branch(IntPtrEqual(var_result_index.value(), object_enum_length),
               &after_loop, &loop);
      }
    }
    BIND(&after_loop);
    return FinalizeValuesOrEntriesJSArray(context, values_or_entries,
                                          var_result_index.value(), array_map,
                                          if_no_properties);
  }
}

TNode<JSArray>
ObjectEntriesValuesBuiltinsAssembler::FinalizeValuesOrEntriesJSArray(
    TNode<Context> context, TNode<FixedArray> result, TNode<IntPtrT> size,
    TNode<Map> array_map, Label* if_empty) {
  CSA_DCHECK(this, IsJSArrayMap(array_map));

  GotoIf(IntPtrEqual(size, IntPtrConstant(0)), if_empty);
  TNode<JSArray> array = AllocateJSArray(array_map, result, SmiTag(size));
  return array;
}

TF_BUILTIN(ObjectPrototypeHasOwnProperty, ObjectBuiltinsAssembler) {
  auto object = Parameter<Object>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kKey);
  auto context = Parameter<Context>(Descriptor::kContext);

  Label call_runtime(this), return_true(this), return_false(this),
      to_primitive(this);

  // Smi receivers do not have own properties, just perform ToPrimitive on the
  // key.
  Label if_objectisnotsmi(this);
  Branch(TaggedIsSmi(object), &to_primitive, &if_objectisnotsmi);
  BIND(&if_objectisnotsmi);

  TNode<HeapObject> heap_object = CAST(object);

  TNode<Map> map = LoadMap(heap_object);
  TNode<Uint16T> instance_type = LoadMapInstanceType(map);

  {
    TVARIABLE(IntPtrT, var_index);
    TVARIABLE(Name, var_unique);

    Label if_index(this, &var_index), if_unique_name(this),
        if_notunique_name(this);
    TryToName(key, &if_index, &var_index, &if_unique_name, &var_unique,
              &call_runtime, &if_notunique_name);

    BIND(&if_unique_name);
    TryHasOwnProperty(heap_object, map, instance_type, var_unique.value(),
                      &return_true, &return_false, &call_runtime);

    BIND(&if_index);
    {
      TryLookupElement(heap_object, map, instance_type, var_index.value(),
                       &return_true, &return_false, &return_false,
                       &call_runtime);
    }

    BIND(&if_notunique_name);
    {
      Label not_in_string_table(this);
      TryInternalizeString(CAST(key), &if_index, &var_index, &if_unique_name,
                           &var_unique, &not_in_string_table, &call_runtime);

      BIND(&not_in_string_table);
      {
        // If the string was not found in the string table, then no regular
        // object can have a property with that name, so return |false|.
        // "Special API objects" with interceptors must take the slow path.
        Branch(IsSpecialReceiverInstanceType(instance_type), &call_runtime,
               &return_false);
      }
    }
  }
  BIND(&to_primitive);
  GotoIf(IsNumber(key), &return_false);
  Branch(IsName(CAST(key)), &return_false, &call_runtime);

  BIND(&return_true);
  Return(TrueConstant());

  BIND(&return_false);
  Return(FalseConstant());

  BIND(&call_runtime);
  Return(CallRuntime(Runtime::kObjectHasOwnProperty, context, object, key));
}

// ES #sec-object.assign
TF_BUILTIN(ObjectAssign, ObjectBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<Object> target = args.GetOptionalArgumentValue(0);

  // 1. Let to be ? ToObject(target).
  TNode<JSReceiver> to = ToObject_Inline(context, target);

  Label done(this);
  // 2. If only one argument was passed, return to.
  GotoIf(UintPtrLessThanOrEqual(args.GetLengthWithoutReceiver(),
                                IntPtrConstant(1)),
         &done);

  // 3. Let sources be the List of argument values starting with the
  //    second argument.
  // 4. For each element nextSource of sources, in ascending index order,
  args.ForEach(
      [=](TNode<Object> next_source) {
        CallBuiltin(Builtin::kSetDataProperties, context, to, next_source);
      },
      IntPtrConstant(1));
  Goto(&done);

  // 5. Return to.
  BIND(&done);
  args.PopAndReturn(to);
}

// ES #sec-object.keys
TF_BUILTIN(ObjectKeys, ObjectBuiltinsAssembler) {
  auto object = Parameter<Object>(Descriptor::kObject);
  auto context = Parameter<Context>(Descriptor::kContext);

  TVARIABLE(Smi, var_length);
  TVARIABLE(FixedArrayBase, var_elements);
  Label if_empty(this, Label::kDeferred), if_empty_elements(this),
      if_fast(this), if_slow(this, Label::kDeferred), if_join(this);

  // Check if the {object} has a usable enum cache.
  GotoIf(TaggedIsSmi(object), &if_slow);

  TNode<Map> object_map = LoadMap(CAST(object));
  TNode<Uint32T> object_bit_field3 = LoadMapBitField3(object_map);
  TNode<UintPtrT> object_enum_length =
      DecodeWordFromWord32<Map::Bits3::EnumLengthBits>(object_bit_field3);
  GotoIf(
      WordEqual(object_enum_length, IntPtrConstant(kInvalidEnumCacheSentinel)),
      &if_slow);

  // Ensure that the {object} doesn't have any elements.
  CSA_DCHECK(this, IsJSObjectMap(object_map));
  TNode<FixedArrayBase> object_elements = LoadElements(CAST(object));
  GotoIf(IsEmptyFixedArray(object_elements), &if_empty_elements);
  Branch(IsEmptySlowElementDictionary(object_elements), &if_empty_elements,
         &if_slow);

  // Check whether there are enumerable properties.
  BIND(&if_empty_elements);
  Branch(WordEqual(object_enum_length, IntPtrConstant(0)), &if_empty, &if_fast);

  // TODO(solanes): These if_xxx here and below seem to be quite similar for
  // ObjectKeys and for ObjectGetOwnPropertyNames. In particular, if_fast seem
  // to be the exact same.
  BIND(&if_fast);
  {
    // The {object} has a usable enum cache, use that.
    TNode<DescriptorArray> object_descriptors = LoadMapDescriptors(object_map);
    TNode<EnumCache> object_enum_cache = LoadObjectField<EnumCache>(
        object_descriptors, DescriptorArray::kEnumCacheOffset);
    auto object_enum_keys = LoadObjectField<FixedArrayBase>(
        object_enum_cache, EnumCache::kKeysOffset);

    // Allocate a JSArray and copy the elements from the {object_enum_keys}.
    TNode<JSArray> array;
    TNode<FixedArrayBase> elements;
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> array_map =
        LoadJSArrayElementsMap(PACKED_ELEMENTS, native_context);
    TNode<IntPtrT> object_enum_length_intptr = Signed(object_enum_length);
    TNode<Smi> array_length = SmiTag(object_enum_length_intptr);
    std::tie(array, elements) = AllocateUninitializedJSArrayWithElements(
        PACKED_ELEMENTS, array_map, array_length, base::nullopt,
        object_enum_length_intptr);
    CopyFixedArrayElements(PACKED_ELEMENTS, object_enum_keys, elements,
                           object_enum_length_intptr, SKIP_WRITE_BARRIER);
    Return(array);
  }

  BIND(&if_empty);
  {
    // The {object} doesn't have any enumerable keys.
    var_length = SmiConstant(0);
    var_elements = EmptyFixedArrayConstant();
    Goto(&if_join);
  }

  BIND(&if_slow);
  {
    // Let the runtime compute the elements.
    TNode<FixedArray> elements =
        CAST(CallRuntime(Runtime::kObjectKeys, context, object));
    var_length = LoadObjectField<Smi>(elements, FixedArray::kLengthOffset);
    var_elements = elements;
    Goto(&if_join);
  }

  BIND(&if_join);
  {
    // Wrap the elements into a proper JSArray and return that.
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> array_map =
        LoadJSArrayElementsMap(PACKED_ELEMENTS, native_context);
    TNode<JSArray> array =
        AllocateJSArray(array_map, var_elements.value(), var_length.value());
    Return(array);
  }
}

// https://github.com/tc39/proposal-accessible-object-hasownproperty
TF_BUILTIN(ObjectHasOwn, ObjectBuiltinsAssembler) {
  // Object.prototype.hasOwnProperty()
  // 1. Let obj be ? ToObject(O).
  // 2. Let key be ? ToPropertyKey(P).
  // 3. Return ? HasOwnProperty(obj, key).
  //
  // ObjectPrototypeHasOwnProperty has similar semantics with steps 1 and 2
  // swapped. We check if ToObject can fail and delegate the rest of the
  // execution to ObjectPrototypeHasOwnProperty.

  auto target = Parameter<Object>(Descriptor::kJSTarget);
  auto new_target = Parameter<Object>(Descriptor::kJSNewTarget);
  auto object = Parameter<Object>(Descriptor::kObject);
  auto key = Parameter<Object>(Descriptor::kKey);
  auto context = Parameter<Context>(Descriptor::kContext);

  // ToObject can only fail when object is undefined or null.
  Label undefined_or_null(this), not_undefined_nor_null(this);
  Branch(IsNullOrUndefined(object), &undefined_or_null,
         &not_undefined_nor_null);

  BIND(&undefined_or_null);
  ThrowTypeError(context, MessageTemplate::kUndefinedOrNullToObject);

  BIND(&not_undefined_nor_null);
  Return(CallBuiltin(Builtin::kObjectPrototypeHasOwnProperty, context, target,
                     new_target, JSParameterCount(1), object, key));
}

// ES #sec-object.getOwnPropertyNames
TF_BUILTIN(ObjectGetOwnPropertyNames, ObjectBuiltinsAssembler) {
  auto object = Parameter<Object>(Descriptor::kObject);
  auto context = Parameter<Context>(Descriptor::kContext);

  TVARIABLE(Smi, var_length);
  TVARIABLE(FixedArrayBase, var_elements);
  Label if_empty(this, Label::kDeferred), if_empty_elements(this),
      if_fast(this), try_fast(this, Label::kDeferred),
      if_slow(this, Label::kDeferred), if_join(this);

  // Take the slow path if the {object} IsCustomElementsReceiverInstanceType or
  // has any elements.
  GotoIf(TaggedIsSmi(object), &if_slow);

  TNode<Map> object_map = LoadMap(CAST(object));
  TNode<Uint16T> instance_type = LoadMapInstanceType(object_map);
  GotoIf(IsCustomElementsReceiverInstanceType(instance_type), &if_slow);
  TNode<FixedArrayBase> object_elements = LoadElements(CAST(object));
  GotoIf(IsEmptyFixedArray(object_elements), &if_empty_elements);
  Branch(IsEmptySlowElementDictionary(object_elements), &if_empty_elements,
         &if_slow);

  // Check if the {object} has a usable enum cache.
  BIND(&if_empty_elements);
  TNode<Uint32T> object_bit_field3 = LoadMapBitField3(object_map);
  TNode<UintPtrT> object_enum_length =
      DecodeWordFromWord32<Map::Bits3::EnumLengthBits>(object_bit_field3);
  GotoIf(
      WordEqual(object_enum_length, IntPtrConstant(kInvalidEnumCacheSentinel)),
      &try_fast);

  // Check whether all own properties are enumerable.
  TNode<UintPtrT> number_descriptors =
      DecodeWordFromWord32<Map::Bits3::NumberOfOwnDescriptorsBits>(
          object_bit_field3);
  GotoIfNot(WordEqual(object_enum_length, number_descriptors), &if_slow);

  // Check whether there are enumerable properties.
  Branch(WordEqual(object_enum_length, IntPtrConstant(0)), &if_empty, &if_fast);

  // TODO(solanes): These if_xxx here and below seem to be quite similar for
  // ObjectKeys and for ObjectGetOwnPropertyNames. In particular, if_fast seem
  // to be the exact same.
  BIND(&if_fast);
  {
    // The {object} has a usable enum cache and all own properties are
    // enumerable, use that.
    TNode<DescriptorArray> object_descriptors = LoadMapDescriptors(object_map);
    TNode<EnumCache> object_enum_cache = LoadObjectField<EnumCache>(
        object_descriptors, DescriptorArray::kEnumCacheOffset);
    auto object_enum_keys = LoadObjectField<FixedArrayBase>(
        object_enum_cache, EnumCache::kKeysOffset);

    // Allocate a JSArray and copy the elements from the {object_enum_keys}.
    TNode<JSArray> array;
    TNode<FixedArrayBase> elements;
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> array_map =
        LoadJSArrayElementsMap(PACKED_ELEMENTS, native_context);
    TNode<IntPtrT> object_enum_length_intptr = Signed(object_enum_length);
    TNode<Smi> array_length = SmiTag(object_enum_length_intptr);
    std::tie(array, elements) = AllocateUninitializedJSArrayWithElements(
        PACKED_ELEMENTS, array_map, array_length, base::nullopt,
        object_enum_length_intptr);
    CopyFixedArrayElements(PACKED_ELEMENTS, object_enum_keys, elements,
                           object_enum_length_intptr, SKIP_WRITE_BARRIER);
    Return(array);
  }

  BIND(&try_fast);
  {
    // Let the runtime compute the elements and try initializing enum cache.
    TNode<FixedArray> elements = CAST(CallRuntime(
        Runtime::kObjectGetOwnPropertyNamesTryFast, context, object));
    var_length = LoadObjectField<Smi>(elements, FixedArray::kLengthOffset);
    var_elements = elements;
    Goto(&if_join);
  }

  BIND(&if_empty);
  {
    // The {object} doesn't have any enumerable keys.
    var_length = SmiConstant(0);
    var_elements = EmptyFixedArrayConstant();
    Goto(&if_join);
  }

  BIND(&if_slow);
  {
    // Let the runtime compute the elements.
    TNode<FixedArray> elements =
        CAST(CallRuntime(Runtime::kObjectGetOwnPropertyNames, context, object));
    var_length = LoadObjectField<Smi>(elements, FixedArray::kLengthOffset);
    var_elements = elements;
    Goto(&if_join);
  }

  BIND(&if_join);
  {
    // Wrap the elements into a proper JSArray and return that.
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> array_map =
        LoadJSArrayElementsMap(PACKED_ELEMENTS, native_context);
    TNode<JSArray> array =
        AllocateJSArray(array_map, var_elements.value(), var_length.value());
    Return(array);
  }
}

TF_BUILTIN(ObjectValues, ObjectEntriesValuesBuiltinsAssembler) {
  auto object = UncheckedParameter<JSObject>(Descriptor::kObject);
  auto context = UncheckedParameter<Context>(Descriptor::kContext);
  GetOwnValuesOrEntries(context, object, CollectType::kValues);
}

TF_BUILTIN(ObjectEntries, ObjectEntriesValuesBuiltinsAssembler) {
  auto object = UncheckedParameter<JSObject>(Descriptor::kObject);
  auto context = UncheckedParameter<Context>(Descriptor::kContext);
  GetOwnValuesOrEntries(context, object, CollectType::kEntries);
}

// ES #sec-object.prototype.isprototypeof
TF_BUILTIN(ObjectPrototypeIsPrototypeOf, ObjectBuiltinsAssembler) {
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto context = Parameter<Context>(Descriptor::kContext);
  Label if_receiverisnullorundefined(this, Label::kDeferred),
      if_valueisnotreceiver(this, Label::kDeferred);

  // We only check whether {value} is a Smi here, so that the
  // prototype chain walk below can safely access the {value}s
  // map. We don't rule out Primitive {value}s, since all of
  // them have null as their prototype, so the chain walk below
  // immediately aborts and returns false anyways.
  GotoIf(TaggedIsSmi(value), &if_valueisnotreceiver);

  {
    TNode<HeapObject> value_heap_object = CAST(value);

    // Check if {receiver} is either null or undefined and in that case,
    // invoke the ToObject builtin, which raises the appropriate error.
    // Otherwise we don't need to invoke ToObject, since {receiver} is
    // either already a JSReceiver, in which case ToObject is a no-op,
    // or it's a Primitive and ToObject would allocate a fresh
    // JSPrimitiveWrapper wrapper, which wouldn't be identical to any existing
    // JSReceiver found in the prototype chain of {value}, hence it will return
    // false no matter if we search for the Primitive {receiver} or
    // a newly allocated JSPrimitiveWrapper wrapper for {receiver}.
    GotoIf(IsNull(receiver), &if_receiverisnullorundefined);
    GotoIf(IsUndefined(receiver), &if_receiverisnullorundefined);

    // Loop through the prototype chain looking for the {receiver}.
    Return(HasInPrototypeChain(context, value_heap_object, receiver));

    BIND(&if_receiverisnullorundefined);
    {
      // If {value} is a primitive HeapObject, we need to return
      // false instead of throwing an exception per order of the
      // steps in the specification, so check that first here.
      GotoIfNot(JSAnyIsNotPrimitive(value_heap_object), &if_valueisnotreceiver);

      // Simulate the ToObject invocation on {receiver}.
      ToObject(context, receiver);
      Unreachable();
    }
  }

  BIND(&if_valueisnotreceiver);
  Return(FalseConstant());
}

TF_BUILTIN(ObjectToString, ObjectBuiltinsAssembler) {
  TVARIABLE(String, var_default);
  TVARIABLE(HeapObject, var_holder);
  TVARIABLE(Map, var_holder_map);

  Label checkstringtag(this), if_arguments(this), if_array(this),
      if_boolean(this), if_date(this), if_error(this), if_function(this),
      if_number(this, Label::kDeferred), if_object(this), if_primitive(this),
      if_proxy(this, {&var_holder, &var_holder_map}, Label::kDeferred),
      if_regexp(this), if_string(this), if_symbol(this, Label::kDeferred),
      if_value(this), if_bigint(this, Label::kDeferred);

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto context = Parameter<Context>(Descriptor::kContext);

  // This is arranged to check the likely cases first.
  GotoIf(TaggedIsSmi(receiver), &if_number);

  TNode<HeapObject> receiver_heap_object = CAST(receiver);
  TNode<Map> receiver_map = LoadMap(receiver_heap_object);
  var_holder = receiver_heap_object;
  var_holder_map = receiver_map;
  TNode<Uint16T> receiver_instance_type = LoadMapInstanceType(receiver_map);
  GotoIf(IsPrimitiveInstanceType(receiver_instance_type), &if_primitive);
  GotoIf(IsFunctionInstanceType(receiver_instance_type), &if_function);
  const struct {
    InstanceType value;
    Label* label;
  } kJumpTable[] = {{JS_OBJECT_TYPE, &if_object},
                    {JS_ARRAY_TYPE, &if_array},
                    {JS_REG_EXP_TYPE, &if_regexp},
                    {JS_ARGUMENTS_OBJECT_TYPE, &if_arguments},
                    {JS_DATE_TYPE, &if_date},
                    {JS_API_OBJECT_TYPE, &if_object},
                    {JS_SPECIAL_API_OBJECT_TYPE, &if_object},
                    {JS_PROXY_TYPE, &if_proxy},
                    {JS_ERROR_TYPE, &if_error},
                    {JS_PRIMITIVE_WRAPPER_TYPE, &if_value}};
  size_t const kNumCases = arraysize(kJumpTable);
  Label* case_labels[kNumCases];
  int32_t case_values[kNumCases];
  for (size_t i = 0; i < kNumCases; ++i) {
    case_labels[i] = kJumpTable[i].label;
    case_values[i] = kJumpTable[i].value;
  }
  Switch(receiver_instance_type, &if_object, case_values, case_labels,
         arraysize(case_values));

  BIND(&if_arguments);
  {
    var_default = ArgumentsToStringConstant();
    Goto(&checkstringtag);
  }

  BIND(&if_array);
  {
    var_default = ArrayToStringConstant();
    Goto(&checkstringtag);
  }

  BIND(&if_boolean);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<JSFunction> boolean_constructor = CAST(
        LoadContextElement(native_context, Context::BOOLEAN_FUNCTION_INDEX));
    TNode<Map> boolean_initial_map = LoadObjectField<Map>(
        boolean_constructor, JSFunction::kPrototypeOrInitialMapOffset);
    TNode<HeapObject> boolean_prototype =
        LoadObjectField<HeapObject>(boolean_initial_map, Map::kPrototypeOffset);
    var_default = BooleanToStringConstant();
    var_holder = boolean_prototype;
    var_holder_map = LoadMap(boolean_prototype);
    Goto(&checkstringtag);
  }

  BIND(&if_date);
  {
    var_default = DateToStringConstant();
    Goto(&checkstringtag);
  }

  BIND(&if_error);
  {
    var_default = ErrorToStringConstant();
    Goto(&checkstringtag);
  }

  BIND(&if_function);
  {
    var_default = FunctionToStringConstant();
    Goto(&checkstringtag);
  }

  BIND(&if_number);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<JSFunction> number_constructor = CAST(
        LoadContextElement(native_context, Context::NUMBER_FUNCTION_INDEX));
    TNode<Map> number_initial_map = LoadObjectField<Map>(
        number_constructor, JSFunction::kPrototypeOrInitialMapOffset);
    TNode<HeapObject> number_prototype =
        LoadObjectField<HeapObject>(number_initial_map, Map::kPrototypeOffset);
    var_default = NumberToStringConstant();
    var_holder = number_prototype;
    var_holder_map = LoadMap(number_prototype);
    Goto(&checkstringtag);
  }

  BIND(&if_object);
  {
    CSA_DCHECK(this, IsJSReceiver(CAST(receiver)));
    var_default = ObjectToStringConstant();
    Goto(&checkstringtag);
  }

  BIND(&if_primitive);
  {
    Label return_undefined(this);

    GotoIf(IsStringInstanceType(receiver_instance_type), &if_string);
    GotoIf(IsBigIntInstanceType(receiver_instance_type), &if_bigint);
    GotoIf(IsBooleanMap(receiver_map), &if_boolean);
    GotoIf(IsHeapNumberMap(receiver_map), &if_number);
    GotoIf(IsSymbolMap(receiver_map), &if_symbol);
    GotoIf(IsUndefined(receiver), &return_undefined);
    CSA_DCHECK(this, IsNull(receiver));
    Return(NullToStringConstant());

    BIND(&return_undefined);
    Return(UndefinedToStringConstant());
  }

  BIND(&if_regexp);
  {
    var_default = RegexpToStringConstant();
    Goto(&checkstringtag);
  }

  BIND(&if_string);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<JSFunction> string_constructor = CAST(
        LoadContextElement(native_context, Context::STRING_FUNCTION_INDEX));
    TNode<Map> string_initial_map = LoadObjectField<Map>(
        string_constructor, JSFunction::kPrototypeOrInitialMapOffset);
    TNode<HeapObject> string_prototype =
        LoadObjectField<HeapObject>(string_initial_map, Map::kPrototypeOffset);
    var_default = StringToStringConstant();
    var_holder = string_prototype;
    var_holder_map = LoadMap(string_prototype);
    Goto(&checkstringtag);
  }

  BIND(&if_symbol);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<JSFunction> symbol_constructor = CAST(
        LoadContextElement(native_context, Context::SYMBOL_FUNCTION_INDEX));
    TNode<Map> symbol_initial_map = LoadObjectField<Map>(
        symbol_constructor, JSFunction::kPrototypeOrInitialMapOffset);
    TNode<HeapObject> symbol_prototype =
        LoadObjectField<HeapObject>(symbol_initial_map, Map::kPrototypeOffset);
    var_default = ObjectToStringConstant();
    var_holder = symbol_prototype;
    var_holder_map = LoadMap(symbol_prototype);
    Goto(&checkstringtag);
  }

  BIND(&if_bigint);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<JSFunction> bigint_constructor = CAST(
        LoadContextElement(native_context, Context::BIGINT_FUNCTION_INDEX));
    TNode<Map> bigint_initial_map = LoadObjectField<Map>(
        bigint_constructor, JSFunction::kPrototypeOrInitialMapOffset);
    TNode<HeapObject> bigint_prototype =
        LoadObjectField<HeapObject>(bigint_initial_map, Map::kPrototypeOffset);
    var_default = ObjectToStringConstant();
    var_holder = bigint_prototype;
    var_holder_map = LoadMap(bigint_prototype);
    Goto(&checkstringtag);
  }

  BIND(&if_value);
  {
    Label if_value_is_number(this, Label::kDeferred),
        if_value_is_boolean(this, Label::kDeferred),
        if_value_is_symbol(this, Label::kDeferred),
        if_value_is_bigint(this, Label::kDeferred),
        if_value_is_string(this, Label::kDeferred);

    TNode<Object> receiver_value =
        LoadJSPrimitiveWrapperValue(CAST(receiver_heap_object));
    // We need to start with the object to see if the value was a subclass
    // which might have interesting properties.
    GotoIf(TaggedIsSmi(receiver_value), &if_value_is_number);
    TNode<Map> receiver_value_map = LoadMap(CAST(receiver_value));
    GotoIf(IsHeapNumberMap(receiver_value_map), &if_value_is_number);
    GotoIf(IsBooleanMap(receiver_value_map), &if_value_is_boolean);
    GotoIf(IsSymbolMap(receiver_value_map), &if_value_is_symbol);
    TNode<Uint16T> receiver_value_instance_type =
        LoadMapInstanceType(receiver_value_map);
    GotoIf(IsBigIntInstanceType(receiver_value_instance_type),
           &if_value_is_bigint);
    CSA_DCHECK(this, IsStringInstanceType(receiver_value_instance_type));
    Goto(&if_value_is_string);

    BIND(&if_value_is_number);
    {
      var_default = NumberToStringConstant();
      Goto(&checkstringtag);
    }

    BIND(&if_value_is_boolean);
    {
      var_default = BooleanToStringConstant();
      Goto(&checkstringtag);
    }

    BIND(&if_value_is_string);
    {
      var_default = StringToStringConstant();
      Goto(&checkstringtag);
    }

    BIND(&if_value_is_bigint);
    {
      var_default = ObjectToStringConstant();
      Goto(&checkstringtag);
    }

    BIND(&if_value_is_symbol);
    {
      var_default = ObjectToStringConstant();
      Goto(&checkstringtag);
    }
  }

  BIND(&checkstringtag);
  {
    Label return_default(this);
    TNode<Object> tag =
        GetInterestingProperty(context, receiver, &var_holder, &var_holder_map,
                               ToStringTagSymbolConstant(), &return_default);
    GotoIf(TaggedIsSmi(tag), &return_default);
    GotoIfNot(IsString(CAST(tag)), &return_default);
    ReturnToStringFormat(context, CAST(tag));

    BIND(&return_default);
    Return(var_default.value());
  }

  BIND(&if_proxy);
  {
    receiver_heap_object = var_holder.value();
    receiver_map = var_holder_map.value();
    // Check if the proxy has been revoked.
    Label throw_proxy_handler_revoked(this, Label::kDeferred);
    TNode<HeapObject> handler =
        CAST(LoadObjectField(receiver_heap_object, JSProxy::kHandlerOffset));
    CSA_DCHECK(this, IsNullOrJSReceiver(handler));
    GotoIfNot(JSAnyIsNotPrimitive(handler), &throw_proxy_handler_revoked);

    // If {receiver_heap_object} is a proxy for a JSArray, we default to
    // "[object Array]", otherwise we default to "[object Object]" or "[object
    // Function]" here, depending on whether the {receiver_heap_object} is
    // callable. The order matters here, i.e. we need to execute the
    // %ArrayIsArray check before the [[Get]] below, as the exception is
    // observable.
    TNode<Object> receiver_is_array =
        CallRuntime(Runtime::kArrayIsArray, context, receiver_heap_object);
    TNode<String> builtin_tag = Select<String>(
        IsTrue(receiver_is_array), [=] { return ArrayStringConstant(); },
        [=] {
          return Select<String>(
              IsCallableMap(receiver_map),
              [=] { return FunctionStringConstant(); },
              [=] { return ObjectStringConstant(); });
        });

    // Lookup the @@toStringTag property on the {receiver_heap_object}.
    TVARIABLE(Object, var_tag,
              GetProperty(context, receiver_heap_object,
                          isolate()->factory()->to_string_tag_symbol()));
    Label if_tagisnotstring(this), if_tagisstring(this);
    GotoIf(TaggedIsSmi(var_tag.value()), &if_tagisnotstring);
    Branch(IsString(CAST(var_tag.value())), &if_tagisstring,
           &if_tagisnotstring);
    BIND(&if_tagisnotstring);
    {
      var_tag = builtin_tag;
      Goto(&if_tagisstring);
    }
    BIND(&if_tagisstring);
    ReturnToStringFormat(context, CAST(var_tag.value()));

    BIND(&throw_proxy_handler_revoked);
    {
      ThrowTypeError(context, MessageTemplate::kProxyRevoked,
                     "Object.prototype.toString");
    }
  }
}

// ES #sec-object.create
TF_BUILTIN(ObjectCreate, ObjectBuiltinsAssembler) {
  int const kPrototypeArg = 0;
  int const kPropertiesArg = 1;

  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  TNode<Object> prototype = args.GetOptionalArgumentValue(kPrototypeArg);
  TNode<Object> properties = args.GetOptionalArgumentValue(kPropertiesArg);
  auto native_context = Parameter<NativeContext>(Descriptor::kContext);

  Label call_runtime(this, Label::kDeferred), prototype_valid(this),
      no_properties(this);

  {
    Comment("Argument 1 check: prototype");
    GotoIf(IsNull(prototype), &prototype_valid);
    BranchIfJSReceiver(prototype, &prototype_valid, &call_runtime);
  }

  BIND(&prototype_valid);
  {
    Comment("Argument 2 check: properties");
    // Check that we have a simple object
    GotoIf(TaggedIsSmi(properties), &call_runtime);
    // Undefined implies no properties.
    GotoIf(IsUndefined(properties), &no_properties);
    TNode<Map> properties_map = LoadMap(CAST(properties));
    GotoIf(IsSpecialReceiverMap(properties_map), &call_runtime);
    // Stay on the fast path only if there are no elements.
    GotoIfNot(
        TaggedEqual(LoadElements(CAST(properties)), EmptyFixedArrayConstant()),
        &call_runtime);
    // Handle dictionary objects or fast objects with properties in runtime.
    TNode<Uint32T> bit_field3 = LoadMapBitField3(properties_map);
    GotoIf(IsSetWord32<Map::Bits3::IsDictionaryMapBit>(bit_field3),
           &call_runtime);
    Branch(IsSetWord32<Map::Bits3::NumberOfOwnDescriptorsBits>(bit_field3),
           &call_runtime, &no_properties);
  }

  // Create a new object with the given prototype.
  BIND(&no_properties);
  {
    TVARIABLE(Map, map);
    TVARIABLE(HeapObject, new_properties);
    Label null_proto(this), non_null_proto(this), instantiate_map(this);

    Branch(IsNull(prototype), &null_proto, &non_null_proto);

    BIND(&null_proto);
    {
      map = LoadSlowObjectWithNullPrototypeMap(native_context);
      new_properties =
          AllocatePropertyDictionary(PropertyDictionary::kInitialCapacity);
      Goto(&instantiate_map);
    }

    BIND(&non_null_proto);
    {
      new_properties = EmptyFixedArrayConstant();
      map = LoadObjectFunctionInitialMap(native_context);
      GotoIf(TaggedEqual(prototype, LoadMapPrototype(map.value())),
             &instantiate_map);
      // Try loading the prototype info.
      TNode<PrototypeInfo> prototype_info =
          LoadMapPrototypeInfo(LoadMap(CAST(prototype)), &call_runtime);
      Comment("Load ObjectCreateMap from PrototypeInfo");
      TNode<HeapObject> derived_maps = CAST(
          LoadObjectField(prototype_info, PrototypeInfo::kDerivedMapsOffset));
      // In case it exists, derived maps is a weak array list where the first
      // element is the object create map.
      GotoIf(TaggedEqual(derived_maps, UndefinedConstant()), &call_runtime);
      CSA_DCHECK(this, InstanceTypeEqual(LoadInstanceType(derived_maps),
                                         WEAK_ARRAY_LIST_TYPE));
      TNode<MaybeObject> maybe_map = UncheckedCast<MaybeObject>(LoadObjectField(
          derived_maps, IntPtrConstant(WeakArrayList::kHeaderSize)));
      map = CAST(GetHeapObjectAssumeWeak(maybe_map, &call_runtime));
      Goto(&instantiate_map);
    }

    BIND(&instantiate_map);
    {
      TNode<JSObject> instance =
          AllocateJSObjectFromMap(map.value(), new_properties.value());
      args.PopAndReturn(instance);
    }
  }

  BIND(&call_runtime);
  {
    TNode<Object> result = CallRuntime(Runtime::kObjectCreate, native_context,
                                       prototype, properties);
    args.PopAndReturn(result);
  }
}

// ES #sec-object.is
TF_BUILTIN(ObjectIs, ObjectBuiltinsAssembler) {
  const auto left = Parameter<Object>(Descriptor::kLeft);
  const auto right = Parameter<Object>(Descriptor::kRight);

  Label return_true(this), return_false(this);
  BranchIfSameValue(left, right, &return_true, &return_false);

  BIND(&return_true);
  Return(TrueConstant());

  BIND(&return_false);
  Return(FalseConstant());
}

TF_BUILTIN(CreateIterResultObject, ObjectBuiltinsAssembler) {
  const auto value = Parameter<Object>(Descriptor::kValue);
  const auto done = Parameter<Boolean>(Descriptor::kDone);
  const auto context = Parameter<Context>(Descriptor::kContext);

  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Map> map = CAST(
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX));

  const TNode<JSObject> result = AllocateJSObjectFromMap(map);

  StoreObjectFieldNoWriteBarrier(result, JSIteratorResult::kValueOffset, value);
  StoreObjectFieldNoWriteBarrier(result, JSIteratorResult::kDoneOffset, done);

  Return(result);
}

TF_BUILTIN(HasProperty, ObjectBuiltinsAssembler) {
  auto key = Parameter<Object>(Descriptor::kKey);
  auto object = Parameter<Object>(Descriptor::kObject);
  auto context = Parameter<Context>(Descriptor::kContext);

  Return(HasProperty(context, object, key, kHasProperty));
}

TF_BUILTIN(InstanceOf, ObjectBuiltinsAssembler) {
  auto object = Parameter<Object>(Descriptor::kLeft);
  auto callable = Parameter<Object>(Descriptor::kRight);
  auto context = Parameter<Context>(Descriptor::kContext);

  Return(InstanceOf(object, callable, context));
}

TF_BUILTIN(InstanceOf_WithFeedback, ObjectBuiltinsAssembler) {
  auto object = Parameter<Object>(Descriptor::kLeft);
  auto callable = Parameter<Object>(Descriptor::kRight);
  auto context = Parameter<Context>(Descriptor::kContext);
  auto feedback_vector = Parameter<HeapObject>(Descriptor::kFeedbackVector);
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);

  CollectInstanceOfFeedback(callable, context, feedback_vector, slot);
  Return(InstanceOf(object, callable, context));
}

TF_BUILTIN(InstanceOf_Baseline, ObjectBuiltinsAssembler) {
  auto object = Parameter<Object>(Descriptor::kLeft);
  auto callable = Parameter<Object>(Descriptor::kRight);
  auto context = LoadContextFromBaseline();
  auto feedback_vector = LoadFeedbackVectorFromBaseline();
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);

  CollectInstanceOfFeedback(callable, context, feedback_vector, slot);
  Return(InstanceOf(object, callable, context));
}

// ES6 section 7.3.19 OrdinaryHasInstance ( C, O )
TF_BUILTIN(OrdinaryHasInstance, ObjectBuiltinsAssembler) {
  auto constructor = Parameter<Object>(Descriptor::kLeft);
  auto object = Parameter<Object>(Descriptor::kRight);
  auto context = Parameter<Context>(Descriptor::kContext);

  Return(OrdinaryHasInstance(context, constructor, object));
}

TF_BUILTIN(CreateGeneratorObject, ObjectBuiltinsAssembler) {
  auto closure = Parameter<JSFunction>(Descriptor::kClosure);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto context = Parameter<Context>(Descriptor::kContext);

  // Get the initial map from the function, jumping to the runtime if we don't
  // have one.
  Label done(this), runtime(this);
  GotoIfNot(IsFunctionWithPrototypeSlotMap(LoadMap(closure)), &runtime);
  TNode<HeapObject> maybe_map = LoadObjectField<HeapObject>(
      closure, JSFunction::kPrototypeOrInitialMapOffset);
  GotoIf(DoesntHaveInstanceType(maybe_map, MAP_TYPE), &runtime);
  TNode<Map> map = CAST(maybe_map);

  TNode<SharedFunctionInfo> shared = LoadObjectField<SharedFunctionInfo>(
      closure, JSFunction::kSharedFunctionInfoOffset);
  TNode<BytecodeArray> bytecode_array =
      LoadSharedFunctionInfoBytecodeArray(shared);

  TNode<IntPtrT> formal_parameter_count = ChangeInt32ToIntPtr(
      LoadSharedFunctionInfoFormalParameterCountWithoutReceiver(shared));
  TNode<IntPtrT> frame_size = ChangeInt32ToIntPtr(
      LoadObjectField<Int32T>(bytecode_array, BytecodeArray::kFrameSizeOffset));
  TNode<IntPtrT> size =
      IntPtrAdd(WordSar(frame_size, IntPtrConstant(kTaggedSizeLog2)),
                formal_parameter_count);
  TNode<FixedArrayBase> parameters_and_registers =
      AllocateFixedArray(HOLEY_ELEMENTS, size);
  FillFixedArrayWithValue(HOLEY_ELEMENTS, parameters_and_registers,
                          IntPtrConstant(0), size, RootIndex::kUndefinedValue);
  // TODO(cbruni): support start_offset to avoid double initialization.
  TNode<JSObject> result =
      AllocateJSObjectFromMap(map, base::nullopt, base::nullopt,
                              AllocationFlag::kNone, kWithSlackTracking);
  StoreObjectFieldNoWriteBarrier(result, JSGeneratorObject::kFunctionOffset,
                                 closure);
  StoreObjectFieldNoWriteBarrier(result, JSGeneratorObject::kContextOffset,
                                 context);
  StoreObjectFieldNoWriteBarrier(result, JSGeneratorObject::kReceiverOffset,
                                 receiver);
  StoreObjectFieldNoWriteBarrier(
      result, JSGeneratorObject::kParametersAndRegistersOffset,
      parameters_and_registers);
  TNode<Smi> resume_mode = SmiConstant(JSGeneratorObject::ResumeMode::kNext);
  StoreObjectFieldNoWriteBarrier(result, JSGeneratorObject::kResumeModeOffset,
                                 resume_mode);
  TNode<Smi> executing = SmiConstant(JSGeneratorObject::kGeneratorExecuting);
  StoreObjectFieldNoWriteBarrier(result, JSGeneratorObject::kContinuationOffset,
                                 executing);
  GotoIfNot(InstanceTypeEqual(LoadMapInstanceType(map),
                              JS_ASYNC_GENERATOR_OBJECT_TYPE),
            &done);
  StoreObjectFieldNoWriteBarrier(
      result, JSAsyncGeneratorObject::kIsAwaitingOffset, SmiConstant(0));
  Goto(&done);

  BIND(&done);
  { Return(result); }

  BIND(&runtime);
  {
    Return(CallRuntime(Runtime::kCreateJSGeneratorObject, context, closure,
                       receiver));
  }
}

TF_BUILTIN(OrdinaryGetOwnPropertyDescriptor, ObjectBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto object = Parameter<JSReceiver>(Descriptor::kReceiver);
  auto name = Parameter<Name>(Descriptor::kKey);
  CSA_DCHECK(this, Word32BinaryNot(IsSpecialReceiverInstanceType(
                       LoadMapInstanceType(LoadMap(object)))));

  Label if_notunique_name(this), if_iskeyunique(this), done(this),
      if_keyisindex(this), call_runtime(this);

  TVARIABLE(IntPtrT, var_index, IntPtrConstant(0));
  TVARIABLE(Name, var_name, name);
  TVARIABLE(HeapObject, result, UndefinedConstant());

  TryToName(name, &if_keyisindex, &var_index, &if_iskeyunique, &var_name,
            &call_runtime, &if_notunique_name);

  BIND(&if_notunique_name);
  {
    Label not_in_string_table(this);
    // If the string was not found in the string table, then no regular
    // object can have a property with that name, so return |undefined|.
    TryInternalizeString(CAST(name), &if_keyisindex, &var_index,
                         &if_iskeyunique, &var_name, &done, &call_runtime);
  }

  BIND(&if_iskeyunique);
  {
    Label if_found_value(this), if_not_found(this);

    TVARIABLE(Object, var_value);
    TVARIABLE(Uint32T, var_details);
    TVARIABLE(Object, var_raw_value);
    TNode<Map> map = LoadMap(object);
    TNode<Int32T> instance_type = LoadMapInstanceType(map);

    TryGetOwnProperty(context, object, object, map, instance_type,
                      var_name.value(), &if_found_value, &var_value,
                      &var_details, &var_raw_value, &done, &call_runtime,
                      kReturnAccessorPair);

    BIND(&if_found_value);

    // 4. Return FromPropertyDetails(desc).
    result = AllocatePropertyDescriptorObject(context);
    InitializePropertyDescriptorObject(CAST(result.value()), var_value.value(),
                                       var_details.value(), &call_runtime);
    Goto(&done);
  }

  BIND(&done);
  Return(result.value());

  BIND(&if_keyisindex);
  Goto(&call_runtime);

  BIND(&call_runtime);
  TailCallRuntime(Runtime::kGetOwnPropertyDescriptorObject, context, object,
                  var_name.value());
}

// ES6 section 19.1.2.7 Object.getOwnPropertyDescriptor ( O, P )
TF_BUILTIN(ObjectGetOwnPropertyDescriptor, ObjectBuiltinsAssembler) {
  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  auto context = Parameter<Context>(Descriptor::kContext);
  CSA_DCHECK(this, IsUndefined(Parameter<Object>(Descriptor::kJSNewTarget)));

  CodeStubArguments args(this, argc);
  TNode<Object> object_input = args.GetOptionalArgumentValue(0);
  TNode<Object> key = args.GetOptionalArgumentValue(1);

  // 1. Let obj be ? ToObject(O).
  TNode<JSReceiver> object = ToObject_Inline(context, object_input);

  // 2. Let key be ? ToPropertyKey(P).
  key = CallBuiltin(Builtin::kToName, context, key);

  // 3. Let desc be ? obj.[[GetOwnProperty]](key).
  TNode<Object> desc =
      CallBuiltin(Builtin::kGetOwnPropertyDescriptor, context, object, key);

  // 4. Return FromPropertyDescriptor(desc).
  TNode<HeapObject> result = FromPropertyDescriptor(context, desc);

  args.PopAndReturn(result);
}

// TODO(v8:11167) remove remove |context| and |object| parameters once
// OrderedNameDictionary supported.
void ObjectBuiltinsAssembler::AddToDictionaryIf(
    TNode<BoolT> condition, TNode<Context> context, TNode<Object> object,
    TNode<HeapObject> name_dictionary, Handle<Name> name, TNode<Object> value,
    Label* bailout) {
  Label done(this);
  GotoIfNot(condition, &done);

  AddToDictionary<PropertyDictionary>(CAST(name_dictionary),
                                      HeapConstantNoHole(name), value, bailout);
  Goto(&done);

  BIND(&done);
}

TNode<JSObject> ObjectBuiltinsAssembler::FromPropertyDescriptor(
    TNode<Context> context, TNode<PropertyDescriptorObject> desc) {
  TVARIABLE(JSObject, js_descriptor);

  TNode<Int32T> flags = LoadAndUntagToWord32ObjectField(
      desc, PropertyDescriptorObject::kFlagsOffset);

  TNode<Int32T> has_flags =
      Word32And(flags, Int32Constant(PropertyDescriptorObject::kHasMask));

  Label if_accessor_desc(this), if_data_desc(this), if_generic_desc(this),
      return_desc(this);
  GotoIf(
      Word32Equal(has_flags,
                  Int32Constant(
                      PropertyDescriptorObject::kRegularAccessorPropertyBits)),
      &if_accessor_desc);
  GotoIf(Word32Equal(
             has_flags,
             Int32Constant(PropertyDescriptorObject::kRegularDataPropertyBits)),
         &if_data_desc);
  Goto(&if_generic_desc);

  BIND(&if_accessor_desc);
  {
    js_descriptor = ConstructAccessorDescriptor(
        context, LoadObjectField(desc, PropertyDescriptorObject::kGetOffset),
        LoadObjectField(desc, PropertyDescriptorObject::kSetOffset),
        IsSetWord32<PropertyDescriptorObject::IsEnumerableBit>(flags),
        IsSetWord32<PropertyDescriptorObject::IsConfigurableBit>(flags));
    Goto(&return_desc);
  }

  BIND(&if_data_desc);
  {
    js_descriptor = ConstructDataDescriptor(
        context, LoadObjectField(desc, PropertyDescriptorObject::kValueOffset),
        IsSetWord32<PropertyDescriptorObject::IsWritableBit>(flags),
        IsSetWord32<PropertyDescriptorObject::IsEnumerableBit>(flags),
        IsSetWord32<PropertyDescriptorObject::IsConfigurableBit>(flags));
    Goto(&return_desc);
  }

  BIND(&if_generic_desc);
  {
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> map = CAST(LoadContextElement(
        native_context, Context::SLOW_OBJECT_WITH_OBJECT_PROTOTYPE_MAP));
    // We want to preallocate the slots for value, writable, get, set,
    // enumerable and configurable - a total of 6
    TNode<HeapObject> properties = AllocatePropertyDictionary(6);
    TNode<JSObject> js_desc = AllocateJSObjectFromMap(map, properties);

    Label bailout(this, Label::kDeferred);

    Factory* factory = isolate()->factory();
    TNode<Object> value =
        LoadObjectField(desc, PropertyDescriptorObject::kValueOffset);
    AddToDictionaryIf(IsNotTheHole(value), context, js_desc, properties,
                      factory->value_string(), value, &bailout);
    AddToDictionaryIf(
        IsSetWord32<PropertyDescriptorObject::HasWritableBit>(flags), context,
        js_desc, properties, factory->writable_string(),
        SelectBooleanConstant(
            IsSetWord32<PropertyDescriptorObject::IsWritableBit>(flags)),
        &bailout);

    TNode<Object> get =
        LoadObjectField(desc, PropertyDescriptorObject::kGetOffset);
    AddToDictionaryIf(IsNotTheHole(get), context, js_desc, properties,
                      factory->get_string(), get, &bailout);
    TNode<Object> set =
        LoadObjectField(desc, PropertyDescriptorObject::kSetOffset);
    AddToDictionaryIf(IsNotTheHole(set), context, js_desc, properties,
                      factory->set_string(), set, &bailout);

    AddToDictionaryIf(
        IsSetWord32<PropertyDescriptorObject::HasEnumerableBit>(flags), context,
        js_desc, properties, factory->enumerable_string(),
        SelectBooleanConstant(
            IsSetWord32<PropertyDescriptorObject::IsEnumerableBit>(flags)),
        &bailout);
    AddToDictionaryIf(
        IsSetWord32<PropertyDescriptorObject::HasConfigurableBit>(flags),
        context, js_desc, properties, factory->configurable_string(),
        SelectBooleanConstant(
            IsSetWord32<PropertyDescriptorObject::IsConfigurableBit>(flags)),
        &bailout);

    js_descriptor = js_desc;
    Goto(&return_desc);

    BIND(&bailout);
    CSA_DCHECK(this, Int32Constant(0));
    Unreachable();
  }

  BIND(&return_desc);
  return js_descriptor.value();
}

TNode<HeapObject> ObjectBuiltinsAssembler::FromPropertyDescriptor(
    TNode<Context> context, TNode<Object> desc) {
  CSA_DCHECK(this, TaggedIsNotSmi(desc));

  if (IsUndefinedConstant(desc)) return UndefinedConstant();

  Label done(this);
  TVARIABLE(HeapObject, result, UndefinedConstant());
  GotoIf(IsUndefined(desc), &done);

  TNode<PropertyDescriptorObject> property_descriptor = CAST(desc);
  result = FromPropertyDescriptor(context, property_descriptor);
  Goto(&done);

  BIND(&done);
  return result.value();
}

TNode<JSObject> ObjectBuiltinsAssembler::FromPropertyDetails(
    TNode<Context> context, TNode<Object> raw_value, TNode<Word32T> details,
    Label* if_bailout) {
  TVARIABLE(JSObject, js_descriptor);

  Label if_accessor_desc(this), if_data_desc(this), return_desc(this);
  BranchIfAccessorPair(raw_value, &if_accessor_desc, &if_data_desc);

  BIND(&if_accessor_desc);
  {
    TNode<AccessorPair> accessor_pair_value = CAST(raw_value);
    TNode<HeapObject> getter = LoadObjectField<HeapObject>(
        accessor_pair_value, AccessorPair::kGetterOffset);
    TNode<HeapObject> setter = LoadObjectField<HeapObject>(
        accessor_pair_value, AccessorPair::kSetterOffset);
    js_descriptor = ConstructAccessorDescriptor(
        context, GetAccessorOrUndefined(getter, if_bailout),
        GetAccessorOrUndefined(setter, if_bailout),
        IsNotSetWord32(details, PropertyDetails::kAttributesDontEnumMask),
        IsNotSetWord32(details, PropertyDetails::kAttributesDontDeleteMask));
    Goto(&return_desc);
  }

  BIND(&if_data_desc);
  {
    js_descriptor = ConstructDataDescriptor(
        context, raw_value,
        IsNotSetWord32(details, PropertyDetails::kAttributesReadOnlyMask),
        IsNotSetWord32(details, PropertyDetails::kAttributesDontEnumMask),
        IsNotSetWord32(details, PropertyDetails::kAttributesDontDeleteMask));
    Goto(&return_desc);
  }

  BIND(&return_desc);
  return js_descriptor.value();
}

TNode<HeapObject> ObjectBuiltinsAssembler::GetAccessorOrUndefined(
    TNode<HeapObject> accessor, Label* if_bailout) {
  Label bind_undefined(this, Label::kDeferred), return_result(this);
  TVARIABLE(HeapObject, result);

  GotoIf(IsNull(accessor), &bind_undefined);
  result = accessor;
  TNode<Map> map = LoadMap(accessor);
  // TODO(ishell): probe template instantiations cache.
  GotoIf(IsFunctionTemplateInfoMap(map), if_bailout);
  Goto(&return_result);

  BIND(&bind_undefined);
  result = UndefinedConstant();
  Goto(&return_result);

  BIND(&return_result);
  return result.value();
}
}  // namespace internal
}  // namespace v8
