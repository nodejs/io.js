// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ROOTS_ROOTS_H_
#define V8_ROOTS_ROOTS_H_

#include "src/base/macros.h"
#include "src/builtins/accessors.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/init/heap-symbols.h"
#include "src/objects/objects-definitions.h"
#include "src/objects/objects.h"
#include "src/objects/slots.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Boolean;
enum ElementsKind : uint8_t;
class Factory;
template <typename Impl>
class FactoryBase;
class LocalFactory;
class PropertyCell;
class ReadOnlyHeap;
class RootVisitor;

#define STRONG_READ_ONLY_HEAP_NUMBER_ROOT_LIST(V)         \
  /* Special numbers */                                   \
  V(HeapNumber, nan_value, NanValue)                      \
  V(HeapNumber, hole_nan_value, HoleNanValue)             \
  V(HeapNumber, infinity_value, InfinityValue)            \
  V(HeapNumber, minus_zero_value, MinusZeroValue)         \
  V(HeapNumber, minus_infinity_value, MinusInfinityValue) \
  V(HeapNumber, max_safe_integer, MaxSafeInteger)         \
  V(HeapNumber, max_uint_32, MaxUInt32)                   \
  V(HeapNumber, smi_min_value, SmiMinValue)               \
  V(HeapNumber, smi_max_value_plus_one, SmiMaxValuePlusOne)

// Adapts one INTERNALIZED_STRING_LIST_GENERATOR entry to
// the ROOT_LIST-compatible entry
#define INTERNALIZED_STRING_LIST_ADAPTER(V, name, ...) V(String, name, name)

// Produces (String, name, CamelCase) entries
#define EXTRA_IMPORTANT_INTERNALIZED_STRING_ROOT_LIST(V) \
  EXTRA_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(    \
      INTERNALIZED_STRING_LIST_ADAPTER, V)

