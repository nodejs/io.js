// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CONTEXTS_H_
#define V8_OBJECTS_CONTEXTS_H_

#include "include/v8-promise.h"
#include "src/objects/fixed-array.h"
#include "src/objects/function-kind.h"
#include "src/objects/ordered-hash-table.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class ConstTrackingLetCell;
class JSGlobalObject;
class JSGlobalProxy;
class MicrotaskQueue;
class NativeContext;
class RegExpMatchInfo;
struct VariableLookupResult;

enum ContextLookupFlags {
  FOLLOW_CONTEXT_CHAIN = 1 << 0,
  FOLLOW_PROTOTYPE_CHAIN = 1 << 1,

  DONT_FOLLOW_CHAINS = 0,
  FOLLOW_CHAINS = FOLLOW_CONTEXT_CHAIN | FOLLOW_PROTOTYPE_CHAIN,
};

// Heap-allocated activation contexts.
//
// Contexts are implemented as FixedArray-like objects having a fixed
// header with a set of common fields.
//
// Note: Context must have no virtual functions and Context objects
// must always be allocated via Heap::AllocateContext() or
// Factory::NewContext.

#define NATIVE_CONTEXT_FIELDS(V)                                               \
  V(GLOBAL_PROXY_INDEX, JSGlobalProxy, global_proxy_object)                    \
  /* TODO(ishell): Actually we store exactly EmbedderDataArray here but */     \
  /* it's already UBSan-fiendly and doesn't require a star... So declare */    \
  /* it as a HeapObject for now. */                                            \
  V(EMBEDDER_DATA_INDEX, HeapObject, embedder_data)                            \
  V(CONTINUATION_PRESERVED_EMBEDDER_DATA_INDEX, HeapObject,                    \
    continuation_preserved_embedder_data)                                      \
  V(GENERATOR_NEXT_INTERNAL, JSFunction, generator_next_internal)              \
  V(ASYNC_MODULE_EVALUATE_INTERNAL, JSFunction,                                \
    async_module_evaluate_internal)                                            \
  V(REFLECT_APPLY_INDEX, JSFunction, reflect_apply)                            \
  V(REFLECT_CONSTRUCT_INDEX, JSFunction, reflect_construct)                    \
  V(PROMISE_THEN_INDEX, JSFunction, promise_then)                              \
  V(PROMISE_RESOLVE_INDEX, JSFunction, promise_resolve)                        \
  V(FUNCTION_PROTOTYPE_APPLY_INDEX, JSFunction, function_prototype_apply)      \
  /* TypedArray constructors - these must stay in order! */                    \
  V(UINT8_ARRAY_FUN_INDEX, JSFunction, uint8_array_fun)                        \
  V(INT8_ARRAY_FUN_INDEX, JSFunction, int8_array_fun)                          \
  V(UINT16_ARRAY_FUN_INDEX, JSFunction, uint16_array_fun)                      \
  V(INT16_ARRAY_FUN_INDEX, JSFunction, int16_array_fun)                        \
  V(UINT32_ARRAY_FUN_INDEX, JSFunction, uint32_array_fun)                      \
  V(INT32_ARRAY_FUN_INDEX, JSFunction, int32_array_fun)                        \
  V(FLOAT32_ARRAY_FUN_INDEX, JSFunction, float32_array_fun)                    \
  V(FLOAT64_ARRAY_FUN_INDEX, JSFunction, float64_array_fun)                    \
  V(UINT8_CLAMPED_ARRAY_FUN_INDEX, JSFunction, uint8_clamped_array_fun)        \
  V(BIGUINT64_ARRAY_FUN_INDEX, JSFunction, biguint64_array_fun)                \
  V(BIGINT64_ARRAY_FUN_INDEX, JSFunction, bigint64_array_fun)                  \
  V(FLOAT16_ARRAY_FUN_INDEX, JSFunction, float16_array_fun)                    \
  V(RAB_GSAB_UINT8_ARRAY_MAP_INDEX, Map, rab_gsab_uint8_array_map)             \
  V(RAB_GSAB_INT8_ARRAY_MAP_INDEX, Map, rab_gsab_int8_array_map)               \
  V(RAB_GSAB_UINT16_ARRAY_MAP_INDEX, Map, rab_gsab_uint16_array_map)           \
  V(RAB_GSAB_INT16_ARRAY_MAP_INDEX, Map, rab_gsab_int16_array_map)             \
  V(RAB_GSAB_UINT32_ARRAY_MAP_INDEX, Map, rab_gsab_uint32_array_map)           \
  V(RAB_GSAB_INT32_ARRAY_MAP_INDEX, Map, rab_gsab_int32_array_map)             \
  V(RAB_GSAB_FLOAT32_ARRAY_MAP_INDEX, Map, rab_gsab_float32_array_map)         \
  V(RAB_GSAB_FLOAT64_ARRAY_MAP_INDEX, Map, rab_gsab_float64_array_map)         \
  V(RAB_GSAB_UINT8_CLAMPED_ARRAY_MAP_INDEX, Map,                               \
    rab_gsab_uint8_clamped_array_map)                                          \
  V(RAB_GSAB_BIGUINT64_ARRAY_MAP_INDEX, Map, rab_gsab_biguint64_array_map)     \
  V(RAB_GSAB_BIGINT64_ARRAY_MAP_INDEX, Map, rab_gsab_bigint64_array_map)       \
  V(RAB_GSAB_FLOAT16_ARRAY_MAP_INDEX, Map, rab_gsab_float16_array_map)         \
  /* Below is alpha-sorted */                                                  \
  V(ABSTRACT_MODULE_SOURCE_FUNCTION_INDEX, JSFunction,                         \
    abstract_module_source_function)                                           \
  V(ABSTRACT_MODULE_SOURCE_PROTOTYPE_INDEX, JSObject,                          \
    abstract_module_source_prototype)                                          \
  V(ACCESSOR_PROPERTY_DESCRIPTOR_MAP_INDEX, Map,                               \
    accessor_property_descriptor_map)                                          \
  V(ALLOW_CODE_GEN_FROM_STRINGS_INDEX, Object, allow_code_gen_from_strings)    \
  V(ARRAY_BUFFER_FUN_INDEX, JSFunction, array_buffer_fun)                      \
  V(ARRAY_BUFFER_MAP_INDEX, Map, array_buffer_map)                             \
  V(ARRAY_BUFFER_NOINIT_FUN_INDEX, JSFunction, array_buffer_noinit_fun)        \
  V(ARRAY_FUNCTION_INDEX, JSFunction, array_function)                          \
  V(ARRAY_JOIN_STACK_INDEX, HeapObject, array_join_stack)                      \
  V(ARRAY_FROM_ASYNC_INDEX, JSFunction, from_async)                            \
  V(ASYNC_FROM_SYNC_ITERATOR_MAP_INDEX, Map, async_from_sync_iterator_map)     \
  V(ASYNC_FUNCTION_FUNCTION_INDEX, JSFunction, async_function_constructor)     \
  V(ASYNC_FUNCTION_OBJECT_MAP_INDEX, Map, async_function_object_map)           \
  V(ASYNC_GENERATOR_FUNCTION_FUNCTION_INDEX, JSFunction,                       \
    async_generator_function_function)                                         \
  V(BIGINT_FUNCTION_INDEX, JSFunction, bigint_function)                        \
  V(BOOLEAN_FUNCTION_INDEX, JSFunction, boolean_function)                      \
  V(BOUND_FUNCTION_WITH_CONSTRUCTOR_MAP_INDEX, Map,                            \
    bound_function_with_constructor_map)                                       \
  V(BOUND_FUNCTION_WITHOUT_CONSTRUCTOR_MAP_INDEX, Map,                         \
    bound_function_without_constructor_map)                                    \
  V(CALL_AS_CONSTRUCTOR_DELEGATE_INDEX, JSFunction,                            \
    call_as_constructor_delegate)                                              \
  V(CALL_AS_FUNCTION_DELEGATE_INDEX, JSFunction, call_as_function_delegate)    \
  V(CALLSITE_FUNCTION_INDEX, JSFunction, callsite_function)                    \
  V(CONTEXT_EXTENSION_FUNCTION_INDEX, JSFunction, context_extension_function)  \
  V(DATA_PROPERTY_DESCRIPTOR_MAP_INDEX, Map, data_property_descriptor_map)     \
  V(DATA_VIEW_FUN_INDEX, JSFunction, data_view_fun)                            \
  V(DATE_FUNCTION_INDEX, JSFunction, date_function)                            \
  V(DEBUG_CONTEXT_ID_INDEX, (UnionOf<Smi, Undefined>), debug_context_id)       \
  V(EMPTY_FUNCTION_INDEX, JSFunction, empty_function)                          \
  V(ERROR_MESSAGE_FOR_CODE_GEN_FROM_STRINGS_INDEX, Object,                     \
    error_message_for_code_gen_from_strings)                                   \
  V(ERROR_MESSAGE_FOR_WASM_CODE_GEN_INDEX, Object,                             \
    error_message_for_wasm_code_gen)                                           \
  V(ERRORS_THROWN_INDEX, Smi, errors_thrown)                                   \
  V(EXTRAS_BINDING_OBJECT_INDEX, JSObject, extras_binding_object)              \
  V(FAST_ALIASED_ARGUMENTS_MAP_INDEX, Map, fast_aliased_arguments_map)         \
  V(FAST_TEMPLATE_INSTANTIATIONS_CACHE_INDEX, FixedArray,                      \
    fast_template_instantiations_cache)                                        \
  V(FUNCTION_FUNCTION_INDEX, JSFunction, function_function)                    \
  V(FUNCTION_PROTOTYPE_INDEX, JSObject, function_prototype)                    \
  V(GENERATOR_FUNCTION_FUNCTION_INDEX, JSFunction,                             \
    generator_function_function)                                               \
  V(GENERATOR_OBJECT_PROTOTYPE_MAP_INDEX, Map, generator_object_prototype_map) \
  V(ASYNC_GENERATOR_OBJECT_PROTOTYPE_MAP_INDEX, Map,                           \
    async_generator_object_prototype_map)                                      \
  V(INITIAL_ARRAY_ITERATOR_MAP_INDEX, Map, initial_array_iterator_map)         \
  V(INITIAL_ARRAY_ITERATOR_PROTOTYPE_INDEX, JSObject,                          \
    initial_array_iterator_prototype)                                          \
  V(INITIAL_ARRAY_PROTOTYPE_INDEX, JSObject, initial_array_prototype)          \
  V(INITIAL_ERROR_PROTOTYPE_INDEX, JSObject, initial_error_prototype)          \
  V(INITIAL_GENERATOR_PROTOTYPE_INDEX, JSObject, initial_generator_prototype)  \
  V(INITIAL_ASYNC_ITERATOR_PROTOTYPE_INDEX, JSObject,                          \
    initial_async_iterator_prototype)                                          \
  V(INITIAL_ASYNC_GENERATOR_PROTOTYPE_INDEX, JSObject,                         \
    initial_async_generator_prototype)                                         \
  V(INITIAL_ITERATOR_PROTOTYPE_INDEX, JSObject, initial_iterator_prototype)    \
  V(INITIAL_DISPOSABLE_STACK_PROTOTYPE_INDEX, JSObject,                        \
    initial_disposable_stack_prototype)                                        \
  V(INITIAL_MAP_ITERATOR_PROTOTYPE_INDEX, JSObject,                            \
    initial_map_iterator_prototype)                                            \
  V(INITIAL_MAP_PROTOTYPE_MAP_INDEX, Map, initial_map_prototype_map)           \
  V(INITIAL_OBJECT_PROTOTYPE_INDEX, JSObject, initial_object_prototype)        \
  V(INITIAL_SET_ITERATOR_PROTOTYPE_INDEX, JSObject,                            \
    initial_set_iterator_prototype)                                            \
  V(INITIAL_SET_PROTOTYPE_INDEX, JSObject, initial_set_prototype)              \
  V(INITIAL_SET_PROTOTYPE_MAP_INDEX, Map, initial_set_prototype_map)           \
  V(INITIAL_STRING_ITERATOR_MAP_INDEX, Map, initial_string_iterator_map)       \
  V(INITIAL_STRING_ITERATOR_PROTOTYPE_INDEX, JSObject,                         \
    initial_string_iterator_prototype)                                         \
  V(INITIAL_STRING_PROTOTYPE_INDEX, JSObject, initial_string_prototype)        \
  V(INITIAL_WEAKMAP_PROTOTYPE_MAP_INDEX, Map, initial_weakmap_prototype_map)   \
  V(INITIAL_WEAKSET_PROTOTYPE_MAP_INDEX, Map, initial_weakset_prototype_map)   \
  V(INTL_COLLATOR_FUNCTION_INDEX, JSFunction, intl_collator_function)          \
  V(INTL_DATE_TIME_FORMAT_FUNCTION_INDEX, JSFunction,                          \
    intl_date_time_format_function)                                            \
  V(INTL_DISPLAY_NAMES_FUNCTION_INDEX, JSFunction,                             \
    intl_display_names_function)                                               \
  V(INTL_DURATION_FORMAT_FUNCTION_INDEX, JSFunction,                           \
    intl_duration_format_function)                                             \
  V(INTL_NUMBER_FORMAT_FUNCTION_INDEX, JSFunction,                             \
    intl_number_format_function)                                               \
  V(INTL_LOCALE_FUNCTION_INDEX, JSFunction, intl_locale_function)              \
  V(INTL_LIST_FORMAT_FUNCTION_INDEX, JSFunction, intl_list_format_function)    \
  V(INTL_PLURAL_RULES_FUNCTION_INDEX, JSFunction, intl_plural_rules_function)  \
  V(INTL_RELATIVE_TIME_FORMAT_FUNCTION_INDEX, JSFunction,                      \
    intl_relative_time_format_function)                                        \
  V(INTL_SEGMENTER_FUNCTION_INDEX, JSFunction, intl_segmenter_function)        \
  V(INTL_SEGMENTS_MAP_INDEX, Map, intl_segments_map)                           \
  V(INTL_SEGMENT_DATA_OBJECT_MAP_INDEX, Map, intl_segment_data_object_map)     \
  V(INTL_SEGMENT_DATA_OBJECT_WORDLIKE_MAP_INDEX, Map,                          \
    intl_segment_data_object_wordlike_map)                                     \
  V(INTL_SEGMENT_ITERATOR_MAP_INDEX, Map, intl_segment_iterator_map)           \
  V(ITERATOR_FILTER_HELPER_MAP_INDEX, Map, iterator_filter_helper_map)         \
  V(ITERATOR_MAP_HELPER_MAP_INDEX, Map, iterator_map_helper_map)               \
  V(ITERATOR_TAKE_HELPER_MAP_INDEX, Map, iterator_take_helper_map)             \
  V(ITERATOR_DROP_HELPER_MAP_INDEX, Map, iterator_drop_helper_map)             \
  V(ITERATOR_FLAT_MAP_HELPER_MAP_INDEX, Map, iterator_flatMap_helper_map)      \
  V(ITERATOR_FUNCTION_INDEX, JSFunction, iterator_function)                    \
  V(VALID_ITERATOR_WRAPPER_MAP_INDEX, Map, valid_iterator_wrapper_map)         \
  V(ITERATOR_RESULT_MAP_INDEX, Map, iterator_result_map)                       \
  V(JS_ARRAY_PACKED_SMI_ELEMENTS_MAP_INDEX, Map,                               \
    js_array_packed_smi_elements_map)                                          \
  V(JS_ARRAY_HOLEY_SMI_ELEMENTS_MAP_INDEX, Map,                                \
    js_array_holey_smi_elements_map)                                           \
  V(JS_ARRAY_PACKED_ELEMENTS_MAP_INDEX, Map, js_array_packed_elements_map)     \
  V(JS_ARRAY_HOLEY_ELEMENTS_MAP_INDEX, Map, js_array_holey_elements_map)       \
  V(JS_ARRAY_PACKED_DOUBLE_ELEMENTS_MAP_INDEX, Map,                            \
    js_array_packed_double_elements_map)                                       \
  V(JS_ARRAY_HOLEY_DOUBLE_ELEMENTS_MAP_INDEX, Map,                             \
    js_array_holey_double_elements_map)                                        \
  V(JS_ARRAY_TEMPLATE_LITERAL_OBJECT_MAP, Map,                                 \
    js_array_template_literal_object_map)                                      \
  V(JS_DISPOSABLE_STACK_FUNCTION_INDEX, JSFunction,                            \
    js_disposable_stack_function)                                              \
  V(JS_DISPOSABLE_STACK_MAP_INDEX, Map, js_disposable_stack_map)               \
  V(JS_MAP_FUN_INDEX, JSFunction, js_map_fun)                                  \
  V(JS_MAP_MAP_INDEX, Map, js_map_map)                                         \
  V(JS_MODULE_NAMESPACE_MAP, Map, js_module_namespace_map)                     \
  V(JS_RAW_JSON_MAP, Map, js_raw_json_map)                                     \
  V(JS_SET_FUN_INDEX, JSFunction, js_set_fun)                                  \
  V(JS_SET_MAP_INDEX, Map, js_set_map)                                         \
  V(JS_WEAK_MAP_FUN_INDEX, JSFunction, js_weak_map_fun)                        \
  V(JS_WEAK_SET_FUN_INDEX, JSFunction, js_weak_set_fun)                        \
  V(JS_WEAK_REF_FUNCTION_INDEX, JSFunction, js_weak_ref_fun)                   \
  V(JS_FINALIZATION_REGISTRY_FUNCTION_INDEX, JSFunction,                       \
    js_finalization_registry_fun)                                              \
  V(JS_TEMPORAL_CALENDAR_FUNCTION_INDEX, JSFunction,                           \
    temporal_calendar_function)                                                \
  V(JS_TEMPORAL_DURATION_FUNCTION_INDEX, JSFunction,                           \
    temporal_duration_function)                                                \
  V(JS_TEMPORAL_INSTANT_FUNCTION_INDEX, JSFunction, temporal_instant_function) \
  V(JS_TEMPORAL_PLAIN_DATE_FUNCTION_INDEX, JSFunction,                         \
    temporal_plain_date_function)                                              \
  V(JS_TEMPORAL_PLAIN_DATE_TIME_FUNCTION_INDEX, JSFunction,                    \
    temporal_plain_date_time_function)                                         \
  V(JS_TEMPORAL_PLAIN_MONTH_DAY_FUNCTION_INDEX, JSFunction,                    \
    temporal_plain_month_day_function)                                         \
  V(JS_TEMPORAL_PLAIN_TIME_FUNCTION_INDEX, JSFunction,                         \
    temporal_plain_time_function)                                              \
  V(JS_TEMPORAL_PLAIN_YEAR_MONTH_FUNCTION_INDEX, JSFunction,                   \
    temporal_plain_year_month_function)                                        \
  V(JS_TEMPORAL_TIME_ZONE_FUNCTION_INDEX, JSFunction,                          \
    temporal_time_zone_function)                                               \
  V(JS_TEMPORAL_ZONED_DATE_TIME_FUNCTION_INDEX, JSFunction,                    \
    temporal_zoned_date_time_function)                                         \
  V(JSON_OBJECT, JSObject, json_object)                                        \
  V(PROMISE_WITHRESOLVERS_RESULT_MAP_INDEX, Map,                               \
    promise_withresolvers_result_map)                                          \
  V(TEMPORAL_OBJECT_INDEX, HeapObject, temporal_object)                        \
  V(TEMPORAL_INSTANT_FIXED_ARRAY_FROM_ITERABLE_FUNCTION_INDEX, JSFunction,     \
    temporal_instant_fixed_array_from_iterable)                                \
  V(STRING_FIXED_ARRAY_FROM_ITERABLE_FUNCTION_INDEX, JSFunction,               \
    string_fixed_array_from_iterable)                                          \
  /* Context maps */                                                           \
  V(META_MAP_INDEX, Map, meta_map)                                             \
  V(FUNCTION_CONTEXT_MAP_INDEX, Map, function_context_map)                     \
  V(MODULE_CONTEXT_MAP_INDEX, Map, module_context_map)                         \
  V(EVAL_CONTEXT_MAP_INDEX, Map, eval_context_map)                             \
  V(SCRIPT_CONTEXT_MAP_INDEX, Map, script_context_map)                         \
  V(AWAIT_CONTEXT_MAP_INDEX, Map, await_context_map)                           \
  V(BLOCK_CONTEXT_MAP_INDEX, Map, block_context_map)                           \
  V(CATCH_CONTEXT_MAP_INDEX, Map, catch_context_map)                           \
  V(WITH_CONTEXT_MAP_INDEX, Map, with_context_map)                             \
  V(DEBUG_EVALUATE_CONTEXT_MAP_INDEX, Map, debug_evaluate_context_map)         \
  V(JS_RAB_GSAB_DATA_VIEW_MAP_INDEX, Map, js_rab_gsab_data_view_map)           \
  V(MAP_CACHE_INDEX, Object, map_cache)                                        \
  V(MAP_KEY_ITERATOR_MAP_INDEX, Map, map_key_iterator_map)                     \
  V(MAP_KEY_VALUE_ITERATOR_MAP_INDEX, Map, map_key_value_iterator_map)         \
  V(MAP_VALUE_ITERATOR_MAP_INDEX, Map, map_value_iterator_map)                 \
  V(MATH_RANDOM_INDEX_INDEX, Smi, math_random_index)                           \
  V(MATH_RANDOM_STATE_INDEX, ByteArray, math_random_state)                     \
  V(MATH_RANDOM_CACHE_INDEX, FixedDoubleArray, math_random_cache)              \
  V(NORMALIZED_MAP_CACHE_INDEX, Object, normalized_map_cache)                  \
  V(NUMBER_FUNCTION_INDEX, JSFunction, number_function)                        \
  V(OBJECT_FUNCTION_INDEX, JSFunction, object_function)                        \
  V(OBJECT_FUNCTION_PROTOTYPE_INDEX, JSObject, object_function_prototype)      \
  V(OBJECT_FUNCTION_PROTOTYPE_MAP_INDEX, Map, object_function_prototype_map)   \
  V(PROMISE_HOOK_INIT_FUNCTION_INDEX, Object, promise_hook_init_function)      \
  V(PROMISE_HOOK_BEFORE_FUNCTION_INDEX, Object, promise_hook_before_function)  \
  V(PROMISE_HOOK_AFTER_FUNCTION_INDEX, Object, promise_hook_after_function)    \
  V(PROMISE_HOOK_RESOLVE_FUNCTION_INDEX, Object,                               \
    promise_hook_resolve_function)                                             \
  V(PROXY_CALLABLE_MAP_INDEX, Map, proxy_callable_map)                         \
  V(PROXY_CONSTRUCTOR_MAP_INDEX, Map, proxy_constructor_map)                   \
  V(PROXY_FUNCTION_INDEX, JSFunction, proxy_function)                          \
  V(PROXY_MAP_INDEX, Map, proxy_map)                                           \
  V(PROXY_REVOCABLE_RESULT_MAP_INDEX, Map, proxy_revocable_result_map)         \
  V(PROMISE_PROTOTYPE_INDEX, JSObject, promise_prototype)                      \
  V(RECORDER_CONTEXT_ID, Object, recorder_context_id)                          \
  V(REGEXP_EXEC_FUNCTION_INDEX, JSFunction, regexp_exec_function)              \
  V(REGEXP_FUNCTION_INDEX, JSFunction, regexp_function)                        \
  V(REGEXP_LAST_MATCH_INFO_INDEX, RegExpMatchInfo, regexp_last_match_info)     \
  V(REGEXP_MATCH_ALL_FUNCTION_INDEX, JSFunction, regexp_match_all_function)    \
  V(REGEXP_MATCH_FUNCTION_INDEX, JSFunction, regexp_match_function)            \
  V(REGEXP_PROTOTYPE_INDEX, JSObject, regexp_prototype)                        \
  V(REGEXP_PROTOTYPE_MAP_INDEX, Map, regexp_prototype_map)                     \
  V(REGEXP_REPLACE_FUNCTION_INDEX, JSFunction, regexp_replace_function)        \
  V(REGEXP_RESULT_MAP_INDEX, Map, regexp_result_map)                           \
  V(REGEXP_RESULT_WITH_INDICES_MAP_INDEX, Map, regexp_result_with_indices_map) \
  V(REGEXP_RESULT_INDICES_MAP_INDEX, Map, regexp_result_indices_map)           \
  V(REGEXP_SEARCH_FUNCTION_INDEX, JSFunction, regexp_search_function)          \
  V(REGEXP_SPLIT_FUNCTION_INDEX, JSFunction, regexp_split_function)            \
  V(INITIAL_REGEXP_STRING_ITERATOR_PROTOTYPE_MAP_INDEX, Map,                   \
    initial_regexp_string_iterator_prototype_map)                              \
  V(SCRIPT_CONTEXT_TABLE_INDEX, ScriptContextTable, script_context_table)      \
  V(SCRIPT_EXECUTION_CALLBACK_INDEX, Object, script_execution_callback)        \
  V(SECURITY_TOKEN_INDEX, Object, security_token)                              \
  V(SERIALIZED_OBJECTS, HeapObject, serialized_objects)                        \
  V(SET_VALUE_ITERATOR_MAP_INDEX, Map, set_value_iterator_map)                 \
  V(SET_KEY_VALUE_ITERATOR_MAP_INDEX, Map, set_key_value_iterator_map)         \
  V(SHARED_ARRAY_BUFFER_FUN_INDEX, JSFunction, shared_array_buffer_fun)        \
  V(SLOPPY_ARGUMENTS_MAP_INDEX, Map, sloppy_arguments_map)                     \
  V(SLOW_ALIASED_ARGUMENTS_MAP_INDEX, Map, slow_aliased_arguments_map)         \
  V(STRICT_ARGUMENTS_MAP_INDEX, Map, strict_arguments_map)                     \
  V(SLOW_OBJECT_WITH_NULL_PROTOTYPE_MAP, Map,                                  \
    slow_object_with_null_prototype_map)                                       \
  V(SLOW_OBJECT_WITH_OBJECT_PROTOTYPE_MAP, Map,                                \
    slow_object_with_object_prototype_map)                                     \
  V(SLOW_TEMPLATE_INSTANTIATIONS_CACHE_INDEX, SimpleNumberDictionary,          \
    slow_template_instantiations_cache)                                        \
  V(ATOMICS_WAITASYNC_PROMISES, OrderedHashSet, atomics_waitasync_promises)    \
  V(WASM_DEBUG_MAPS, FixedArray, wasm_debug_maps)                              \
  /* Fast Path Protectors */                                                   \
  V(REGEXP_SPECIES_PROTECTOR_INDEX, PropertyCell, regexp_species_protector)    \
  /* All *_FUNCTION_MAP_INDEX definitions used by Context::FunctionMapIndex */ \
  /* must remain together. */                                                  \
  V(SLOPPY_FUNCTION_MAP_INDEX, Map, sloppy_function_map)                       \
  V(SLOPPY_FUNCTION_WITH_NAME_MAP_INDEX, Map, sloppy_function_with_name_map)   \
  V(SLOPPY_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX, Map,                          \
    sloppy_function_without_prototype_map)                                     \
  V(SLOPPY_FUNCTION_WITH_READONLY_PROTOTYPE_MAP_INDEX, Map,                    \
    sloppy_function_with_readonly_prototype_map)                               \
  V(STRICT_FUNCTION_MAP_INDEX, Map, strict_function_map)                       \
  V(STRICT_FUNCTION_WITH_NAME_MAP_INDEX, Map, strict_function_with_name_map)   \
  V(STRICT_FUNCTION_WITH_READONLY_PROTOTYPE_MAP_INDEX, Map,                    \
    strict_function_with_readonly_prototype_map)                               \
  V(STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX, Map,                          \
    strict_function_without_prototype_map)                                     \
  V(METHOD_WITH_NAME_MAP_INDEX, Map, method_with_name_map)                     \
  V(ASYNC_FUNCTION_MAP_INDEX, Map, async_function_map)                         \
  V(ASYNC_FUNCTION_WITH_NAME_MAP_INDEX, Map, async_function_with_name_map)     \
  V(GENERATOR_FUNCTION_MAP_INDEX, Map, generator_function_map)                 \
  V(GENERATOR_FUNCTION_WITH_NAME_MAP_INDEX, Map,                               \
    generator_function_with_name_map)                                          \
  V(ASYNC_GENERATOR_FUNCTION_MAP_INDEX, Map, async_generator_function_map)     \
  V(ASYNC_GENERATOR_FUNCTION_WITH_NAME_MAP_INDEX, Map,                         \
    async_generator_function_with_name_map)                                    \
  V(CLASS_FUNCTION_MAP_INDEX, Map, class_function_map)                         \
  V(STRING_FUNCTION_INDEX, JSFunction, string_function)                        \
  V(STRING_FUNCTION_PROTOTYPE_MAP_INDEX, Map, string_function_prototype_map)   \
  V(SYMBOL_FUNCTION_INDEX, JSFunction, symbol_function)                        \
  V(IS_WASM_JS_INSTALLED_INDEX, Smi, is_wasm_js_installed)                     \
  V(IS_WASM_JSPI_INSTALLED_INDEX, Smi, is_wasm_jspi_installed)                 \
  V(WASM_WEBASSEMBLY_OBJECT_INDEX, JSObject, wasm_webassembly_object)          \
  V(WASM_EXPORTED_FUNCTION_MAP_INDEX, Map, wasm_exported_function_map)         \
  V(WASM_TAG_CONSTRUCTOR_INDEX, JSFunction, wasm_tag_constructor)              \
  V(WASM_EXCEPTION_CONSTRUCTOR_INDEX, JSFunction, wasm_exception_constructor)  \
  V(WASM_GLOBAL_CONSTRUCTOR_INDEX, JSFunction, wasm_global_constructor)        \
  V(WASM_INSTANCE_CONSTRUCTOR_INDEX, JSFunction, wasm_instance_constructor)    \
  V(WASM_JS_TAG_INDEX, JSObject, wasm_js_tag)                                  \
  V(WASM_MEMORY_CONSTRUCTOR_INDEX, JSFunction, wasm_memory_constructor)        \
  V(WASM_MODULE_CONSTRUCTOR_INDEX, JSFunction, wasm_module_constructor)        \
  V(WASM_TABLE_CONSTRUCTOR_INDEX, JSFunction, wasm_table_constructor)          \
  V(WASM_SUSPENDING_CONSTRUCTOR_INDEX, JSFunction,                             \
    wasm_suspending_constructor)                                               \
  V(WASM_SUSPENDER_CONSTRUCTOR_INDEX, JSFunction, wasm_suspender_constructor)  \
  V(WASM_SUSPENDING_MAP, Map, wasm_suspending_map)                             \
  V(WASM_SUSPENDING_PROTOTYPE, JSObject, wasm_suspending_prototype)            \
  V(TEMPLATE_WEAKMAP_INDEX, HeapObject, template_weakmap)                      \
  V(TYPED_ARRAY_FUN_INDEX, JSFunction, typed_array_function)                   \
  V(TYPED_ARRAY_PROTOTYPE_INDEX, JSObject, typed_array_prototype)              \
  V(ARRAY_ENTRIES_ITERATOR_INDEX, JSFunction, array_entries_iterator)          \
  V(ARRAY_FOR_EACH_ITERATOR_INDEX, JSFunction, array_for_each_iterator)        \
  V(ARRAY_KEYS_ITERATOR_INDEX, JSFunction, array_keys_iterator)                \
  V(ARRAY_VALUES_ITERATOR_INDEX, JSFunction, array_values_iterator)            \
  V(ERROR_FUNCTION_INDEX, JSFunction, error_function)                          \
  V(ERROR_TO_STRING, JSFunction, error_to_string)                              \
  V(EVAL_ERROR_FUNCTION_INDEX, JSFunction, eval_error_function)                \
  V(AGGREGATE_ERROR_FUNCTION_INDEX, JSFunction, aggregate_error_function)      \
  V(GLOBAL_EVAL_FUN_INDEX, JSFunction, global_eval_fun)                        \
  V(GLOBAL_PARSE_FLOAT_FUN_INDEX, JSFunction, global_parse_float_fun)          \
  V(GLOBAL_PARSE_INT_FUN_INDEX, JSFunction, global_parse_int_fun)              \
  V(GLOBAL_PROXY_FUNCTION_INDEX, JSFunction, global_proxy_function)            \
  V(MAP_DELETE_INDEX, JSFunction, map_delete)                                  \
  V(MAP_GET_INDEX, JSFunction, map_get)                                        \
  V(MAP_HAS_INDEX, JSFunction, map_has)                                        \
  V(MAP_SET_INDEX, JSFunction, map_set)                                        \
  V(FINALIZATION_REGISTRY_CLEANUP_SOME, JSFunction,                            \
    finalization_registry_cleanup_some)                                        \
  V(FUNCTION_HAS_INSTANCE_INDEX, JSFunction, function_has_instance)            \
  V(FUNCTION_TO_STRING_INDEX, JSFunction, function_to_string)                  \
  V(OBJECT_TO_STRING, JSFunction, object_to_string)                            \
  V(OBJECT_VALUE_OF_FUNCTION_INDEX, JSFunction, object_value_of_function)      \
  V(PROMISE_ALL_INDEX, JSFunction, promise_all)                                \
  V(PROMISE_ALL_SETTLED_INDEX, JSFunction, promise_all_settled)                \
  V(PROMISE_ANY_INDEX, JSFunction, promise_any)                                \
  V(PROMISE_FUNCTION_INDEX, JSFunction, promise_function)                      \
  V(RANGE_ERROR_FUNCTION_INDEX, JSFunction, range_error_function)              \
  V(REFERENCE_ERROR_FUNCTION_INDEX, JSFunction, reference_error_function)      \
  V(SET_ADD_INDEX, JSFunction, set_add)                                        \
  V(SET_DELETE_INDEX, JSFunction, set_delete)                                  \
  V(SET_HAS_INDEX, JSFunction, set_has)                                        \
  V(SHADOW_REALM_IMPORT_VALUE_REJECTED_INDEX, JSFunction,                      \
    shadow_realm_import_value_rejected)                                        \
  V(SUPPRESSED_ERROR_FUNCTION_INDEX, JSFunction, suppressed_error_function)    \
  V(SYNTAX_ERROR_FUNCTION_INDEX, JSFunction, syntax_error_function)            \
  V(TYPE_ERROR_FUNCTION_INDEX, JSFunction, type_error_function)                \
  V(URI_ERROR_FUNCTION_INDEX, JSFunction, uri_error_function)                  \
  V(WASM_COMPILE_ERROR_FUNCTION_INDEX, JSFunction,                             \
    wasm_compile_error_function)                                               \
  V(WASM_LINK_ERROR_FUNCTION_INDEX, JSFunction, wasm_link_error_function)      \
  V(WASM_RUNTIME_ERROR_FUNCTION_INDEX, JSFunction,                             \
    wasm_runtime_error_function)                                               \
  V(WEAKMAP_SET_INDEX, JSFunction, weakmap_set)                                \
  V(WEAKMAP_GET_INDEX, JSFunction, weakmap_get)                                \
  V(WEAKMAP_DELETE_INDEX, JSFunction, weakmap_delete)                          \
  V(WEAKSET_ADD_INDEX, JSFunction, weakset_add)                                \
  V(WRAPPED_FUNCTION_MAP_INDEX, Map, wrapped_function_map)                     \
  V(RETAINED_MAPS, Object, retained_maps)                                      \
  V(SHARED_SPACE_JS_OBJECT_HAS_INSTANCE_INDEX, JSFunction,                     \
    shared_space_js_object_has_instance)

