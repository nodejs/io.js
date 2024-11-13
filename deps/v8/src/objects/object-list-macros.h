// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECT_LIST_MACROS_H_
#define V8_OBJECTS_OBJECT_LIST_MACROS_H_

#include "src/base/macros.h"  // For IF_WASM.
#include "torque-generated/instance-types.h"

namespace v8 {
namespace internal {

// SIMPLE_HEAP_OBJECT_LIST1 and SIMPLE_HEAP_OBJECT_LIST2 are intended to
// simplify type-related boilerplate. How to use these lists: add types here,
// and don't add them in other related macro lists below (e.g.
// HEAP_OBJECT_ORDINARY_TYPE_LIST), and don't add them in various other spots
// (e.g. Map::GetVisitorId). Easy.
//
// All types in these lists, the 'simple' types, must satisfy the following
// conditions. They:
//
// - are an 'ordinary type' (HEAP_OBJECT_ORDINARY_TYPE_LIST)
// - define TypeCamelCase::AllocatedSize()
// - define TypeCamelCase::BodyDescriptor
// - have an associated visitor id kVisit##TypeCamelCase
// - have an associated instance type TYPE_UPPER_CASE##_TYPE
//
// Also don't forget about DYNAMICALLY_SIZED_HEAP_OBJECT_LIST.
//
// Note these lists are split into multiple lists for historic/pragmatic
// reasons since many users pass a macro `V` that expects exactly one argument.
//
// TODO(jgruber): Extend this list. There's more we can move here from
// HEAP_OBJECT_ORDINARY_TYPE_LIST.
// TODO(jgruber): Consider merging this file with objects-definitions.h.
#define SIMPLE_HEAP_OBJECT_LIST_GENERATOR(APPLY, V)                      \
  APPLY(V, ArrayList, ARRAY_LIST)                                        \
  APPLY(V, ByteArray, BYTE_ARRAY)                                        \
  APPLY(V, ClosureFeedbackCellArray, CLOSURE_FEEDBACK_CELL_ARRAY)        \
  APPLY(V, FixedArray, FIXED_ARRAY)                                      \
  APPLY(V, FixedDoubleArray, FIXED_DOUBLE_ARRAY)                         \
  APPLY(V, ObjectBoilerplateDescription, OBJECT_BOILERPLATE_DESCRIPTION) \
  APPLY(V, RegExpMatchInfo, REG_EXP_MATCH_INFO)                          \
  APPLY(V, ScriptContextTable, SCRIPT_CONTEXT_TABLE)                     \
  APPLY(V, WeakFixedArray, WEAK_FIXED_ARRAY)

// The SIMPLE_HEAP_OBJECT_LIST1 format is:
//   V(TypeCamelCase)
//
#define SIMPLE_HEAP_OBJECT_LIST1_ADAPTER(V, Name, NAME) V(Name)
#define SIMPLE_HEAP_OBJECT_LIST1(V) \
  SIMPLE_HEAP_OBJECT_LIST_GENERATOR(SIMPLE_HEAP_OBJECT_LIST1_ADAPTER, V)

// The SIMPLE_HEAP_OBJECT_LIST2 format is:
//   V(TypeCamelCase, TYPE_UPPER_CASE)
//
#define SIMPLE_HEAP_OBJECT_LIST2_ADAPTER(V, Name, NAME) V(Name, NAME)
#define SIMPLE_HEAP_OBJECT_LIST2(V) \
  SIMPLE_HEAP_OBJECT_LIST_GENERATOR(SIMPLE_HEAP_OBJECT_LIST2_ADAPTER, V)

// Types in this list may be allocated in large object spaces.
#define DYNAMICALLY_SIZED_HEAP_OBJECT_LIST(V) \
  V(ArrayList)                                \
  V(BigInt)                                   \
  V(ByteArray)                                \
  V(BytecodeArray)                            \
  V(ClosureFeedbackCellArray)                 \
  V(Code)                                     \
  V(Context)                                  \
  V(ExternalPointerArray)                     \
  V(ExternalString)                           \
  V(FeedbackMetadata)                         \
  V(FeedbackVector)                           \
  V(FixedArray)                               \
  V(FixedDoubleArray)                         \
  V(FreeSpace)                                \
  V(InstructionStream)                        \
  V(ObjectBoilerplateDescription)             \
  V(PreparseData)                             \
  V(PropertyArray)                            \
  V(ProtectedFixedArray)                      \
  V(RegExpMatchInfo)                          \
  V(ScopeInfo)                                \
  V(ScriptContextTable)                       \
  V(SeqString)                                \
  V(SloppyArgumentsElements)                  \
  V(SwissNameDictionary)                      \
  V(ThinString)                               \
  V(TrustedByteArray)                         \
  V(TrustedFixedArray)                        \
  V(TrustedWeakFixedArray)                    \
  V(UncompiledDataWithoutPreparseData)        \
  V(WeakArrayList)                            \
  V(WeakFixedArray)                           \
  IF_WASM(V, WasmArray)                       \
  IF_WASM(V, WasmDispatchTable)               \
  IF_WASM(V, WasmStruct)

// TODO(jgruber): Move more types to SIMPLE_HEAP_OBJECT_LIST_GENERATOR.
#define HEAP_OBJECT_ORDINARY_TYPE_LIST_BASE(V)  \
  V(AbstractCode)                               \
  V(AccessCheckNeeded)                          \
  V(AccessorInfo)                               \
  V(AllocationSite)                             \
  V(AlwaysSharedSpaceJSObject)                  \
  V(BigInt)                                     \
  V(BigIntBase)                                 \
  V(BigIntWrapper)                              \
  V(Boolean)                                    \
  V(BooleanWrapper)                             \
  V(ExternalPointerArray)                       \
  V(Callable)                                   \
  V(Cell)                                       \
  V(CompilationCacheTable)                      \
  V(ConsString)                                 \
  V(Constructor)                                \
  V(ConstTrackingLetCell)                       \
  V(Context)                                    \
  V(CoverageInfo)                               \
  V(DataHandler)                                \
  V(DeoptimizationData)                         \
  V(DependentCode)                              \
  V(DescriptorArray)                            \
  V(DictionaryTemplateInfo)                     \
  V(EmbedderDataArray)                          \
  V(EphemeronHashTable)                         \
  V(ExternalOneByteString)                      \
  V(ExternalString)                             \
  V(ExternalTwoByteString)                      \
  V(FeedbackCell)                               \
  V(FeedbackMetadata)                           \
  V(FeedbackVector)                             \
  V(FunctionTemplateInfo)                       \
  V(Filler)                                     \
  V(FixedArrayBase)                             \
  V(FixedArrayExact)                            \
  V(Foreign)                                    \
  V(FreeSpace)                                  \
  V(GcSafeCode)                                 \
  V(GlobalDictionary)                           \
  V(HandlerTable)                               \
  V(HeapNumber)                                 \
  V(InternalizedString)                         \
  V(JSArgumentsObject)                          \
  V(JSArray)                                    \
  V(JSArrayBuffer)                              \
  V(JSArrayBufferView)                          \
  V(JSArrayIterator)                            \
  V(JSAsyncFromSyncIterator)                    \
  V(JSAsyncFunctionObject)                      \
  V(JSAsyncGeneratorObject)                     \
  V(JSAtomicsCondition)                         \
  V(JSAtomicsMutex)                             \
  V(JSBoundFunction)                            \
  V(JSCollection)                               \
  V(JSCollectionIterator)                       \
  V(JSContextExtensionObject)                   \
  V(JSCustomElementsObject)                     \
  V(JSDataView)                                 \
  V(JSDataViewOrRabGsabDataView)                \
  V(JSDate)                                     \
  V(JSDisposableStackBase)                      \
  V(JSSyncDisposableStack)                      \
  V(JSAsyncDisposableStack)                     \
  V(JSError)                                    \
  V(JSExternalObject)                           \
  V(JSFinalizationRegistry)                     \
  V(JSFunction)                                 \
  V(JSFunctionOrBoundFunctionOrWrappedFunction) \
  V(JSGeneratorObject)                          \
  V(JSGlobalObject)                             \
  V(JSGlobalProxy)                              \
  V(JSIteratorHelper)                           \
  V(JSIteratorFilterHelper)                     \
  V(JSIteratorMapHelper)                        \
  V(JSIteratorTakeHelper)                       \
  V(JSIteratorDropHelper)                       \
  V(JSIteratorFlatMapHelper)                    \
  V(JSMap)                                      \
  V(JSMapIterator)                              \
  V(JSMessageObject)                            \
  V(JSModuleNamespace)                          \
  V(JSObject)                                   \
  V(JSAPIObjectWithEmbedderSlots)               \
  V(JSObjectWithEmbedderSlots)                  \
  V(JSPrimitiveWrapper)                         \
  V(JSPromise)                                  \
  V(JSProxy)                                    \
  V(JSRabGsabDataView)                          \
  V(JSRawJson)                                  \
  V(JSReceiver)                                 \
  V(JSRegExp)                                   \
  V(JSRegExpStringIterator)                     \
  V(JSSet)                                      \
  V(JSSetIterator)                              \
  V(JSShadowRealm)                              \
  V(JSSharedArray)                              \
  V(JSSharedStruct)                             \
  V(JSSpecialObject)                            \
  V(JSStringIterator)                           \
  V(JSSynchronizationPrimitive)                 \
  V(JSTemporalCalendar)                         \
  V(JSTemporalDuration)                         \
  V(JSTemporalInstant)                          \
  V(JSTemporalPlainDate)                        \
  V(JSTemporalPlainTime)                        \
  V(JSTemporalPlainDateTime)                    \
  V(JSTemporalPlainMonthDay)                    \
  V(JSTemporalPlainYearMonth)                   \
  V(JSTemporalTimeZone)                         \
  V(JSTemporalZonedDateTime)                    \
  V(JSTypedArray)                               \
  V(JSValidIteratorWrapper)                     \
  V(JSWeakCollection)                           \
  V(JSWeakRef)                                  \
  V(JSWeakMap)                                  \
  V(JSWeakSet)                                  \
  V(JSWrappedFunction)                          \
  V(LoadHandler)                                \
  V(Map)                                        \
  V(MapCache)                                   \
  V(MegaDomHandler)                             \
  V(Module)                                     \
  V(Microtask)                                  \
  V(Name)                                       \
  V(NameDictionary)                             \
  V(NameToIndexHashTable)                       \
  V(NativeContext)                              \
  V(NormalizedMapCache)                         \
  V(NumberDictionary)                           \
  V(NumberWrapper)                              \
  V(ObjectHashSet)                              \
  V(ObjectHashTable)                            \
  V(ObjectTemplateInfo)                         \
  V(ObjectTwoHashTable)                         \
  V(Oddball)                                    \
  V(Hole)                                       \
  V(OrderedHashMap)                             \
  V(OrderedHashSet)                             \
  V(OrderedNameDictionary)                      \
  V(OSROptimizedCodeCache)                      \
  V(PreparseData)                               \
  V(PrimitiveHeapObject)                        \
  V(PromiseReactionJobTask)                     \
  V(PropertyArray)                              \
  V(PropertyCell)                               \
  V(ScopeInfo)                                  \
  V(ScriptWrapper)                              \
  V(SeqOneByteString)                           \
  V(SeqString)                                  \
  V(SeqTwoByteString)                           \
  V(SharedFunctionInfo)                         \
  V(SimpleNumberDictionary)                     \
  V(SlicedString)                               \
  V(SmallOrderedHashMap)                        \
  V(SmallOrderedHashSet)                        \
  V(SmallOrderedNameDictionary)                 \
  V(SourceTextModule)                           \
  V(SourceTextModuleInfo)                       \
  V(StoreHandler)                               \
  V(String)                                     \
  V(StringSet)                                  \
  V(RegisteredSymbolTable)                      \
  V(StringWrapper)                              \
  V(Struct)                                     \
  V(SwissNameDictionary)                        \
  V(Symbol)                                     \
  V(SymbolWrapper)                              \
  V(SyntheticModule)                            \
  V(TemplateInfo)                               \
  V(TemplateLiteralObject)                      \
  V(ThinString)                                 \
  V(TransitionArray)                            \
  V(TurboshaftFloat64RangeType)                 \
  V(TurboshaftFloat64SetType)                   \
  V(TurboshaftFloat64Type)                      \
  V(TurboshaftType)                             \
  V(TurboshaftWord32RangeType)                  \
  V(TurboshaftWord32SetType)                    \
  V(TurboshaftWord32Type)                       \
  V(TurboshaftWord64RangeType)                  \
  V(TurboshaftWord64SetType)                    \
  V(TurboshaftWord64Type)                       \
  V(Undetectable)                               \
  V(UniqueName)                                 \
  IF_WASM(V, WasmArray)                         \
  IF_WASM(V, WasmContinuationObject)            \
  IF_WASM(V, WasmExceptionPackage)              \
  IF_WASM(V, WasmFuncRef)                       \
  IF_WASM(V, WasmGlobalObject)                  \
  IF_WASM(V, WasmInstanceObject)                \
  IF_WASM(V, WasmMemoryObject)                  \
  IF_WASM(V, WasmModuleObject)                  \
  IF_WASM(V, WasmNull)                          \
  IF_WASM(V, WasmObject)                        \
  IF_WASM(V, WasmResumeData)                    \
  IF_WASM(V, WasmStruct)                        \
  IF_WASM(V, WasmSuspenderObject)               \
  IF_WASM(V, WasmSuspendingObject)              \
  IF_WASM(V, WasmTableObject)                   \
  IF_WASM(V, WasmTagObject)                     \
  IF_WASM(V, WasmTypeInfo)                      \
  IF_WASM(V, WasmValueObject)                   \
  V(WeakArrayList)                              \
  V(WeakCell)                                   \
  TORQUE_DEFINED_CLASS_LIST(V)                  \
  SIMPLE_HEAP_OBJECT_LIST1(V)

#ifdef V8_INTL_SUPPORT
#define HEAP_OBJECT_ORDINARY_TYPE_LIST(V) \
  HEAP_OBJECT_ORDINARY_TYPE_LIST_BASE(V)  \
  V(JSV8BreakIterator)                    \
  V(JSCollator)                           \
  V(JSDateTimeFormat)                     \
  V(JSDisplayNames)                       \
  V(JSDurationFormat)                     \
  V(JSListFormat)                         \
  V(JSLocale)                             \
  V(JSNumberFormat)                       \
  V(JSPluralRules)                        \
  V(JSRelativeTimeFormat)                 \
  V(JSSegmentDataObject)                  \
  V(JSSegmentDataObjectWithIsWordLike)    \
  V(JSSegmentIterator)                    \
  V(JSSegmenter)                          \
  V(JSSegments)
#else
#define HEAP_OBJECT_ORDINARY_TYPE_LIST(V) HEAP_OBJECT_ORDINARY_TYPE_LIST_BASE(V)
#endif  // V8_INTL_SUPPORT

//
// Trusted Objects.
//
// Objects that are considered trusted. They must inherit from TrustedObject
// and live in trusted space, outside of the sandbox.
//

#define ABSTRACT_TRUSTED_OBJECT_LIST_GENERATOR(APPLY, V) \
  APPLY(V, TrustedObject, TRUSTED_OBJECT)                \
  APPLY(V, ExposedTrustedObject, EXPOSED_TRUSTED_OBJECT) \
  APPLY(V, UncompiledData, UNCOMPILED_DATA)              \
  IF_WASM(APPLY, V, WasmFunctionData, WASM_FUNCTION_DATA)

// Concrete trusted objects. These must:
// - (Transitively) inherit from TrustedObject
// - Have a unique instance type
// - Define a custom body descriptor
#define CONCRETE_TRUSTED_OBJECT_LIST_GENERATOR(APPLY, V)                       \
  APPLY(V, BytecodeArray, BYTECODE_ARRAY)                                      \
  APPLY(V, Code, CODE)                                                         \
  APPLY(V, InstructionStream, INSTRUCTION_STREAM)                              \
  APPLY(V, InterpreterData, INTERPRETER_DATA)                                  \
  APPLY(V, UncompiledDataWithPreparseData, UNCOMPILED_DATA_WITH_PREPARSE_DATA) \
  APPLY(V, UncompiledDataWithoutPreparseData,                                  \
        UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA)                                 \
  APPLY(V, UncompiledDataWithPreparseDataAndJob,                               \
        UNCOMPILED_DATA_WITH_PREPARSE_DATA_AND_JOB)                            \
  APPLY(V, UncompiledDataWithoutPreparseDataWithJob,                           \
        UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_WITH_JOB)                        \
  APPLY(V, SharedFunctionInfoWrapper, SHARED_FUNCTION_INFO_WRAPPER)            \
  APPLY(V, ProtectedFixedArray, PROTECTED_FIXED_ARRAY)                         \
  APPLY(V, TrustedByteArray, TRUSTED_BYTE_ARRAY)                               \
  APPLY(V, TrustedFixedArray, TRUSTED_FIXED_ARRAY)                             \
  APPLY(V, TrustedForeign, TRUSTED_FOREIGN)                                    \
  APPLY(V, TrustedWeakFixedArray, TRUSTED_WEAK_FIXED_ARRAY)                    \
  APPLY(V, AtomRegExpData, ATOM_REG_EXP_DATA)                                  \
  APPLY(V, IrRegExpData, IR_REG_EXP_DATA)                                      \
  APPLY(V, RegExpData, REG_EXP_DATA)                                           \
  IF_WASM(APPLY, V, WasmImportData, WASM_IMPORT_DATA)                          \
  IF_WASM(APPLY, V, WasmCapiFunctionData, WASM_CAPI_FUNCTION_DATA)             \
  IF_WASM(APPLY, V, WasmDispatchTable, WASM_DISPATCH_TABLE)                    \
  IF_WASM(APPLY, V, WasmExportedFunctionData, WASM_EXPORTED_FUNCTION_DATA)     \
  IF_WASM(APPLY, V, WasmJSFunctionData, WASM_JS_FUNCTION_DATA)                 \
  IF_WASM(APPLY, V, WasmInternalFunction, WASM_INTERNAL_FUNCTION)              \
  IF_WASM(APPLY, V, WasmTrustedInstanceData, WASM_TRUSTED_INSTANCE_DATA)

#define TRUSTED_OBJECT_LIST1_ADAPTER(V, Name, NAME) V(Name)
#define TRUSTED_OBJECT_LIST2_ADAPTER(V, Name, NAME) V(Name, NAME)

// The format is:
//   V(TypeCamelCase)
#define CONCRETE_TRUSTED_OBJECT_TYPE_LIST1(V) \
  CONCRETE_TRUSTED_OBJECT_LIST_GENERATOR(TRUSTED_OBJECT_LIST1_ADAPTER, V)
// The format is:
//   V(TypeCamelCase, TYPE_UPPER_CASE)
#define CONCRETE_TRUSTED_OBJECT_TYPE_LIST2(V) \
  CONCRETE_TRUSTED_OBJECT_LIST_GENERATOR(TRUSTED_OBJECT_LIST2_ADAPTER, V)

// The format is:
//   V(TypeCamelCase)
#define HEAP_OBJECT_TRUSTED_TYPE_LIST(V)                                  \
  ABSTRACT_TRUSTED_OBJECT_LIST_GENERATOR(TRUSTED_OBJECT_LIST1_ADAPTER, V) \
  CONCRETE_TRUSTED_OBJECT_LIST_GENERATOR(TRUSTED_OBJECT_LIST1_ADAPTER, V)

#define HEAP_OBJECT_TEMPLATE_TYPE_LIST(V) V(HashTable)

// Logical sub-types of heap objects that don't correspond to a C++ class but
// represent some specialization in terms of additional constraints.
#define HEAP_OBJECT_SPECIALIZED_TYPE_LIST(V) \
  V(AwaitContext)                            \
  V(BlockContext)                            \
  V(CallableApiObject)                       \
  V(CallableJSFunction)                      \
  V(CallableJSProxy)                         \
  V(CatchContext)                            \
  V(DebugEvaluateContext)                    \
  V(EvalContext)                             \
  V(FreeSpaceOrFiller)                       \
  V(FunctionContext)                         \
  V(JSApiObject)                             \
  V(JSClassConstructor)                      \
  V(JSLastDummyApiObject)                    \
  V(JSPromiseConstructor)                    \
  V(JSArrayConstructor)                      \
  V(JSRegExpConstructor)                     \
  V(JSMapKeyIterator)                        \
  V(JSMapKeyValueIterator)                   \
  V(JSMapValueIterator)                      \
  V(JSSetKeyValueIterator)                   \
  V(JSSetValueIterator)                      \
  V(JSSpecialApiObject)                      \
  V(ModuleContext)                           \
  V(NonNullForeign)                          \
  V(ScriptContext)                           \
  V(WithContext)                             \
  V(JSPrototype)                             \
  V(JSObjectPrototype)                       \
  V(JSRegExpPrototype)                       \
  V(JSPromisePrototype)                      \
  V(JSSetPrototype)                          \
  V(JSIteratorPrototype)                     \
  V(JSArrayIteratorPrototype)                \
  V(JSMapIteratorPrototype)                  \
  V(JSTypedArrayPrototype)                   \
  V(JSSetIteratorPrototype)                  \
  V(JSStringIteratorPrototype)               \
  V(TypedArrayConstructor)                   \
  V(Uint8TypedArrayConstructor)              \
  V(Int8TypedArrayConstructor)               \
  V(Uint16TypedArrayConstructor)             \
  V(Int16TypedArrayConstructor)              \
  V(Uint32TypedArrayConstructor)             \
  V(Int32TypedArrayConstructor)              \
  V(Float16TypedArrayConstructor)            \
  V(Float32TypedArrayConstructor)            \
  V(Float64TypedArrayConstructor)            \
  V(Uint8ClampedTypedArrayConstructor)       \
  V(Biguint64TypedArrayConstructor)          \
  V(Bigint64TypedArrayConstructor)

#define HEAP_OBJECT_TYPE_LIST(V)    \
  HEAP_OBJECT_ORDINARY_TYPE_LIST(V) \
  HEAP_OBJECT_TRUSTED_TYPE_LIST(V)  \
  HEAP_OBJECT_TEMPLATE_TYPE_LIST(V) \
  HEAP_OBJECT_SPECIALIZED_TYPE_LIST(V)

#define ODDBALL_LIST(V)                         \
  V(Undefined, undefined_value, UndefinedValue) \
  V(Null, null_value, NullValue)                \
  V(True, true_value, TrueValue)                \
  V(False, false_value, FalseValue)

#define HOLE_LIST(V)                                                   \
  V(TheHole, the_hole_value, TheHoleValue)                             \
  V(PropertyCellHole, property_cell_hole_value, PropertyCellHoleValue) \
  V(HashTableHole, hash_table_hole_value, HashTableHoleValue)          \
  V(PromiseHole, promise_hole_value, PromiseHoleValue)                 \
  V(Exception, exception, Exception)                                   \
  V(TerminationException, termination_exception, TerminationException) \
  V(Uninitialized, uninitialized_value, UninitializedValue)            \
  V(ArgumentsMarker, arguments_marker, ArgumentsMarker)                \
  V(OptimizedOut, optimized_out, OptimizedOut)                         \
  V(StaleRegister, stale_register, StaleRegister)                      \
  V(SelfReferenceMarker, self_reference_marker, SelfReferenceMarker)   \
  V(BasicBlockCountersMarker, basic_block_counters_marker,             \
    BasicBlockCountersMarker)

#define OBJECT_TYPE_LIST(V) \
  V(Primitive)              \
  V(Number)                 \
  V(Numeric)

// These forward-declarations expose heap object types to most of our codebase.
#define DEF_FWD_DECLARATION(Type) class Type;
HEAP_OBJECT_ORDINARY_TYPE_LIST(DEF_FWD_DECLARATION)
HEAP_OBJECT_TRUSTED_TYPE_LIST(DEF_FWD_DECLARATION)
HEAP_OBJECT_SPECIALIZED_TYPE_LIST(DEF_FWD_DECLARATION)
#undef DEF_FWD_DECLARATION

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECT_LIST_MACROS_H_