// Defines all the read-only roots in Heap.
#define STRONG_READ_ONLY_ROOT_LIST(V)                                          \
  /* Cluster the most popular ones in a few cache lines here at the top.    */ \
  /* The first 32 entries are most often used in the startup snapshot and   */ \
  /* can use a shorter representation in the serialization format.          */ \
  V(Map, free_space_map, FreeSpaceMap)                                         \
  V(Map, one_pointer_filler_map, OnePointerFillerMap)                          \
  V(Map, two_pointer_filler_map, TwoPointerFillerMap)                          \
  V(Hole, uninitialized_value, UninitializedValue)                             \
  V(Undefined, undefined_value, UndefinedValue)                                \
  V(Hole, the_hole_value, TheHoleValue)                                        \
  V(Null, null_value, NullValue)                                               \
  V(True, true_value, TrueValue)                                               \
  V(False, false_value, FalseValue)                                            \
  EXTRA_IMPORTANT_INTERNALIZED_STRING_ROOT_LIST(V)                             \
  V(Map, meta_map, MetaMap)                                                    \
  V(Map, byte_array_map, ByteArrayMap)                                         \
  V(Map, fixed_array_map, FixedArrayMap)                                       \
  V(Map, fixed_cow_array_map, FixedCOWArrayMap)                                \
  V(Map, fixed_double_array_map, FixedDoubleArrayMap)                          \
  V(Map, hash_table_map, HashTableMap)                                         \
  V(Map, symbol_map, SymbolMap)                                                \
  V(Map, seq_one_byte_string_map, SeqOneByteStringMap)                         \
  V(Map, internalized_one_byte_string_map, InternalizedOneByteStringMap)       \
  V(Map, scope_info_map, ScopeInfoMap)                                         \
  V(Map, shared_function_info_map, SharedFunctionInfoMap)                      \
  V(Map, instruction_stream_map, InstructionStreamMap)                         \
  V(Map, cell_map, CellMap)                                                    \
  V(Map, global_property_cell_map, GlobalPropertyCellMap)                      \
  V(Map, foreign_map, ForeignMap)                                              \
  V(Map, heap_number_map, HeapNumberMap)                                       \
  V(Map, transition_array_map, TransitionArrayMap)                             \
  /* TODO(mythria): Once lazy feedback lands, check if feedback vector map */  \
  /* is still a popular map */                                                 \
  V(Map, feedback_vector_map, FeedbackVectorMap)                               \
  V(ScopeInfo, empty_scope_info, EmptyScopeInfo)                               \
  V(FixedArray, empty_fixed_array, EmptyFixedArray)                            \
  V(DescriptorArray, empty_descriptor_array, EmptyDescriptorArray)             \
  /* Entries beyond the first 32                                            */ \
  /* Holes */                                                                  \
  V(Hole, arguments_marker, ArgumentsMarker)                                   \
  V(Hole, exception, Exception)                                                \
  V(Hole, termination_exception, TerminationException)                         \
  V(Hole, optimized_out, OptimizedOut)                                         \
  V(Hole, stale_register, StaleRegister)                                       \
  V(Hole, property_cell_hole_value, PropertyCellHoleValue)                     \
  V(Hole, hash_table_hole_value, HashTableHoleValue)                           \
  V(Hole, promise_hole_value, PromiseHoleValue)                                \
  /* Maps */                                                                   \
  V(Map, script_context_table_map, ScriptContextTableMap)                      \
  V(Map, closure_feedback_cell_array_map, ClosureFeedbackCellArrayMap)         \
  V(Map, feedback_metadata_map, FeedbackMetadataArrayMap)                      \
  V(Map, array_list_map, ArrayListMap)                                         \
  V(Map, bigint_map, BigIntMap)                                                \
  V(Map, object_boilerplate_description_map, ObjectBoilerplateDescriptionMap)  \
  V(Map, bytecode_array_map, BytecodeArrayMap)                                 \
  V(Map, code_map, CodeMap)                                                    \
  V(Map, coverage_info_map, CoverageInfoMap)                                   \
  V(Map, dictionary_template_info_map, DictionaryTemplateInfoMap)              \
  V(Map, global_dictionary_map, GlobalDictionaryMap)                           \
  V(Map, global_const_tracking_let_cell_map, GlobalConstTrackingLetCellMap)    \
  V(Map, many_closures_cell_map, ManyClosuresCellMap)                          \
  V(Map, mega_dom_handler_map, MegaDomHandlerMap)                              \
  V(Map, module_info_map, ModuleInfoMap)                                       \
  V(Map, name_dictionary_map, NameDictionaryMap)                               \
  V(Map, no_closures_cell_map, NoClosuresCellMap)                              \
  V(Map, number_dictionary_map, NumberDictionaryMap)                           \
  V(Map, one_closure_cell_map, OneClosureCellMap)                              \
  V(Map, ordered_hash_map_map, OrderedHashMapMap)                              \
  V(Map, ordered_hash_set_map, OrderedHashSetMap)                              \
  V(Map, name_to_index_hash_table_map, NameToIndexHashTableMap)                \
  V(Map, registered_symbol_table_map, RegisteredSymbolTableMap)                \
  V(Map, ordered_name_dictionary_map, OrderedNameDictionaryMap)                \
  V(Map, preparse_data_map, PreparseDataMap)                                   \
  V(Map, property_array_map, PropertyArrayMap)                                 \
  V(Map, accessor_info_map, AccessorInfoMap)                                   \
  V(Map, regexp_match_info_map, RegExpMatchInfoMap)                            \
  V(Map, regexp_data_map, RegExpDataMap)                                       \
  V(Map, atom_regexp_data_map, AtomRegExpDataMap)                              \
  V(Map, ir_regexp_data_map, IrRegExpDataMap)                                  \
  V(Map, simple_number_dictionary_map, SimpleNumberDictionaryMap)              \
  V(Map, small_ordered_hash_map_map, SmallOrderedHashMapMap)                   \
  V(Map, small_ordered_hash_set_map, SmallOrderedHashSetMap)                   \
  V(Map, small_ordered_name_dictionary_map, SmallOrderedNameDictionaryMap)     \
  V(Map, source_text_module_map, SourceTextModuleMap)                          \
  V(Map, swiss_name_dictionary_map, SwissNameDictionaryMap)                    \
  V(Map, synthetic_module_map, SyntheticModuleMap)                             \
  IF_WASM(V, Map, wasm_import_data_map, WasmImportDataMap)                     \
  IF_WASM(V, Map, wasm_capi_function_data_map, WasmCapiFunctionDataMap)        \
  IF_WASM(V, Map, wasm_continuation_object_map, WasmContinuationObjectMap)     \
  IF_WASM(V, Map, wasm_dispatch_table_map, WasmDispatchTableMap)               \
  IF_WASM(V, Map, wasm_exported_function_data_map,                             \
          WasmExportedFunctionDataMap)                                         \
  IF_WASM(V, Map, wasm_internal_function_map, WasmInternalFunctionMap)         \
  IF_WASM(V, Map, wasm_func_ref_map, WasmFuncRefMap)                           \
  IF_WASM(V, Map, wasm_js_function_data_map, WasmJSFunctionDataMap)            \
  IF_WASM(V, Map, wasm_null_map, WasmNullMap)                                  \
  IF_WASM(V, Map, wasm_resume_data_map, WasmResumeDataMap)                     \
  IF_WASM(V, Map, wasm_suspender_object_map, WasmSuspenderObjectMap)           \
  IF_WASM(V, Map, wasm_trusted_instance_data_map, WasmTrustedInstanceDataMap)  \
  IF_WASM(V, Map, wasm_type_info_map, WasmTypeInfoMap)                         \
  V(Map, weak_fixed_array_map, WeakFixedArrayMap)                              \
  V(Map, weak_array_list_map, WeakArrayListMap)                                \
  V(Map, ephemeron_hash_table_map, EphemeronHashTableMap)                      \
  V(Map, embedder_data_array_map, EmbedderDataArrayMap)                        \
  V(Map, weak_cell_map, WeakCellMap)                                           \
  V(Map, external_pointer_array_map, ExternalPointerArrayMap)                  \
  V(Map, trusted_fixed_array_map, TrustedFixedArrayMap)                        \
  V(Map, trusted_weak_fixed_array_map, TrustedWeakFixedArrayMap)               \
  V(Map, trusted_byte_array_map, TrustedByteArrayMap)                          \
  V(Map, protected_fixed_array_map, ProtectedFixedArrayMap)                    \
  V(Map, interpreter_data_map, InterpreterDataMap)                             \
  V(Map, shared_function_info_wrapper_map, SharedFunctionInfoWrapperMap)       \
  V(Map, trusted_foreign_map, TrustedForeignMap)                               \
  /* String maps */                                                            \
  V(Map, seq_two_byte_string_map, SeqTwoByteStringMap)                         \
  V(Map, cons_two_byte_string_map, ConsTwoByteStringMap)                       \
  V(Map, cons_one_byte_string_map, ConsOneByteStringMap)                       \
  V(Map, thin_two_byte_string_map, ThinTwoByteStringMap)                       \
  V(Map, thin_one_byte_string_map, ThinOneByteStringMap)                       \
  V(Map, sliced_two_byte_string_map, SlicedTwoByteStringMap)                   \
  V(Map, sliced_one_byte_string_map, SlicedOneByteStringMap)                   \
  V(Map, external_two_byte_string_map, ExternalTwoByteStringMap)               \
  V(Map, external_one_byte_string_map, ExternalOneByteStringMap)               \
  V(Map, internalized_two_byte_string_map, InternalizedTwoByteStringMap)       \
  V(Map, external_internalized_two_byte_string_map,                            \
    ExternalInternalizedTwoByteStringMap)                                      \
  V(Map, external_internalized_one_byte_string_map,                            \
    ExternalInternalizedOneByteStringMap)                                      \
  V(Map, uncached_external_internalized_two_byte_string_map,                   \
    UncachedExternalInternalizedTwoByteStringMap)                              \
  V(Map, uncached_external_internalized_one_byte_string_map,                   \
    UncachedExternalInternalizedOneByteStringMap)                              \
  V(Map, uncached_external_two_byte_string_map,                                \
    UncachedExternalTwoByteStringMap)                                          \
  V(Map, uncached_external_one_byte_string_map,                                \
    UncachedExternalOneByteStringMap)                                          \
  V(Map, shared_seq_one_byte_string_map, SharedSeqOneByteStringMap)            \
  V(Map, shared_seq_two_byte_string_map, SharedSeqTwoByteStringMap)            \
  V(Map, shared_external_one_byte_string_map, SharedExternalOneByteStringMap)  \
  V(Map, shared_external_two_byte_string_map, SharedExternalTwoByteStringMap)  \
  V(Map, shared_uncached_external_one_byte_string_map,                         \
    SharedUncachedExternalOneByteStringMap)                                    \
  V(Map, shared_uncached_external_two_byte_string_map,                         \
    SharedUncachedExternalTwoByteStringMap)                                    \
  /* Oddball maps */                                                           \
  V(Map, undefined_map, UndefinedMap)                                          \
  V(Map, null_map, NullMap)                                                    \
  V(Map, boolean_map, BooleanMap)                                              \
  V(Map, hole_map, HoleMap)                                                    \
  /* Shared space object maps */                                               \
  V(Map, js_shared_array_map, JSSharedArrayMap)                                \
  V(Map, js_atomics_mutex_map, JSAtomicsMutexMap)                              \
  V(Map, js_atomics_condition_map, JSAtomicsConditionMap)                      \
  /* Canonical empty values */                                                 \
  V(EnumCache, empty_enum_cache, EmptyEnumCache)                               \
  V(PropertyArray, empty_property_array, EmptyPropertyArray)                   \
  V(ByteArray, empty_byte_array, EmptyByteArray)                               \
  V(ExternalPointerArray, empty_external_pointer_array,                        \
    EmptyExternalPointerArray)                                                 \
  V(ObjectBoilerplateDescription, empty_object_boilerplate_description,        \
    EmptyObjectBoilerplateDescription)                                         \
  V(ArrayBoilerplateDescription, empty_array_boilerplate_description,          \
    EmptyArrayBoilerplateDescription)                                          \
  V(ClosureFeedbackCellArray, empty_closure_feedback_cell_array,               \
    EmptyClosureFeedbackCellArray)                                             \
  V(NumberDictionary, empty_slow_element_dictionary,                           \
    EmptySlowElementDictionary)                                                \
  V(OrderedHashMap, empty_ordered_hash_map, EmptyOrderedHashMap)               \
  V(OrderedHashSet, empty_ordered_hash_set, EmptyOrderedHashSet)               \
  V(FeedbackMetadata, empty_feedback_metadata, EmptyFeedbackMetadata)          \
  V(NameDictionary, empty_property_dictionary, EmptyPropertyDictionary)        \
  V(OrderedNameDictionary, empty_ordered_property_dictionary,                  \
    EmptyOrderedPropertyDictionary)                                            \
  V(SwissNameDictionary, empty_swiss_property_dictionary,                      \
    EmptySwissPropertyDictionary)                                              \
  V(InterceptorInfo, noop_interceptor_info, NoOpInterceptorInfo)               \
  V(ArrayList, empty_array_list, EmptyArrayList)                               \
  V(WeakFixedArray, empty_weak_fixed_array, EmptyWeakFixedArray)               \
  V(WeakArrayList, empty_weak_array_list, EmptyWeakArrayList)                  \
  V(Cell, invalid_prototype_validity_cell, InvalidPrototypeValidityCell)       \
  STRONG_READ_ONLY_HEAP_NUMBER_ROOT_LIST(V)                                    \
  /* Table of strings of one-byte single characters */                         \
  V(FixedArray, single_character_string_table, SingleCharacterStringTable)     \
  /* Marker for self-references during code-generation */                      \
  V(Hole, self_reference_marker, SelfReferenceMarker)                          \
  /* Marker for basic-block usage counters array during code-generation */     \
  V(Hole, basic_block_counters_marker, BasicBlockCountersMarker)               \
  /* Canonical scope infos */                                                  \
  V(ScopeInfo, global_this_binding_scope_info, GlobalThisBindingScopeInfo)     \
  V(ScopeInfo, empty_function_scope_info, EmptyFunctionScopeInfo)              \
  V(ScopeInfo, native_scope_info, NativeScopeInfo)                             \
  V(ScopeInfo, shadow_realm_scope_info, ShadowRealmScopeInfo)                  \
  V(RegisteredSymbolTable, empty_symbol_table, EmptySymbolTable)               \
  /* Hash seed */                                                              \
  V(ByteArray, hash_seed, HashSeed)                                            \
  IF_WASM(V, HeapObject, wasm_null_padding, WasmNullPadding)                   \
  IF_WASM(V, WasmNull, wasm_null, WasmNull)