#include "torque-generated/src/objects/contexts-tq.inc"

// JSFunctions are pairs (context, function code), sometimes also called
// closures. A Context object is used to represent function contexts and
// dynamically pushed 'with' contexts (or 'scopes' in ECMA-262 speak).
//
// At runtime, the contexts build a stack in parallel to the execution
// stack, with the top-most context being the current context. All contexts
// have the following slots:
//
// [ scope_info     ]  This is the scope info describing the current context. It
//                     contains the names of statically allocated context slots,
//                     and stack-allocated locals.  The names are needed for
//                     dynamic lookups in the presence of 'with' or 'eval', and
//                     for the debugger.
//
// [ previous       ]  A pointer to the previous context.
//
// [ extension      ]  Additional data. This slot is only available when
//                     ScopeInfo::HasContextExtensionSlot returns true.
//
//                     For native contexts, it contains the global object.
//                     For module contexts, it contains the module object.
//                     For await contexts, it contains the generator object.
//                     For var block contexts, it may contain an "extension
//                     object".
//                     For with contexts, it contains an "extension object".
//
//                     An "extension object" is used to dynamically extend a
//                     context with additional variables, namely in the
//                     implementation of the 'with' construct and the 'eval'
//                     construct.  For instance, Context::Lookup also searches
//                     the extension object for properties.  (Storing the
//                     extension object is the original purpose of this context
//                     slot, hence the name.)
//
// In addition, function contexts with sloppy eval may have statically
// allocated context slots to store local variables/functions that are accessed
// from inner functions (via static context addresses) or through 'eval'
// (dynamic context lookups).
// The native context contains additional slots for fast access to native
// properties.
//
// Finally, with Harmony scoping, the JSFunction representing a top level
// script will have the ScriptContext rather than a FunctionContext.
// Script contexts from all top-level scripts are gathered in
// ScriptContextTable.

