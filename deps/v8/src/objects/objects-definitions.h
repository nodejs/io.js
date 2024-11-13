// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECTS_DEFINITIONS_H_
#define V8_OBJECTS_OBJECTS_DEFINITIONS_H_

#include "src/init/heap-symbols.h"
#include "torque-generated/instance-types.h"

namespace v8 {

namespace internal {

// All Maps have a field instance_type containing a InstanceType.
// It describes the type of the instances.
//
// As an example, a JavaScript object is a heap object and its map
// instance_type is JS_OBJECT_TYPE.
//
// The names of the string instance types are intended to systematically
// mirror their encoding in the instance_type field of the map.  The default
// encoding is considered TWO_BYTE.  It is not mentioned in the name.  ONE_BYTE
// encoding is mentioned explicitly in the name.  Likewise, the default
// representation is considered sequential.  It is not mentioned in the
// name.  The other representations (e.g. CONS, EXTERNAL) are explicitly
// mentioned.  Finally, the string is either a STRING_TYPE (if it is a normal
// string) or a INTERNALIZED_STRING_TYPE (if it is a internalized string).
//
// NOTE: The following things are some that depend on the string types having
// instance_types that are less than those of all other types:
// HeapObject::Size, HeapObject::IterateBody, the typeof operator, and
// Object::IsString.
#define INSTANCE_TYPE_LIST_BASE(V)                       \
  V(INTERNALIZED_TWO_BYTE_STRING_TYPE)                   \
  V(EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE)          \
  V(INTERNALIZED_ONE_BYTE_STRING_TYPE)                   \
  V(EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE)          \
  V(UNCACHED_EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE) \
  V(UNCACHED_EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE) \
  V(SEQ_TWO_BYTE_STRING_TYPE)                            \
  V(CONS_TWO_BYTE_STRING_TYPE)                           \
  V(EXTERNAL_TWO_BYTE_STRING_TYPE)                       \
  V(SLICED_TWO_BYTE_STRING_TYPE)                         \
  V(THIN_TWO_BYTE_STRING_TYPE)                           \
  V(SEQ_ONE_BYTE_STRING_TYPE)                            \
  V(CONS_ONE_BYTE_STRING_TYPE)                           \
  V(EXTERNAL_ONE_BYTE_STRING_TYPE)                       \
  V(SLICED_ONE_BYTE_STRING_TYPE)                         \
  V(THIN_ONE_BYTE_STRING_TYPE)                           \
  V(UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE)              \
  V(UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE)              \
  V(SHARED_SEQ_TWO_BYTE_STRING_TYPE)                     \
  V(SHARED_EXTERNAL_TWO_BYTE_STRING_TYPE)                \
  V(SHARED_SEQ_ONE_BYTE_STRING_TYPE)                     \
  V(SHARED_EXTERNAL_ONE_BYTE_STRING_TYPE)                \
  V(SHARED_UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE)       \
  V(SHARED_UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE)

#define INSTANCE_TYPE_LIST(V) \
  INSTANCE_TYPE_LIST_BASE(V)  \
  TORQUE_ASSIGNED_INSTANCE_TYPE_LIST(V)

// Since string types are not consecutive, this macro is used to iterate over
// them. The order matters for read only heap layout. The maps are placed such
// that string types map to address ranges of maps.
#define STRING_TYPE_LIST(V)                                                    \
  /* Start sequential strings*/                                                \
  V(SEQ_TWO_BYTE_STRING_TYPE, kVariableSizeSentinel, seq_two_byte_string,      \
    SeqTwoByteString)                                                          \
  V(SEQ_ONE_BYTE_STRING_TYPE, kVariableSizeSentinel, seq_one_byte_string,      \
    SeqOneByteString)                                                          \
  V(SHARED_SEQ_TWO_BYTE_STRING_TYPE, kVariableSizeSentinel,                    \
    shared_seq_two_byte_string, SharedSeqTwoByteString)                        \
  V(SHARED_SEQ_ONE_BYTE_STRING_TYPE, kVariableSizeSentinel,                    \
    shared_seq_one_byte_string, SharedSeqOneByteString)                        \
  /* Start internalized strings*/                                              \
  V(INTERNALIZED_TWO_BYTE_STRING_TYPE, kVariableSizeSentinel,                  \
    internalized_two_byte_string, InternalizedTwoByteString)                   \
  V(INTERNALIZED_ONE_BYTE_STRING_TYPE, kVariableSizeSentinel,                  \
    internalized_one_byte_string, InternalizedOneByteString)                   \
  /* End sequential strings*/                                                  \
  /* Start external strings*/                                                  \
  V(EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE, sizeof(ExternalTwoByteString), \
    external_internalized_two_byte_string, ExternalInternalizedTwoByteString)  \
  V(EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE, sizeof(ExternalOneByteString), \
    external_internalized_one_byte_string, ExternalInternalizedOneByteString)  \
  /* Start uncached external strings*/                                         \
  V(UNCACHED_EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE,                       \
    sizeof(UncachedExternalString),                                            \
    uncached_external_internalized_two_byte_string,                            \
    UncachedExternalInternalizedTwoByteString)                                 \
  V(UNCACHED_EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE,                       \
    sizeof(UncachedExternalString),                                            \
    uncached_external_internalized_one_byte_string,                            \
    UncachedExternalInternalizedOneByteString)                                 \
  /* End internalized strings*/                                                \
  V(UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE, sizeof(UncachedExternalString),    \
    uncached_external_two_byte_string, UncachedExternalTwoByteString)          \
  V(UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE, sizeof(UncachedExternalString),    \
    uncached_external_one_byte_string, UncachedExternalOneByteString)          \
  V(SHARED_UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE,                             \
    sizeof(UncachedExternalString), shared_uncached_external_two_byte_string,  \
    SharedUncachedExternalTwoByteString)                                       \
  V(SHARED_UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE,                             \
    sizeof(UncachedExternalString), shared_uncached_external_one_byte_string,  \
    SharedUncachedExternalOneByteString)                                       \
  /* End uncached external strings*/                                           \
  V(EXTERNAL_TWO_BYTE_STRING_TYPE, sizeof(ExternalTwoByteString),              \
    external_two_byte_string, ExternalTwoByteString)                           \
  V(EXTERNAL_ONE_BYTE_STRING_TYPE, sizeof(ExternalOneByteString),              \
    external_one_byte_string, ExternalOneByteString)                           \
  V(SHARED_EXTERNAL_TWO_BYTE_STRING_TYPE, sizeof(ExternalTwoByteString),       \
    shared_external_two_byte_string, SharedExternalTwoByteString)              \
  V(SHARED_EXTERNAL_ONE_BYTE_STRING_TYPE, sizeof(ExternalOneByteString),       \
    shared_external_one_byte_string, SharedExternalOneByteString)              \
  /* End external strings*/                                                    \
                                                                               \
  V(CONS_TWO_BYTE_STRING_TYPE, sizeof(ConsString), cons_two_byte_string,       \
    ConsTwoByteString)                                                         \
  V(CONS_ONE_BYTE_STRING_TYPE, sizeof(ConsString), cons_one_byte_string,       \
    ConsOneByteString)                                                         \
  V(SLICED_TWO_BYTE_STRING_TYPE, sizeof(SlicedString), sliced_two_byte_string, \
    SlicedTwoByteString)                                                       \
  V(SLICED_ONE_BYTE_STRING_TYPE, sizeof(SlicedString), sliced_one_byte_string, \
    SlicedOneByteString)                                                       \
  V(THIN_TWO_BYTE_STRING_TYPE, sizeof(ThinString), thin_two_byte_string,       \
    ThinTwoByteString)                                                         \
  V(THIN_ONE_BYTE_STRING_TYPE, sizeof(ThinString), thin_one_byte_string,       \
    ThinOneByteString)

// A struct is a simple object a set of object-valued fields.  Including an
// object type in this causes the compiler to generate most of the boilerplate
// code for the class including allocation and garbage collection routines,
// casts and predicates.  All you need to define is the class, methods and
// object verification routines.  Easy, no?
#define STRUCT_LIST_GENERATOR(V, _)                                           \
  V(_, PROMISE_FULFILL_REACTION_JOB_TASK_TYPE, PromiseFulfillReactionJobTask, \
    promise_fulfill_reaction_job_task)                                        \
  V(_, PROMISE_REJECT_REACTION_JOB_TASK_TYPE, PromiseRejectReactionJobTask,   \
    promise_reject_reaction_job_task)                                         \
  V(_, CALLABLE_TASK_TYPE, CallableTask, callable_task)                       \
  V(_, CALLBACK_TASK_TYPE, CallbackTask, callback_task)                       \
  V(_, PROMISE_RESOLVE_THENABLE_JOB_TASK_TYPE, PromiseResolveThenableJobTask, \
    promise_resolve_thenable_job_task)                                        \
  V(_, ACCESS_CHECK_INFO_TYPE, AccessCheckInfo, access_check_info)            \
  V(_, ACCESSOR_PAIR_TYPE, AccessorPair, accessor_pair)                       \
  V(_, ALIASED_ARGUMENTS_ENTRY_TYPE, AliasedArgumentsEntry,                   \
    aliased_arguments_entry)                                                  \
  V(_, ALLOCATION_MEMENTO_TYPE, AllocationMemento, allocation_memento)        \
  V(_, ARRAY_BOILERPLATE_DESCRIPTION_TYPE, ArrayBoilerplateDescription,       \
    array_boilerplate_description)                                            \
  IF_WASM(V, _, ASM_WASM_DATA_TYPE, AsmWasmData, asm_wasm_data)               \
  V(_, ASYNC_GENERATOR_REQUEST_TYPE, AsyncGeneratorRequest,                   \
    async_generator_request)                                                  \
  V(_, BREAK_POINT_TYPE, BreakPoint, break_point)                             \
  V(_, BREAK_POINT_INFO_TYPE, BreakPointInfo, break_point_info)               \
  V(_, BYTECODE_WRAPPER_TYPE, BytecodeWrapper, bytecode_wrapper)              \
  V(_, CALL_SITE_INFO_TYPE, CallSiteInfo, call_site_info)                     \
  V(_, CLASS_BOILERPLATE_TYPE, ClassBoilerplate, class_boilerplate)           \
  V(_, CLASS_POSITIONS_TYPE, ClassPositions, class_positions)                 \
  V(_, CODE_WRAPPER_TYPE, CodeWrapper, code_wrapper)                          \
  V(_, DEBUG_INFO_TYPE, DebugInfo, debug_info)                                \
  V(_, ENUM_CACHE_TYPE, EnumCache, enum_cache)                                \
  V(_, ERROR_STACK_DATA_TYPE, ErrorStackData, error_stack_data)               \
  V(_, FUNCTION_TEMPLATE_RARE_DATA_TYPE, FunctionTemplateRareData,            \
    function_template_rare_data)                                              \
  V(_, INTERCEPTOR_INFO_TYPE, InterceptorInfo, interceptor_info)              \
  V(_, MODULE_REQUEST_TYPE, ModuleRequest, module_request)                    \
  V(_, PROMISE_CAPABILITY_TYPE, PromiseCapability, promise_capability)        \
  V(_, PROMISE_REACTION_TYPE, PromiseReaction, promise_reaction)              \
  V(_, PROPERTY_DESCRIPTOR_OBJECT_TYPE, PropertyDescriptorObject,             \
    property_descriptor_object)                                               \
  V(_, PROTOTYPE_INFO_TYPE, PrototypeInfo, prototype_info)                    \
  V(_, REG_EXP_BOILERPLATE_DESCRIPTION_TYPE, RegExpBoilerplateDescription,    \
    regexp_boilerplate_description)                                           \
  V(_, REG_EXP_DATA_WRAPPER_TYPE, RegExpDataWrapper, regexp_data_wrapper)     \
  V(_, SCRIPT_TYPE, Script, script)                                           \
  V(_, SCRIPT_OR_MODULE_TYPE, ScriptOrModule, script_or_module)               \
  V(_, SOURCE_TEXT_MODULE_INFO_ENTRY_TYPE, SourceTextModuleInfoEntry,         \
    module_info_entry)                                                        \
  V(_, STACK_FRAME_INFO_TYPE, StackFrameInfo, stack_frame_info)               \
  V(_, TEMPLATE_OBJECT_DESCRIPTION_TYPE, TemplateObjectDescription,           \
    template_object_description)                                              \
  V(_, TUPLE2_TYPE, Tuple2, tuple2)                                           \
  IF_WASM(V, _, WASM_EXCEPTION_TAG_TYPE, WasmExceptionTag, wasm_exception_tag)

// Adapts one STRUCT_LIST_GENERATOR entry to the STRUCT_LIST entry
#define STRUCT_LIST_ADAPTER(V, NAME, Name, name) V(NAME, Name, name)

// Produces (NAME, Name, name) entries.
#define STRUCT_LIST(V) STRUCT_LIST_GENERATOR(STRUCT_LIST_ADAPTER, V)

// Adapts one STRUCT_LIST_GENERATOR entry to the STRUCT_MAPS_LIST entry
#define STRUCT_MAPS_LIST_ADAPTER(V, NAME, Name, name) \
  V(Map, name##_map, Name##Map)

// Produces (Map, struct_name_map, StructNameMap) entries
#define STRUCT_MAPS_LIST(V) STRUCT_LIST_GENERATOR(STRUCT_MAPS_LIST_ADAPTER, V)

//
// The following macros define list of allocation size objects and list of
// their maps.
//
#define ALLOCATION_SITE_LIST(V, _)                                          \
  V(_, ALLOCATION_SITE_TYPE, AllocationSite, WithWeakNext, allocation_site) \
  V(_, ALLOCATION_SITE_TYPE, AllocationSite, WithoutWeakNext,               \
    allocation_site_without_weaknext)

// Adapts one ALLOCATION_SITE_LIST entry to the ALLOCATION_SITE_MAPS_LIST entry
#define ALLOCATION_SITE_MAPS_LIST_ADAPTER(V, TYPE, Name, Size, name_size) \
  V(Map, name_size##_map, Name##Size##Map)

// Produces (Map, allocation_site_name_map, AllocationSiteNameMap) entries
#define ALLOCATION_SITE_MAPS_LIST(V) \
  ALLOCATION_SITE_LIST(ALLOCATION_SITE_MAPS_LIST_ADAPTER, V)

//
// The following macros define list of data handler objects and list of their
// maps.
//
#define DATA_HANDLER_LIST(V, _)                             \
  V(_, LOAD_HANDLER_TYPE, LoadHandler, 1, load_handler1)    \
  V(_, LOAD_HANDLER_TYPE, LoadHandler, 2, load_handler2)    \
  V(_, LOAD_HANDLER_TYPE, LoadHandler, 3, load_handler3)    \
  V(_, STORE_HANDLER_TYPE, StoreHandler, 0, store_handler0) \
  V(_, STORE_HANDLER_TYPE, StoreHandler, 1, store_handler1) \
  V(_, STORE_HANDLER_TYPE, StoreHandler, 2, store_handler2) \
  V(_, STORE_HANDLER_TYPE, StoreHandler, 3, store_handler3)

// Adapts one DATA_HANDLER_LIST entry to the DATA_HANDLER_MAPS_LIST entry.
#define DATA_HANDLER_MAPS_LIST_ADAPTER(V, TYPE, Name, Size, name_size) \
  V(Map, name_size##_map, Name##Size##Map)

// Produces (Map, handler_name_map, HandlerNameMap) entries
#define DATA_HANDLER_MAPS_LIST(V) \
  DATA_HANDLER_LIST(DATA_HANDLER_MAPS_LIST_ADAPTER, V)

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECTS_DEFINITIONS_H_