// TODO(saelo): ideally, these would be read-only roots (and then become part
// of the READ_ONLY_ROOT_LIST instead of the
// STRONG_MUTABLE_IMMOVABLE_ROOT_LIST). However, currently we do not have a
// trusted RO space.
#define TRUSTED_ROOT_LIST(V)                                              \
  V(TrustedByteArray, empty_trusted_byte_array, EmptyTrustedByteArray)    \
  V(TrustedFixedArray, empty_trusted_fixed_array, EmptyTrustedFixedArray) \
  V(TrustedWeakFixedArray, empty_trusted_weak_fixed_array,                \
    EmptyTrustedWeakFixedArray)                                           \
  V(ProtectedFixedArray, empty_protected_fixed_array, EmptyProtectedFixedArray)

#define BUILTINS_WITH_SFI_LIST_GENERATOR(APPLY, V)                             \
  APPLY(V, ProxyRevoke, proxy_revoke)                                          \
  APPLY(V, AsyncFromSyncIteratorCloseSyncAndRethrow,                           \
        async_from_sync_iterator_close_sync_and_rethrow)                       \
  APPLY(V, AsyncIteratorValueUnwrap, async_iterator_value_unwrap)              \
  APPLY(V, ArrayFromAsyncArrayLikeOnFulfilled,                                 \
        array_from_async_array_like_on_fulfilled)                              \
  APPLY(V, ArrayFromAsyncArrayLikeOnRejected,                                  \
        array_from_async_array_like_on_rejected)                               \
  APPLY(V, ArrayFromAsyncIterableOnFulfilled,                                  \
        array_from_async_iterable_on_fulfilled)                                \
  APPLY(V, ArrayFromAsyncIterableOnRejected,                                   \
        array_from_async_iterable_on_rejected)                                 \
  APPLY(V, PromiseCapabilityDefaultResolve,                                    \
        promise_capability_default_resolve)                                    \
  APPLY(V, PromiseCapabilityDefaultReject, promise_capability_default_reject)  \
  APPLY(V, PromiseGetCapabilitiesExecutor, promise_get_capabilities_executor)  \
  APPLY(V, PromiseAllSettledResolveElementClosure,                             \
        promise_all_settled_resolve_element_closure)                           \
  APPLY(V, PromiseAllSettledRejectElementClosure,                              \
        promise_all_settled_reject_element_closure)                            \
  APPLY(V, PromiseAllResolveElementClosure,                                    \
        promise_all_resolve_element_closure)                                   \
  APPLY(V, PromiseAnyRejectElementClosure, promise_any_reject_element_closure) \
  APPLY(V, PromiseThrowerFinally, promise_thrower_finally)                     \
  APPLY(V, PromiseValueThunkFinally, promise_value_thunk_finally)              \
  APPLY(V, PromiseThenFinally, promise_then_finally)                           \
  APPLY(V, PromiseCatchFinally, promise_catch_finally)                         \
  APPLY(V, ShadowRealmImportValueFulfilled, shadow_realm_import_value_fulfilled)