class Context : public TorqueGeneratedContext<Context, HeapObject> {
 public:
  NEVER_READ_ONLY_SPACE

  using TorqueGeneratedContext::length;      // Non-atomic.
  using TorqueGeneratedContext::set_length;  // Non-atomic.
  DECL_RELAXED_INT_ACCESSORS(length)

  // Setter and getter for elements.
  // Note the plain accessors use relaxed semantics.
  // TODO(jgruber): Make that explicit through tags.
  V8_INLINE Tagged<Object> get(int index) const;
  V8_INLINE Tagged<Object> get(PtrComprCageBase cage_base, int index) const;
  V8_INLINE void set(int index, Tagged<Object> value,
                     WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  // Accessors with acquire-release semantics.
  V8_INLINE Tagged<Object> get(int index, AcquireLoadTag) const;
  V8_INLINE Tagged<Object> get(PtrComprCageBase cage_base, int index,
                               AcquireLoadTag) const;
  V8_INLINE void set(int index, Tagged<Object> value, WriteBarrierMode mode,
                     ReleaseStoreTag);

  static const int kScopeInfoOffset = kElementsOffset;
  static const int kPreviousOffset = kScopeInfoOffset + kTaggedSize;

  /* Header size. */                                                  \
  /* TODO(ishell): use this as header size once MIN_CONTEXT_SLOTS */  \
  /* is removed in favour of offset-based access to common fields. */ \
  static const int kTodoHeaderSize = kPreviousOffset + kTaggedSize;

  // If the extension slot exists, it is the first slot after the header.
  static const int kExtensionOffset = kTodoHeaderSize;

  // Garbage collection support.
  V8_INLINE static constexpr int SizeFor(int length) {
    // TODO(v8:9287): This is a workaround for GCMole build failures.
    int result = kElementsOffset + length * kTaggedSize;
    DCHECK_EQ(TorqueGeneratedContext::SizeFor(length), result);
    return result;
  }

  // Code Generation support.
  // Offset of the element from the beginning of object.
  V8_INLINE static constexpr int OffsetOfElementAt(int index) {
    return SizeFor(index);
  }
  // Offset of the element from the heap object pointer.
  V8_INLINE static constexpr int SlotOffset(int index) {
    return OffsetOfElementAt(index) - kHeapObjectTag;
  }

  // Initializes the variable slots of the context. Lexical variables that need
  // initialization are filled with the hole.
  void Initialize(Isolate* isolate);

  // TODO(ishell): eventually migrate to the offset based access instead of
  // index-based.
  // The default context slot layout; indices are FixedArray slot indices.
  enum Field {
    // TODO(shell): use offset-based approach for accessing common values.
    // These slots are in all contexts.
    SCOPE_INFO_INDEX,
    PREVIOUS_INDEX,

    // This slot only exists if ScopeInfo::HasContextExtensionSlot returns true.
    EXTENSION_INDEX,

// These slots are only in native contexts.
#define NATIVE_CONTEXT_SLOT(index, type, name) index,
    NATIVE_CONTEXT_FIELDS(NATIVE_CONTEXT_SLOT)
#undef NATIVE_CONTEXT_SLOT

    // Properties from here are treated as weak references by the full GC.
    // Scavenge treats them as strong references.
    NEXT_CONTEXT_LINK,  // Weak.

    // Total number of slots.
    NATIVE_CONTEXT_SLOTS,
    FIRST_WEAK_SLOT = NEXT_CONTEXT_LINK,
    FIRST_JS_ARRAY_MAP_SLOT = JS_ARRAY_PACKED_SMI_ELEMENTS_MAP_INDEX,

    // TODO(shell): Remove, once it becomes zero
    MIN_CONTEXT_SLOTS = EXTENSION_INDEX,
    MIN_CONTEXT_EXTENDED_SLOTS = EXTENSION_INDEX + 1,

    // This slot holds the thrown value in catch contexts.
    THROWN_OBJECT_INDEX = MIN_CONTEXT_SLOTS,

    // These slots hold values in debug evaluate contexts.
    WRAPPED_CONTEXT_INDEX = MIN_CONTEXT_EXTENDED_SLOTS,

    // This slot holds the const tracking let side data.
    CONST_TRACKING_LET_SIDE_DATA_INDEX = MIN_CONTEXT_SLOTS,
  };

  static const int kExtensionSize =
      (MIN_CONTEXT_EXTENDED_SLOTS - MIN_CONTEXT_SLOTS) * kTaggedSize;
  static const int kExtendedHeaderSize = kTodoHeaderSize + kExtensionSize;

  // A region of native context entries containing maps for functions created
  // by Builtin::kFastNewClosure.
  static const int FIRST_FUNCTION_MAP_INDEX = SLOPPY_FUNCTION_MAP_INDEX;
  static const int LAST_FUNCTION_MAP_INDEX = CLASS_FUNCTION_MAP_INDEX;

  static const int FIRST_FIXED_TYPED_ARRAY_FUN_INDEX = UINT8_ARRAY_FUN_INDEX;
  static const int FIRST_RAB_GSAB_TYPED_ARRAY_MAP_INDEX =
      RAB_GSAB_UINT8_ARRAY_MAP_INDEX;

  static const int kNoContext = 0;
  static const int kInvalidContext = 1;

  // Direct slot access.
  DECL_ACCESSORS(scope_info, Tagged<ScopeInfo>)

  inline Tagged<Object> unchecked_previous() const;
  inline Tagged<Context> previous() const;

  inline Tagged<Object> next_context_link() const;

  inline bool has_extension() const;
  inline Tagged<HeapObject> extension() const;
  V8_EXPORT_PRIVATE void set_extension(
      Tagged<HeapObject> object, WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  Tagged<JSObject> extension_object() const;
  Tagged<JSReceiver> extension_receiver() const;

  // Find the module context (assuming there is one) and return the associated
  // module object.
  Tagged<SourceTextModule> module() const;

  // Get the context where var declarations will be hoisted to, which
  // may be the context itself.
  Tagged<Context> declaration_context() const;
  bool is_declaration_context() const;

  // Get the next closure's context on the context chain.
  Tagged<Context> closure_context() const;

  // Returns a JSGlobalProxy object or null.
  V8_EXPORT_PRIVATE Tagged<JSGlobalProxy> global_proxy() const;

  // Get the JSGlobalObject object.
  V8_EXPORT_PRIVATE Tagged<JSGlobalObject> global_object() const;

  // Get the script context by traversing the context chain.
  Tagged<Context> script_context() const;

  // Compute the native context.
  inline Tagged<NativeContext> native_context() const;
  inline bool IsDetached() const;

  // Predicates for context types.  IsNativeContext is already defined on
  // Object.
  inline bool IsFunctionContext() const;
  inline bool IsCatchContext() const;
  inline bool IsWithContext() const;
  inline bool IsDebugEvaluateContext() const;
  inline bool IsAwaitContext() const;
  inline bool IsBlockContext() const;
  inline bool IsModuleContext() const;
  inline bool IsEvalContext() const;
  inline bool IsScriptContext() const;

  inline bool HasSameSecurityTokenAs(Tagged<Context> that) const;

  Handle<Object> ErrorMessageForCodeGenerationFromStrings();
  Handle<Object> ErrorMessageForWasmCodeGeneration();

#define NATIVE_CONTEXT_FIELD_ACCESSORS(index, type, name)   \
  inline void set_##name(Tagged<UNPAREN(type)> value);      \
  inline bool is_##name(Tagged<UNPAREN(type)> value) const; \
  inline Tagged<UNPAREN(type)> name() const;                \
  inline Tagged<UNPAREN(type)> name(AcquireLoadTag) const;
  NATIVE_CONTEXT_FIELDS(NATIVE_CONTEXT_FIELD_ACCESSORS)
#undef NATIVE_CONTEXT_FIELD_ACCESSORS

  // Lookup the slot called name, starting with the current context.
  // There are three possibilities:
  //
  // 1) result->IsContext():
  //    The binding was found in a context.  *index is always the
  //    non-negative slot index.  *attributes is NONE for var and let
  //    declarations, READ_ONLY for const declarations (never ABSENT).
  //
  // 2) result->IsJSObject():
  //    The binding was found as a named property in a context extension
  //    object (i.e., was introduced via eval), as a property on the subject
  //    of with, or as a property of the global object.  *index is -1 and
  //    *attributes is not ABSENT.
  //
  // 3) result->IsModule():
  //    The binding was found in module imports or exports.
  //     *attributes is never ABSENT. imports are READ_ONLY.
  //
  // 4) result.is_null():
  //    There was no binding found, *index is always -1 and *attributes is
  //    always ABSENT.
  static Handle<Object> Lookup(Handle<Context> context, Handle<String> name,
                               ContextLookupFlags flags, int* index,
                               PropertyAttributes* attributes,
                               InitializationFlag* init_flag,
                               VariableMode* variable_mode,
                               bool* is_sloppy_function_name = nullptr);

  static inline int FunctionMapIndex(LanguageMode language_mode,
                                     FunctionKind kind, bool has_shared_name);

  static int ArrayMapIndex(ElementsKind elements_kind) {
    DCHECK(IsFastElementsKind(elements_kind));
    return int{elements_kind} + FIRST_JS_ARRAY_MAP_SLOT;
  }

  inline Tagged<Map> GetInitialJSArrayMap(ElementsKind kind) const;

  static Tagged<ConstTrackingLetCell> GetOrCreateConstTrackingLetCell(
      DirectHandle<Context> context, size_t index, Isolate* isolate);

  bool ConstTrackingLetSideDataIsConst(size_t index) const;

  static void UpdateConstTrackingLetSideData(
      DirectHandle<Context> script_context, int index,
      DirectHandle<Object> new_value, Isolate* isolate);

  static const int kNotFound = -1;

  // Dispatched behavior.
  DECL_PRINTER(Context)
  DECL_VERIFIER(Context)

  class BodyDescriptor;

#ifdef VERIFY_HEAP
  V8_EXPORT_PRIVATE void VerifyExtensionSlot(Tagged<HeapObject> extension);
#endif

 private:
#ifdef DEBUG
  // Bootstrapping-aware type checks.
  static bool IsBootstrappingOrValidParentContext(Tagged<Object> object,
                                                  Tagged<Context> kid);
#endif

  friend class Factory;
  inline void set_previous(Tagged<Context> context,
                           WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  TQ_OBJECT_CONSTRUCTORS(Context)
};

class NativeContext : public Context {
 public:
  // TODO(neis): Move some stuff from Context here.

  // NativeContext fields are read concurrently from background threads; any
  // concurrent writes of affected fields must have acquire-release semantics,
  // thus we hide the non-atomic setter. Note this doesn't protect fully since
  // one could still use Context::set and/or write directly using offsets (e.g.
  // from CSA/Torque).
  void set(int index, Tagged<Object> value, WriteBarrierMode mode) = delete;
  V8_INLINE void set(int index, Tagged<Object> value, WriteBarrierMode mode,
                     ReleaseStoreTag);

  // [microtask_queue]: pointer to the MicrotaskQueue object.
  DECL_EXTERNAL_POINTER_ACCESSORS(microtask_queue, MicrotaskQueue*)

  inline void synchronized_set_script_context_table(
      Tagged<ScriptContextTable> script_context_table);
  inline Tagged<ScriptContextTable> synchronized_script_context_table() const;

  // Caution, hack: this getter ignores the AcquireLoadTag. The global_object
  // slot is safe to read concurrently since it is immutable after
  // initialization.  This function should *not* be used from anywhere other
  // than heap-refs.cc.
  // TODO(jgruber): Remove this function after NativeContextRef is actually
  // never serialized and BROKER_NATIVE_CONTEXT_FIELDS is removed.
  Tagged<JSGlobalObject> global_object() { return Context::global_object(); }
  Tagged<JSGlobalObject> global_object(AcquireLoadTag) {
    return Context::global_object();
  }

  inline Tagged<Map> TypedArrayElementsKindToCtorMap(
      ElementsKind element_kind) const;

  inline Tagged<Map> TypedArrayElementsKindToRabGsabCtorMap(
      ElementsKind element_kind) const;

  bool HasTemplateLiteralObject(Tagged<JSArray> array);

  // Dispatched behavior.
  DECL_PRINTER(NativeContext)
  DECL_VERIFIER(NativeContext)

  // Layout description.
#define NATIVE_CONTEXT_FIELDS_DEF(V)                                        \
  /* TODO(ishell): move definition of common context offsets to Context. */ \
  V(kStartOfNativeContextFieldsOffset,                                      \
    (FIRST_WEAK_SLOT - MIN_CONTEXT_EXTENDED_SLOTS) * kTaggedSize)           \
  V(kEndOfStrongFieldsOffset, 0)                                            \
  V(kStartOfWeakFieldsOffset,                                               \
    (NATIVE_CONTEXT_SLOTS - FIRST_WEAK_SLOT) * kTaggedSize)                 \
  V(kEndOfWeakFieldsOffset, 0)                                              \
  V(kEndOfNativeContextFieldsOffset, 0)                                     \
  V(kEndOfTaggedFieldsOffset, 0)                                            \
  /* Raw data. */                                                           \
  V(kMicrotaskQueueOffset, kSystemPointerSize)                              \
  /* Total size. */                                                         \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(Context::kExtendedHeaderSize,
                                NATIVE_CONTEXT_FIELDS_DEF)
#undef NATIVE_CONTEXT_FIELDS_DEF

  class BodyDescriptor;

  void ResetErrorsThrown();
  void IncrementErrorsThrown();
  int GetErrorsThrown();

#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  void RunPromiseHook(PromiseHookType type, Handle<JSPromise> promise,
                      Handle<Object> parent);
#endif

 private:
  static_assert(OffsetOfElementAt(EMBEDDER_DATA_INDEX) ==
                Internals::kNativeContextEmbedderDataOffset);

  OBJECT_CONSTRUCTORS(NativeContext, Context);
};

class ScriptContextTableShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  using ElementT = Context;
  using CompressionScheme = V8HeapCompressionScheme;
  static constexpr RootIndex kMapRootIndex = RootIndex::kScriptContextTableMap;
  static constexpr bool kLengthEqualsCapacity = false;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kLengthOffset, kTaggedSize)                                         \
  V(kNamesToContextIndexOffset, kTaggedSize)                            \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// A table of all script contexts. Every loaded top-level script with top-level
// lexical declarations contributes its ScriptContext into this table.
class ScriptContextTable
    : public TaggedArrayBase<ScriptContextTable, ScriptContextTableShape> {
  using Super = TaggedArrayBase<ScriptContextTable, ScriptContextTableShape>;
  OBJECT_CONSTRUCTORS(ScriptContextTable, Super);

 public:
  using Shape = ScriptContextTableShape;

  static Handle<ScriptContextTable> New(
      Isolate* isolate, int capacity,
      AllocationType allocation = AllocationType::kYoung);

  DECL_RELEASE_ACQUIRE_INT_ACCESSORS(length)
  DECL_ACCESSORS(names_to_context_index, Tagged<NameToIndexHashTable>)

  inline Tagged<Context> get(int index) const;
  inline Tagged<Context> get(int index, AcquireLoadTag) const;

  // Lookup a variable `name` in a ScriptContextTable.
  // If it returns true, the variable is found and `result` contains
  // valid information about its location.
  // If it returns false, `result` is untouched.
  V8_WARN_UNUSED_RESULT
  V8_EXPORT_PRIVATE bool Lookup(Handle<String> name,
                                VariableLookupResult* result);

  V8_WARN_UNUSED_RESULT
  V8_EXPORT_PRIVATE static Handle<ScriptContextTable> Add(
      Isolate* isolate, Handle<ScriptContextTable> table,
      DirectHandle<Context> script_context, bool ignore_duplicates);

  DECL_PRINTER(ScriptContextTable)
  DECL_VERIFIER(ScriptContextTable)

  class BodyDescriptor;

  static constexpr int kLengthOffset = Shape::kLengthOffset;
  static constexpr int kNamesToContextIndexOffset =
      Shape::kNamesToContextIndexOffset;
};

using ContextField = Context::Field;

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_CONTEXTS_H_
