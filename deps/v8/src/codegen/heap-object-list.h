// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_HEAP_OBJECT_LIST_H_
#define V8_CODEGEN_HEAP_OBJECT_LIST_H_

#define BUILTINS_WITH_SFI_OBJECT_LIST_ADAPTER(V, CamelName, underscore_name, \
                                              ...)                           \
  V(CamelName##SharedFun, underscore_name##_shared_fun, CamelName##SharedFun)

#define BUILTINS_WITH_SFI_OBJECT_LIST(V) \
  BUILTINS_WITH_SFI_LIST_GENERATOR(BUILTINS_WITH_SFI_OBJECT_LIST_ADAPTER, V)

#define HEAP_MUTABLE_IMMOVABLE_OBJECT_LIST(V)                                 \
  V(ArrayIteratorProtector, array_iterator_protector, ArrayIteratorProtector) \
  V(ArraySpeciesProtector, array_species_protector, ArraySpeciesProtector)    \
  V(IsConcatSpreadableProtector, is_concat_spreadable_protector,              \
    IsConcatSpreadableProtector)                                              \
  V(MapIteratorProtector, map_iterator_protector, MapIteratorProtector)       \
  V(NoElementsProtector, no_elements_protector, NoElementsProtector)          \
  V(MegaDOMProtector, mega_dom_protector, MegaDOMProtector)                   \
  V(NumberStringCache, number_string_cache, NumberStringCache)                \
  V(NumberStringNotRegexpLikeProtector,                                       \
    number_string_not_regexp_like_protector,                                  \
    NumberStringNotRegexpLikeProtector)                                       \
  V(PromiseResolveProtector, promise_resolve_protector,                       \
    PromiseResolveProtector)                                                  \
  V(PromiseSpeciesProtector, promise_species_protector,                       \
    PromiseSpeciesProtector)                                                  \
  V(PromiseThenProtector, promise_then_protector, PromiseThenProtector)       \
  V(RegExpSpeciesProtector, regexp_species_protector, RegExpSpeciesProtector) \
  V(SetIteratorProtector, set_iterator_protector, SetIteratorProtector)       \
  V(StringIteratorProtector, string_iterator_protector,                       \
    StringIteratorProtector)                                                  \
  V(StringWrapperToPrimitiveProtector, string_wrapper_to_primitive_protector, \
    StringWrapperToPrimitiveProtector)                                        \
  V(TypedArrayLengthProtector, typed_array_length_protector,                  \
    TypedArrayLengthProtector)                                                \
  V(TypedArraySpeciesProtector, typed_array_species_protector,                \
    TypedArraySpeciesProtector)                                               \
  BUILTINS_WITH_SFI_OBJECT_LIST(V)

#define UNIQUE_INSTANCE_TYPE_IMMUTABLE_IMMOVABLE_MAP_ADAPTER( \
    V, rootIndexName, rootAccessorName, class_name)           \
  V(rootIndexName, rootAccessorName, class_name##Map)

#define HEAP_IMMUTABLE_IMMOVABLE_OBJECT_LIST(V)                              \
  V(AllocationSiteWithoutWeakNextMap, allocation_site_without_weaknext_map,  \
    AllocationSiteWithoutWeakNextMap)                                        \
  V(AllocationSiteWithWeakNextMap, allocation_site_map, AllocationSiteMap)   \
  V(arguments_to_string, arguments_to_string, ArgumentsToString)             \
  V(ArrayListMap, array_list_map, ArrayListMap)                              \
  V(Array_string, Array_string, ArrayString)                                 \
  V(array_to_string, array_to_string, ArrayToString)                         \
  V(BooleanMap, boolean_map, BooleanMap)                                     \
  V(boolean_to_string, boolean_to_string, BooleanToString)                   \
  V(class_fields_symbol, class_fields_symbol, ClassFieldsSymbol)             \
  V(ConsOneByteStringMap, cons_one_byte_string_map, ConsOneByteStringMap)    \
  V(ConsTwoByteStringMap, cons_two_byte_string_map, ConsTwoByteStringMap)    \
  V(constructor_string, constructor_string, ConstructorString)               \
  V(date_to_string, date_to_string, DateToString)                            \
  V(default_string, default_string, DefaultString)                           \
  V(EmptyArrayList, empty_array_list, EmptyArrayList)                        \
  V(EmptyByteArray, empty_byte_array, EmptyByteArray)                        \
  V(EmptyFixedArray, empty_fixed_array, EmptyFixedArray)                     \
  V(EmptyOrderedHashSet, empty_ordered_hash_set, EmptyOrderedHashSet)        \
  V(EmptyScopeInfo, empty_scope_info, EmptyScopeInfo)                        \
  V(EmptyPropertyDictionary, empty_property_dictionary,                      \
    EmptyPropertyDictionary)                                                 \
  V(EmptyOrderedPropertyDictionary, empty_ordered_property_dictionary,       \
    EmptyOrderedPropertyDictionary)                                          \
  V(EmptySwissPropertyDictionary, empty_swiss_property_dictionary,           \
    EmptySwissPropertyDictionary)                                            \
  V(EmptySlowElementDictionary, empty_slow_element_dictionary,               \
    EmptySlowElementDictionary)                                              \
  V(empty_string, empty_string, EmptyString)                                 \
  V(error_to_string, error_to_string, ErrorToString)                         \
  V(error_string, error_string, ErrorString)                                 \
  V(errors_string, errors_string, ErrorsString)                              \
  V(FalseValue, false_value, False)                                          \
  V(FixedArrayMap, fixed_array_map, FixedArrayMap)                           \
  V(FixedCOWArrayMap, fixed_cow_array_map, FixedCOWArrayMap)                 \
  V(Function_string, function_string, FunctionString)                        \
  V(function_to_string, function_to_string, FunctionToString)                \
  V(get_string, get_string, GetString)                                       \
  V(has_instance_symbol, has_instance_symbol, HasInstanceSymbol)             \
  V(has_string, has_string, HasString)                                       \
  V(Infinity_string, Infinity_string, InfinityString)                        \
  V(is_concat_spreadable_symbol, is_concat_spreadable_symbol,                \
    IsConcatSpreadableSymbol)                                                \
  V(Iterator_string, Iterator_string, IteratorString)                        \
  V(iterator_symbol, iterator_symbol, IteratorSymbol)                        \
  V(keys_string, keys_string, KeysString)                                    \
  V(async_iterator_symbol, async_iterator_symbol, AsyncIteratorSymbol)       \
  V(length_string, length_string, LengthString)                              \
  V(ManyClosuresCellMap, many_closures_cell_map, ManyClosuresCellMap)        \
  V(match_symbol, match_symbol, MatchSymbol)                                 \
  V(megamorphic_symbol, megamorphic_symbol, MegamorphicSymbol)               \
  V(mega_dom_symbol, mega_dom_symbol, MegaDOMSymbol)                         \
  V(message_string, message_string, MessageString)                           \
  V(minus_Infinity_string, minus_Infinity_string, MinusInfinityString)       \
  V(MinusZeroValue, minus_zero_value, MinusZero)                             \
  V(name_string, name_string, NameString)                                    \
  V(NanValue, nan_value, Nan)                                                \
  V(NaN_string, NaN_string, NaNString)                                       \
  V(next_string, next_string, NextString)                                    \
  V(NoClosuresCellMap, no_closures_cell_map, NoClosuresCellMap)              \
  V(null_to_string, null_to_string, NullToString)                            \
  V(NullValue, null_value, Null)                                             \
  IF_WASM(V, WasmNull, wasm_null, WasmNull)                                  \
  V(number_string, number_string, NumberString)                              \
  V(number_to_string, number_to_string, NumberToString)                      \
  V(Object_string, Object_string, ObjectString)                              \
  V(object_string, object_string, objectString)                              \
  V(object_to_string, object_to_string, ObjectToString)                      \
  V(SeqOneByteStringMap, seq_one_byte_string_map, SeqOneByteStringMap)       \
  V(OneClosureCellMap, one_closure_cell_map, OneClosureCellMap)              \
  V(OnePointerFillerMap, one_pointer_filler_map, OnePointerFillerMap)        \
  V(PromiseCapabilityMap, promise_capability_map, PromiseCapabilityMap)      \
  V(promise_forwarding_handler_symbol, promise_forwarding_handler_symbol,    \
    PromiseForwardingHandlerSymbol)                                          \
  V(PromiseFulfillReactionJobTaskMap, promise_fulfill_reaction_job_task_map, \
    PromiseFulfillReactionJobTaskMap)                                        \
  V(promise_handled_by_symbol, promise_handled_by_symbol,                    \
    PromiseHandledBySymbol)                                                  \
  V(PromiseReactionMap, promise_reaction_map, PromiseReactionMap)            \
  V(PromiseRejectReactionJobTaskMap, promise_reject_reaction_job_task_map,   \
    PromiseRejectReactionJobTaskMap)                                         \
  V(PromiseResolveThenableJobTaskMap, promise_resolve_thenable_job_task_map, \
    PromiseResolveThenableJobTaskMap)                                        \
  V(prototype_string, prototype_string, PrototypeString)                     \
  V(replace_symbol, replace_symbol, ReplaceSymbol)                           \
  V(regexp_to_string, regexp_to_string, RegexpToString)                      \
  V(resolve_string, resolve_string, ResolveString)                           \
  V(return_string, return_string, ReturnString)                              \
  V(search_symbol, search_symbol, SearchSymbol)                              \
  V(SingleCharacterStringTable, single_character_string_table,               \
    SingleCharacterStringTable)                                              \
  V(size_string, size_string, SizeString)                                    \
  V(species_symbol, species_symbol, SpeciesSymbol)                           \
  V(StaleRegister, stale_register, StaleRegister)                            \
  V(StoreHandler0Map, store_handler0_map, StoreHandler0Map)                  \
  V(string_string, string_string, StringString)                              \
  V(string_to_string, string_to_string, StringToString)                      \
  V(suppressed_string, suppressed_string, SuppressedString)                  \
  V(SeqTwoByteStringMap, seq_two_byte_string_map, SeqTwoByteStringMap)       \
  V(TheHoleValue, the_hole_value, TheHole)                                   \
  V(PropertyCellHoleValue, property_cell_hole_value, PropertyCellHole)       \
  V(HashTableHoleValue, hash_table_hole_value, HashTableHole)                \
  V(PromiseHoleValue, promise_hole_value, PromiseHole)                       \
  V(then_string, then_string, ThenString)                                    \
  V(toJSON_string, toJSON_string, ToJSONString)                              \
  V(toString_string, toString_string, ToStringString)                        \
  V(to_primitive_symbol, to_primitive_symbol, ToPrimitiveSymbol)             \
  V(to_string_tag_symbol, to_string_tag_symbol, ToStringTagSymbol)           \
  V(TrueValue, true_value, True)                                             \
  V(undefined_to_string, undefined_to_string, UndefinedToString)             \
  V(UndefinedValue, undefined_value, Undefined)                              \
  V(uninitialized_symbol, uninitialized_symbol, UninitializedSymbol)         \
  V(valueOf_string, valueOf_string, ValueOfString)                           \
  V(wasm_cross_instance_call_symbol, wasm_cross_instance_call_symbol,        \
    WasmCrossInstanceCallSymbol)                                             \
  V(zero_string, zero_string, ZeroString)                                    \
  UNIQUE_INSTANCE_TYPE_MAP_LIST_GENERATOR(                                   \
      UNIQUE_INSTANCE_TYPE_IMMUTABLE_IMMOVABLE_MAP_ADAPTER, V)

#define HEAP_IMMOVABLE_OBJECT_LIST(V)   \
  HEAP_MUTABLE_IMMOVABLE_OBJECT_LIST(V) \
  HEAP_IMMUTABLE_IMMOVABLE_OBJECT_LIST(V)

#endif  // V8_CODEGEN_HEAP_OBJECT_LIST_H_