#define BUILTINS_WITH_SFI_ROOTS_LIST_ADAPTER(V, CamelName, underscore_name, \
                                             ...)                           \
  V(SharedFunctionInfo, underscore_name##_shared_fun, CamelName##SharedFun)

#define BUILTINS_WITH_SFI_ROOTS_LIST(V) \
  BUILTINS_WITH_SFI_LIST_GENERATOR(BUILTINS_WITH_SFI_ROOTS_LIST_ADAPTER, V)

// Mutable roots that are known to be immortal immovable, for which we can
// safely skip write barriers.
#define STRONG_MUTABLE_IMMOVABLE_ROOT_LIST(V)                                  \
  ACCESSOR_INFO_ROOT_LIST(V)                                                   \
  /* Maps */                                                                   \
  V(Map, external_map, ExternalMap)                                            \
  V(Map, message_object_map, JSMessageObjectMap)                               \
  /* Canonical empty values */                                                 \
  V(Script, empty_script, EmptyScript)                                         \
  V(FeedbackCell, many_closures_cell, ManyClosuresCell)                        \
  /* Protectors */                                                             \
  V(PropertyCell, array_constructor_protector, ArrayConstructorProtector)      \
  V(PropertyCell, no_elements_protector, NoElementsProtector)                  \
  V(PropertyCell, mega_dom_protector, MegaDOMProtector)                        \
  V(PropertyCell, no_profiling_protector, NoProfilingProtector)                \
  V(PropertyCell, no_undetectable_objects_protector,                           \
    NoUndetectableObjectsProtector)                                            \
  V(PropertyCell, is_concat_spreadable_protector, IsConcatSpreadableProtector) \
  V(PropertyCell, array_species_protector, ArraySpeciesProtector)              \
  V(PropertyCell, typed_array_species_protector, TypedArraySpeciesProtector)   \
  V(PropertyCell, promise_species_protector, PromiseSpeciesProtector)          \
  V(PropertyCell, regexp_species_protector, RegExpSpeciesProtector)            \
  V(PropertyCell, string_length_protector, StringLengthProtector)              \
  V(PropertyCell, array_iterator_protector, ArrayIteratorProtector)            \
  V(PropertyCell, array_buffer_detaching_protector,                            \
    ArrayBufferDetachingProtector)                                             \
  V(PropertyCell, promise_hook_protector, PromiseHookProtector)                \
  V(PropertyCell, promise_resolve_protector, PromiseResolveProtector)          \
  V(PropertyCell, map_iterator_protector, MapIteratorProtector)                \
  V(PropertyCell, promise_then_protector, PromiseThenProtector)                \
  V(PropertyCell, set_iterator_protector, SetIteratorProtector)                \
  V(PropertyCell, string_iterator_protector, StringIteratorProtector)          \
  V(PropertyCell, string_wrapper_to_primitive_protector,                       \
    StringWrapperToPrimitiveProtector)                                         \
  V(PropertyCell, number_string_not_regexp_like_protector,                     \
    NumberStringNotRegexpLikeProtector)                                        \
  /* Caches */                                                                 \
  V(FixedArray, string_split_cache, StringSplitCache)                          \
  V(FixedArray, regexp_multiple_cache, RegExpMultipleCache)                    \
  /* Indirection lists for isolate-independent builtins */                     \
  V(FixedArray, builtins_constants_table, BuiltinsConstantsTable)              \
  /* Internal SharedFunctionInfos */                                           \
  V(SharedFunctionInfo, async_function_await_reject_shared_fun,                \
    AsyncFunctionAwaitRejectSharedFun)                                         \
  V(SharedFunctionInfo, async_function_await_resolve_shared_fun,               \
    AsyncFunctionAwaitResolveSharedFun)                                        \
  V(SharedFunctionInfo, async_generator_await_reject_shared_fun,               \
    AsyncGeneratorAwaitRejectSharedFun)                                        \
  V(SharedFunctionInfo, async_generator_await_resolve_shared_fun,              \
    AsyncGeneratorAwaitResolveSharedFun)                                       \
  V(SharedFunctionInfo, async_generator_yield_with_await_resolve_shared_fun,   \
    AsyncGeneratorYieldWithAwaitResolveSharedFun)                              \
  V(SharedFunctionInfo, async_generator_return_resolve_shared_fun,             \
    AsyncGeneratorReturnResolveSharedFun)                                      \
  V(SharedFunctionInfo, async_generator_return_closed_reject_shared_fun,       \
    AsyncGeneratorReturnClosedRejectSharedFun)                                 \
  V(SharedFunctionInfo, async_generator_return_closed_resolve_shared_fun,      \
    AsyncGeneratorReturnClosedResolveSharedFun)                                \
  V(SharedFunctionInfo, source_text_module_execute_async_module_fulfilled_sfi, \
    SourceTextModuleExecuteAsyncModuleFulfilledSFI)                            \
  V(SharedFunctionInfo, source_text_module_execute_async_module_rejected_sfi,  \
    SourceTextModuleExecuteAsyncModuleRejectedSFI)                             \
  V(SharedFunctionInfo, atomics_mutex_async_unlock_resolve_handler_sfi,        \
    AtomicsMutexAsyncUnlockResolveHandlerSFI)                                  \
  V(SharedFunctionInfo, atomics_mutex_async_unlock_reject_handler_sfi,         \
    AtomicsMutexAsyncUnlockRejectHandlerSFI)                                   \
  V(SharedFunctionInfo, atomics_condition_acquire_lock_sfi,                    \
    AtomicsConditionAcquireLockSFI)                                            \
  V(SharedFunctionInfo, async_dispose_from_sync_dispose_shared_fun,            \
    AsyncDisposeFromSyncDisposeSharedFun)                                      \
  BUILTINS_WITH_SFI_ROOTS_LIST(V)                                              \
  TRUSTED_ROOT_LIST(V)

// These root references can be updated by the mutator.
#define STRONG_MUTABLE_MOVABLE_ROOT_LIST(V)                                 \
  /* Caches */                                                              \
  V(FixedArray, number_string_cache, NumberStringCache)                     \
  /* Lists and dictionaries */                                              \
  V(RegisteredSymbolTable, public_symbol_table, PublicSymbolTable)          \
  V(RegisteredSymbolTable, api_symbol_table, ApiSymbolTable)                \
  V(RegisteredSymbolTable, api_private_symbol_table, ApiPrivateSymbolTable) \
  V(WeakArrayList, script_list, ScriptList)                                 \
  V(FixedArray, materialized_objects, MaterializedObjects)                  \
  V(WeakArrayList, detached_contexts, DetachedContexts)                     \
  /* Feedback vectors that we need for code coverage or type profile */     \
  V(Object, feedback_vectors_for_profiling_tools,                           \
    FeedbackVectorsForProfilingTools)                                       \
  V(HeapObject, serialized_objects, SerializedObjects)                      \
  V(FixedArray, serialized_global_proxy_sizes, SerializedGlobalProxySizes)  \
  V(ArrayList, message_listeners, MessageListeners)                         \
  /* Support for async stack traces */                                      \
  V(HeapObject, current_microtask, CurrentMicrotask)                        \
  /* KeepDuringJob set for JS WeakRefs */                                   \
  V(HeapObject, weak_refs_keep_during_job, WeakRefsKeepDuringJob)           \
  V(Object, functions_marked_for_manual_optimization,                       \
    FunctionsMarkedForManualOptimization)                                   \
  V(ArrayList, basic_block_profiling_data, BasicBlockProfilingData)         \
  V(WeakArrayList, shared_wasm_memories, SharedWasmMemories)                \
  /* EphemeronHashTable for debug scopes (local debug evaluate) */          \
  V(HeapObject, locals_block_list_cache, DebugLocalsBlockListCache)         \
  IF_WASM(V, HeapObject, active_continuation, ActiveContinuation)           \
  IF_WASM(V, HeapObject, active_suspender, ActiveSuspender)                 \
  IF_WASM(V, WeakArrayList, js_to_wasm_wrappers, JSToWasmWrappers)          \
  IF_WASM(V, WeakArrayList, wasm_canonical_rtts, WasmCanonicalRtts)         \
  /* Internal SharedFunctionInfos */                                        \
  V(FunctionTemplateInfo, error_stack_getter_fun_template,                  \
    ErrorStackGetterSharedFun)                                              \
  V(FunctionTemplateInfo, error_stack_setter_fun_template,                  \
    ErrorStackSetterSharedFun)

// Entries in this list are limited to Smis and are not visited during GC.
#define SMI_ROOT_LIST(V)                                                       \
  V(Smi, last_script_id, LastScriptId)                                         \
  V(Smi, last_debugging_id, LastDebuggingId)                                   \
  /* To distinguish the function templates, so that we can find them in the */ \
  /* function cache of the native context. */                                  \
  V(Smi, next_template_serial_number, NextTemplateSerialNumber)                \
  V(Smi, construct_stub_create_deopt_pc_offset,                                \
    ConstructStubCreateDeoptPCOffset)                                          \
  V(Smi, construct_stub_invoke_deopt_pc_offset,                                \
    ConstructStubInvokeDeoptPCOffset)                                          \
  V(Smi, interpreter_entry_return_pc_offset, InterpreterEntryReturnPCOffset)

// Produces (String, name, CamelCase) entries
#define INTERNALIZED_STRING_ROOT_LIST(V)            \
  IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR(     \
      INTERNALIZED_STRING_LIST_ADAPTER, V)          \
  NOT_IMPORTANT_INTERNALIZED_STRING_LIST_GENERATOR( \
      INTERNALIZED_STRING_LIST_ADAPTER, V)

// Adapts one XXX_SYMBOL_LIST_GENERATOR entry to the ROOT_LIST-compatible entry
#define SYMBOL_ROOT_LIST_ADAPTER(V, name, ...) V(Symbol, name, name)

// Produces (Symbol, name, CamelCase) entries
#define PRIVATE_SYMBOL_ROOT_LIST(V) \
  PRIVATE_SYMBOL_LIST_GENERATOR(SYMBOL_ROOT_LIST_ADAPTER, V)
#define PUBLIC_SYMBOL_ROOT_LIST(V) \
  PUBLIC_SYMBOL_LIST_GENERATOR(SYMBOL_ROOT_LIST_ADAPTER, V)
#define WELL_KNOWN_SYMBOL_ROOT_LIST(V) \
  WELL_KNOWN_SYMBOL_LIST_GENERATOR(SYMBOL_ROOT_LIST_ADAPTER, V)

// Produces (Na,e, name, CamelCase) entries
#define NAME_FOR_PROTECTOR_ROOT_LIST(V)                                   \
  INTERNALIZED_STRING_FOR_PROTECTOR_LIST_GENERATOR(                       \
      INTERNALIZED_STRING_LIST_ADAPTER, V)                                \
  SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(SYMBOL_ROOT_LIST_ADAPTER, V)        \
  PUBLIC_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(SYMBOL_ROOT_LIST_ADAPTER, V) \
  WELL_KNOWN_SYMBOL_FOR_PROTECTOR_LIST_GENERATOR(SYMBOL_ROOT_LIST_ADAPTER, V)

// Adapts one ACCESSOR_INFO_LIST_GENERATOR entry to the ROOT_LIST-compatible
// entry
#define ACCESSOR_INFO_ROOT_LIST_ADAPTER(V, name, CamelName, ...) \
  V(AccessorInfo, name##_accessor, CamelName##Accessor)

// Produces (AccessorInfo, name, CamelCase) entries
#define ACCESSOR_INFO_ROOT_LIST(V) \
  ACCESSOR_INFO_LIST_GENERATOR(ACCESSOR_INFO_ROOT_LIST_ADAPTER, V)

#define READ_ONLY_ROOT_LIST(V)     \
  STRONG_READ_ONLY_ROOT_LIST(V)    \
  INTERNALIZED_STRING_ROOT_LIST(V) \
  PRIVATE_SYMBOL_ROOT_LIST(V)      \
  PUBLIC_SYMBOL_ROOT_LIST(V)       \
  WELL_KNOWN_SYMBOL_ROOT_LIST(V)   \
  STRUCT_MAPS_LIST(V)              \
  TORQUE_DEFINED_MAP_ROOT_LIST(V)  \
  ALLOCATION_SITE_MAPS_LIST(V)     \
  NAME_FOR_PROTECTOR_ROOT_LIST(V)  \
  DATA_HANDLER_MAPS_LIST(V)

#define MUTABLE_ROOT_LIST(V)            \
  STRONG_MUTABLE_IMMOVABLE_ROOT_LIST(V) \
  STRONG_MUTABLE_MOVABLE_ROOT_LIST(V)   \
  SMI_ROOT_LIST(V)

#define ROOT_LIST(V)     \
  READ_ONLY_ROOT_LIST(V) \
  MUTABLE_ROOT_LIST(V)

// Declare all the root indices.  This defines the root list order.
// clang-format off
enum class RootIndex : uint16_t {
#define COUNT_ROOT(...) +1
#define DECL(type, name, CamelName) k##CamelName,
  ROOT_LIST(DECL)
#undef DECL

  kRootListLength,

  // Helper aliases for inclusive regions of root indices.
  kFirstRoot = 0,
  kLastRoot = kRootListLength - 1,

  kReadOnlyRootsCount = 0 READ_ONLY_ROOT_LIST(COUNT_ROOT),
  kImmortalImmovableRootsCount =
      kReadOnlyRootsCount STRONG_MUTABLE_IMMOVABLE_ROOT_LIST(COUNT_ROOT),

  kFirstReadOnlyRoot = kFirstRoot,
  kLastReadOnlyRoot = kFirstReadOnlyRoot + kReadOnlyRootsCount - 1,

  kFirstHeapNumberRoot = kNanValue,
  kLastHeapNumberRoot = kSmiMaxValuePlusOne,

  // Keep this in sync with the first map allocated by
  // Heap::CreateLateReadOnlyJSReceiverMaps.
  kFirstJSReceiverMapRoot = kJSSharedArrayMap,

  // Use for fast protector update checks
  kFirstNameForProtector = kconstructor_string,
  kNameForProtectorCount = 0 NAME_FOR_PROTECTOR_ROOT_LIST(COUNT_ROOT),
  kLastNameForProtector = kFirstNameForProtector + kNameForProtectorCount - 1,

  // The strong roots visited by the garbage collector (not including read-only
  // roots).
  kMutableRootsCount = 0
      STRONG_MUTABLE_IMMOVABLE_ROOT_LIST(COUNT_ROOT)
      STRONG_MUTABLE_MOVABLE_ROOT_LIST(COUNT_ROOT),
  kFirstStrongRoot = kLastReadOnlyRoot + 1,
  kLastStrongRoot = kFirstStrongRoot + kMutableRootsCount - 1,

  // All of the strong roots plus the read-only roots.
  kFirstStrongOrReadOnlyRoot = kFirstRoot,
  kLastStrongOrReadOnlyRoot = kLastStrongRoot,

  // All immortal immovable roots including read only ones.
  kFirstImmortalImmovableRoot = kFirstReadOnlyRoot,
  kLastImmortalImmovableRoot =
      kFirstImmortalImmovableRoot + kImmortalImmovableRootsCount - 1,

  kFirstSmiRoot = kLastStrongRoot + 1,
  kLastSmiRoot = kLastRoot
#undef COUNT_ROOT
};
// clang-format on

static_assert(RootIndex::kFirstNameForProtector <=
              RootIndex::kLastNameForProtector);
#define FOR_PROTECTOR_CHECK(type, name, CamelName)                             \
  static_assert(RootIndex::kFirstNameForProtector <= RootIndex::k##CamelName); \
  static_assert(RootIndex::k##CamelName <= RootIndex::kLastNameForProtector);
NAME_FOR_PROTECTOR_ROOT_LIST(FOR_PROTECTOR_CHECK)
#undef FOR_PROTECTOR_CHECK

// Represents a storage of V8 heap roots.
class RootsTable {
 public:
  static constexpr size_t kEntriesCount =
      static_cast<size_t>(RootIndex::kRootListLength);

  RootsTable() : roots_{} {}

  inline bool IsRootHandleLocation(Address* handle_location,
                                   RootIndex* index) const;

  template <typename T>
  bool IsRootHandle(Handle<T> handle, RootIndex* index) const;

  Address const& operator[](RootIndex root_index) const {
    size_t index = static_cast<size_t>(root_index);
    DCHECK_LT(index, kEntriesCount);
    return roots_[index];
  }

  FullObjectSlot slot(RootIndex root_index) {
    size_t index = static_cast<size_t>(root_index);
    DCHECK_LT(index, kEntriesCount);
    return FullObjectSlot(&roots_[index]);
  }

  static const char* name(RootIndex root_index) {
    size_t index = static_cast<size_t>(root_index);
    DCHECK_LT(index, kEntriesCount);
    return root_names_[index];
  }

  static constexpr int offset_of(RootIndex root_index) {
    return static_cast<int>(root_index) * kSystemPointerSize;
  }

  // Immortal immovable root objects are allocated in OLD space and GC never
  // moves them and the root table entries are guaranteed to not be modified
  // after initialization. Note, however, that contents of those root objects
  // that are allocated in writable space can still be modified after
  // initialization.
  // Generated code can treat direct references to these roots as constants.
  static constexpr bool IsImmortalImmovable(RootIndex root_index) {
    static_assert(static_cast<int>(RootIndex::kFirstImmortalImmovableRoot) ==
                  0);
    return static_cast<unsigned>(root_index) <=
           static_cast<unsigned>(RootIndex::kLastImmortalImmovableRoot);
  }

  static constexpr bool IsReadOnly(RootIndex root_index) {
    static_assert(static_cast<int>(RootIndex::kFirstReadOnlyRoot) == 0);
    return static_cast<unsigned>(root_index) <=
           static_cast<unsigned>(RootIndex::kLastReadOnlyRoot);
  }

 private:
  FullObjectSlot begin() {
    return FullObjectSlot(&roots_[static_cast<size_t>(RootIndex::kFirstRoot)]);
  }
  FullObjectSlot end() {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kLastRoot) + 1]);
  }

  // Used for iterating over all of the read-only and mutable strong roots.
  FullObjectSlot strong_or_read_only_roots_begin() const {
    static_assert(static_cast<size_t>(RootIndex::kLastReadOnlyRoot) ==
                  static_cast<size_t>(RootIndex::kFirstStrongRoot) - 1);
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kFirstStrongOrReadOnlyRoot)]);
  }
  FullObjectSlot strong_or_read_only_roots_end() const {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kLastStrongOrReadOnlyRoot) + 1]);
  }

  // The read-only, strong and Smi roots as defined by these accessors are all
  // disjoint.
  FullObjectSlot read_only_roots_begin() const {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kFirstReadOnlyRoot)]);
  }
  FullObjectSlot read_only_roots_end() const {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kLastReadOnlyRoot) + 1]);
  }

  FullObjectSlot strong_roots_begin() const {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kFirstStrongRoot)]);
  }
  FullObjectSlot strong_roots_end() const {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kLastStrongRoot) + 1]);
  }

  FullObjectSlot smi_roots_begin() const {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kFirstSmiRoot)]);
  }
  FullObjectSlot smi_roots_end() const {
    return FullObjectSlot(
        &roots_[static_cast<size_t>(RootIndex::kLastSmiRoot) + 1]);
  }

  Address& operator[](RootIndex root_index) {
    size_t index = static_cast<size_t>(root_index);
    DCHECK_LT(index, kEntriesCount);
    return roots_[index];
  }

  Address roots_[kEntriesCount];
  static const char* root_names_[kEntriesCount];

  friend class Isolate;
  friend class Heap;
  friend class Factory;
  friend class FactoryBase<Factory>;
  friend class FactoryBase<LocalFactory>;
  friend class PointerCompressedReadOnlyArtifacts;
  friend class ReadOnlyHeap;
  friend class ReadOnlyRoots;
  friend class RootsSerializer;
  friend class SoleReadOnlyHeap;
};

#define ROOT_TYPE_FWD_DECL(Type, name, CamelName) class Type;
READ_ONLY_ROOT_LIST(ROOT_TYPE_FWD_DECL)
#undef ROOT_TYPE_FWD_DECL

class ReadOnlyRoots {
 public:
  static constexpr size_t kEntriesCount =
      static_cast<size_t>(RootIndex::kReadOnlyRootsCount);

  V8_INLINE explicit ReadOnlyRoots(Heap* heap);
  V8_INLINE explicit ReadOnlyRoots(const Isolate* isolate);
  V8_INLINE explicit ReadOnlyRoots(LocalIsolate* isolate);

  // For `v8_enable_map_packing=true`, this will return a packed (also untagged)
  // map-word instead of a tagged heap pointer.
  MapWord one_pointer_filler_map_word();

#define ROOT_ACCESSOR(Type, name, CamelName)       \
  V8_INLINE Tagged<Type> name() const;             \
  V8_INLINE Tagged<Type> unchecked_##name() const; \
  V8_INLINE Handle<Type> name##_handle() const;

  READ_ONLY_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

  V8_INLINE bool IsNameForProtector(Tagged<HeapObject> object) const;
  V8_INLINE void VerifyNameForProtectorsPages() const;
#ifdef DEBUG
  void VerifyNameForProtectors();
#endif

  V8_INLINE Tagged<Boolean> boolean_value(bool value) const;
  V8_INLINE Handle<Boolean> boolean_value_handle(bool value) const;

  // Returns heap number with identical value if it already exists or the empty
  // handle otherwise.
  Handle<HeapNumber> FindHeapNumber(double value);

  V8_INLINE Address address_at(RootIndex root_index) const;
  V8_INLINE Tagged<Object> object_at(RootIndex root_index) const;
  V8_INLINE Handle<Object> handle_at(RootIndex root_index) const;

  // Check if a slot is initialized yet. Should only be neccessary for code
  // running during snapshot creation.
  V8_INLINE bool is_initialized(RootIndex root_index) const;

  // Iterate over all the read-only roots. This is not necessary for garbage
  // collection and is usually only performed as part of (de)serialization or
  // heap verification.
  void Iterate(RootVisitor* visitor);

  // Uncompress pointers in the static roots table and store them into the
  // actual roots table.
  void InitFromStaticRootsTable(Address cage_base);

 private:
  V8_INLINE Address first_name_for_protector() const;
  V8_INLINE Address last_name_for_protector() const;
#ifdef DEBUG
#define ROOT_TYPE_CHECK(Type, name, CamelName) \
  V8_EXPORT_PRIVATE bool CheckType_##name() const;

  READ_ONLY_ROOT_LIST(ROOT_TYPE_CHECK)
#undef ROOT_TYPE_CHECK
#endif

  V8_INLINE explicit ReadOnlyRoots(Address* ro_roots)
      : read_only_roots_(ro_roots) {}

  V8_INLINE Address* GetLocation(RootIndex root_index) const;

  Address* read_only_roots_;

  friend class ReadOnlyHeap;
  friend class DeserializerAllocator;
  friend class ReadOnlyHeapImageDeserializer;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_ROOTS_ROOTS_H_
