// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_DEFINITIONS_H_
#define V8_BUILTINS_BUILTINS_DEFINITIONS_H_

#include "builtins-generated/bytecodes-builtins-list.h"

// include generated header
#include "torque-generated/builtin-definitions.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS
#define IF_TSA(TSA_MACRO, CSA_MACRO) TSA_MACRO
#else
#define IF_TSA(TSA_MACRO, CSA_MACRO) CSA_MACRO
#endif

// CPP: Builtin in C++. Entered via BUILTIN_EXIT frame.
//      Args: name
// TFJ: Builtin in Turbofan, with JS linkage (callable as Javascript function).
//      Args: name, arguments count, explicit argument names...
// TFS: Builtin in Turbofan, with CodeStub linkage.
//      Args: name, needs context, explicit argument names...
// TFC: Builtin in Turbofan, with CodeStub linkage and custom descriptor.
//      Args: name, interface descriptor
// TSC: Builtin in Turboshaft, with CodeStub linkage and custom descriptor.
//      Args: name, interface descriptor
// TFH: Handlers in Turbofan, with CodeStub linkage.
//      Args: name, interface descriptor
// BCH: Bytecode Handlers, with bytecode dispatch linkage.
//      Args: name, OperandScale, Bytecode
// ASM: Builtin in platform-dependent assembly.
//      Args: name, interface descriptor

// Builtins are additionally split into tiers, where the tier determines the
// distance of the builtins table from the root register within IsolateData.
//
//  - Tier 0 (T0) are guaranteed to be close to the root register and can thus
//    be accessed efficiently root-relative calls (so not, e.g., calls from
//    generated code when short-builtin-calls is on).
//  - T1 builtins have no distance guarantees.
//
// Note, this mechanism works only if the set of T0 builtins is kept as small
// as possible. Please, resist the temptation to add your builtin here unless
// there's a very good reason.
#define BUILTIN_LIST_BASE_TIER0(CPP, TFJ, TFC, TFS, TFH, ASM) \
  /* Deoptimization entries. */                               \
  ASM(DeoptimizationEntry_Eager, DeoptimizationEntry)         \
  ASM(DeoptimizationEntry_Lazy, DeoptimizationEntry)          \
                                                              \
  /* GC write barrier. */                                     \
  TFC(RecordWriteSaveFP, WriteBarrier)                        \
  TFC(RecordWriteIgnoreFP, WriteBarrier)                      \
  TFC(EphemeronKeyBarrierSaveFP, WriteBarrier)                \
  TFC(EphemeronKeyBarrierIgnoreFP, WriteBarrier)              \
                                                              \
  /* Adaptor for CPP builtins. */                             \
  TFC(AdaptorWithBuiltinExitFrame, CppBuiltinAdaptor)

#define BUILTIN_LIST_BASE_TIER1(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, ASM)       \
  /* GC write barriers */                                                     \
  TFC(IndirectPointerBarrierSaveFP, IndirectPointerWriteBarrier)              \
  TFC(IndirectPointerBarrierIgnoreFP, IndirectPointerWriteBarrier)            \
                                                                              \
  /* TSAN support for stores in generated code. */                            \
  IF_TSAN(TFC, TSANRelaxedStore8IgnoreFP, TSANStore)                          \
  IF_TSAN(TFC, TSANRelaxedStore8SaveFP, TSANStore)                            \
  IF_TSAN(TFC, TSANRelaxedStore16IgnoreFP, TSANStore)                         \
  IF_TSAN(TFC, TSANRelaxedStore16SaveFP, TSANStore)                           \
  IF_TSAN(TFC, TSANRelaxedStore32IgnoreFP, TSANStore)                         \
  IF_TSAN(TFC, TSANRelaxedStore32SaveFP, TSANStore)                           \
  IF_TSAN(TFC, TSANRelaxedStore64IgnoreFP, TSANStore)                         \
  IF_TSAN(TFC, TSANRelaxedStore64SaveFP, TSANStore)                           \
  IF_TSAN(TFC, TSANSeqCstStore8IgnoreFP, TSANStore)                           \
  IF_TSAN(TFC, TSANSeqCstStore8SaveFP, TSANStore)                             \
  IF_TSAN(TFC, TSANSeqCstStore16IgnoreFP, TSANStore)                          \
  IF_TSAN(TFC, TSANSeqCstStore16SaveFP, TSANStore)                            \
  IF_TSAN(TFC, TSANSeqCstStore32IgnoreFP, TSANStore)                          \
  IF_TSAN(TFC, TSANSeqCstStore32SaveFP, TSANStore)                            \
  IF_TSAN(TFC, TSANSeqCstStore64IgnoreFP, TSANStore)                          \
  IF_TSAN(TFC, TSANSeqCstStore64SaveFP, TSANStore)                            \
                                                                              \
  /* TSAN support for loads in generated code. */                             \
  IF_TSAN(TFC, TSANRelaxedLoad32IgnoreFP, TSANLoad)                           \
  IF_TSAN(TFC, TSANRelaxedLoad32SaveFP, TSANLoad)                             \
  IF_TSAN(TFC, TSANRelaxedLoad64IgnoreFP, TSANLoad)                           \
  IF_TSAN(TFC, TSANRelaxedLoad64SaveFP, TSANLoad)                             \
                                                                              \
  /* Calls */                                                                 \
  /* ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList) */             \
  ASM(CallFunction_ReceiverIsNullOrUndefined, CallTrampoline)                 \
  ASM(CallFunction_ReceiverIsNotNullOrUndefined, CallTrampoline)              \
  ASM(CallFunction_ReceiverIsAny, CallTrampoline)                             \
  /* ES6 section 9.4.1.1 [[Call]] ( thisArgument, argumentsList) */           \
  ASM(CallBoundFunction, CallTrampoline)                                      \
  /* #sec-wrapped-function-exotic-objects-call-thisargument-argumentslist */  \
  TFC(CallWrappedFunction, CallTrampoline)                                    \
  /* ES6 section 7.3.12 Call(F, V, [argumentsList]) */                        \
  ASM(Call_ReceiverIsNullOrUndefined, CallTrampoline)                         \
  ASM(Call_ReceiverIsNotNullOrUndefined, CallTrampoline)                      \
  ASM(Call_ReceiverIsAny, CallTrampoline)                                     \
  TFC(Call_ReceiverIsNullOrUndefined_Baseline_Compact,                        \
      CallTrampoline_Baseline_Compact)                                        \
  TFC(Call_ReceiverIsNullOrUndefined_Baseline, CallTrampoline_Baseline)       \
  TFC(Call_ReceiverIsNotNullOrUndefined_Baseline_Compact,                     \
      CallTrampoline_Baseline_Compact)                                        \
  TFC(Call_ReceiverIsNotNullOrUndefined_Baseline, CallTrampoline_Baseline)    \
  TFC(Call_ReceiverIsAny_Baseline_Compact, CallTrampoline_Baseline_Compact)   \
  TFC(Call_ReceiverIsAny_Baseline, CallTrampoline_Baseline)                   \
  TFC(Call_ReceiverIsNullOrUndefined_WithFeedback,                            \
      CallTrampoline_WithFeedback)                                            \
  TFC(Call_ReceiverIsNotNullOrUndefined_WithFeedback,                         \
      CallTrampoline_WithFeedback)                                            \
  TFC(Call_ReceiverIsAny_WithFeedback, CallTrampoline_WithFeedback)           \
                                                                              \
  /* ES6 section 9.5.12[[Call]] ( thisArgument, argumentsList ) */            \
  TFC(CallProxy, CallTrampoline)                                              \
  ASM(CallVarargs, CallVarargs)                                               \
  TFC(CallWithSpread, CallWithSpread)                                         \
  TFC(CallWithSpread_Baseline, CallWithSpread_Baseline)                       \
  TFC(CallWithSpread_WithFeedback, CallWithSpread_WithFeedback)               \
  TFC(CallWithArrayLike, CallWithArrayLike)                                   \
  TFC(CallWithArrayLike_WithFeedback, CallWithArrayLike_WithFeedback)         \
  ASM(CallForwardVarargs, CallForwardVarargs)                                 \
  ASM(CallFunctionForwardVarargs, CallForwardVarargs)                         \
  /* Call an API callback via a {FunctionTemplateInfo}, doing appropriate */  \
  /* access and compatible receiver checks. */                                \
  TFC(CallFunctionTemplate_Generic, CallFunctionTemplateGeneric)              \
  TFC(CallFunctionTemplate_CheckAccess, CallFunctionTemplate)                 \
  TFC(CallFunctionTemplate_CheckCompatibleReceiver, CallFunctionTemplate)     \
  TFC(CallFunctionTemplate_CheckAccessAndCompatibleReceiver,                  \
      CallFunctionTemplate)                                                   \
                                                                              \
  /* Construct */                                                             \
  /* ES6 section 9.2.2 [[Construct]] ( argumentsList, newTarget) */           \
  ASM(ConstructFunction, JSTrampoline)                                        \
  /* ES6 section 9.4.1.2 [[Construct]] (argumentsList, newTarget) */          \
  ASM(ConstructBoundFunction, JSTrampoline)                                   \
  ASM(ConstructedNonConstructable, JSTrampoline)                              \
  /* ES6 section 7.3.13 Construct (F, [argumentsList], [newTarget]) */        \
  ASM(Construct, JSTrampoline)                                                \
  ASM(ConstructVarargs, ConstructVarargs)                                     \
  TFC(ConstructWithSpread, ConstructWithSpread)                               \
  TFC(ConstructWithSpread_Baseline, ConstructWithSpread_Baseline)             \
  TFC(ConstructWithSpread_WithFeedback, ConstructWithSpread_WithFeedback)     \
  TFC(ConstructWithArrayLike, ConstructWithArrayLike)                         \
  ASM(ConstructForwardVarargs, ConstructForwardVarargs)                       \
  ASM(ConstructForwardAllArgs, ConstructForwardAllArgs)                       \
  TFC(ConstructForwardAllArgs_Baseline, ConstructForwardAllArgs_Baseline)     \
  TFC(ConstructForwardAllArgs_WithFeedback,                                   \
      ConstructForwardAllArgs_WithFeedback)                                   \
  ASM(ConstructFunctionForwardVarargs, ConstructForwardVarargs)               \
  TFC(Construct_Baseline, Construct_Baseline)                                 \
  TFC(Construct_WithFeedback, Construct_WithFeedback)                         \
  ASM(JSConstructStubGeneric, ConstructStub)                                  \
  ASM(JSBuiltinsConstructStub, ConstructStub)                                 \
  TFC(FastNewObject, FastNewObject)                                           \
  TFS(FastNewClosure, NeedsContext::kYes, kSharedFunctionInfo, kFeedbackCell) \
  /* ES6 section 9.5.14 [[Construct]] ( argumentsList, newTarget) */          \
  TFC(ConstructProxy, JSTrampoline)                                           \
                                                                              \
  /* Apply and entries */                                                     \
  ASM(JSEntry, JSEntry)                                                       \
  ASM(JSConstructEntry, JSEntry)                                              \
  ASM(JSRunMicrotasksEntry, RunMicrotasksEntry)                               \
  /* Call a JSValue. */                                                       \
  ASM(JSEntryTrampoline, JSEntry)                                             \
  /* Construct a JSValue. */                                                  \
  ASM(JSConstructEntryTrampoline, JSEntry)                                    \
  ASM(ResumeGeneratorTrampoline, ResumeGenerator)                             \
                                                                              \
  /* String helpers */                                                        \
  IF_TSA(TSC, TFC)(StringFromCodePointAt, StringAtAsString)                    \
  TFC(StringEqual, StringEqual)                                                \
  TFC(StringGreaterThan, CompareNoContext)                                     \
  TFC(StringGreaterThanOrEqual, CompareNoContext)                              \
  TFC(StringLessThan, CompareNoContext)                                        \
  TFC(StringLessThanOrEqual, CompareNoContext)                                 \
  TFC(StringCompare, CompareNoContext)                                         \
  TFC(StringSubstring, StringSubstring)                                        \
                                                                               \
  /* OrderedHashTable helpers */                                               \
  TFS(OrderedHashTableHealIndex, NeedsContext::kYes, kTable, kIndex)           \
                                                                               \
  /* Interpreter */                                                            \
  /* InterpreterEntryTrampoline dispatches to the interpreter to run a */      \
  /* JSFunction in the form of bytecodes */                                    \
  ASM(InterpreterEntryTrampoline, JSTrampoline)                                \
  ASM(InterpreterEntryTrampolineForProfiling, JSTrampoline)                    \
  ASM(InterpreterForwardAllArgsThenConstruct, ConstructForwardAllArgs)         \
  ASM(InterpreterPushArgsThenCall, InterpreterPushArgsThenCall)                \
  ASM(InterpreterPushUndefinedAndArgsThenCall, InterpreterPushArgsThenCall)    \
  ASM(InterpreterPushArgsThenCallWithFinalSpread, InterpreterPushArgsThenCall) \
  ASM(InterpreterPushArgsThenConstruct, InterpreterPushArgsThenConstruct)      \
  ASM(InterpreterPushArgsThenFastConstructFunction,                            \
      InterpreterPushArgsThenConstruct)                                        \
  ASM(InterpreterPushArgsThenConstructArrayFunction,                           \
      InterpreterPushArgsThenConstruct)                                        \
  ASM(InterpreterPushArgsThenConstructWithFinalSpread,                         \
      InterpreterPushArgsThenConstruct)                                        \
  ASM(InterpreterEnterAtBytecode, Void)                                        \
  ASM(InterpreterEnterAtNextBytecode, Void)                                    \
  ASM(InterpreterOnStackReplacement, OnStackReplacement)                       \
                                                                               \
  /* Baseline Compiler */                                                      \
  ASM(BaselineOutOfLinePrologue, BaselineOutOfLinePrologue)                    \
  ASM(BaselineOutOfLinePrologueDeopt, Void)                                    \
  ASM(BaselineOnStackReplacement, OnStackReplacement)                          \
  ASM(BaselineLeaveFrame, BaselineLeaveFrame)                                  \
  ASM(BaselineOrInterpreterEnterAtBytecode, Void)                              \
  ASM(BaselineOrInterpreterEnterAtNextBytecode, Void)                          \
  ASM(InterpreterOnStackReplacement_ToBaseline, Void)                          \
                                                                               \
  /* Maglev Compiler */                                                        \
  ASM(MaglevOnStackReplacement, OnStackReplacement)                            \
  ASM(MaglevFunctionEntryStackCheck_WithoutNewTarget, Void)                    \
  ASM(MaglevFunctionEntryStackCheck_WithNewTarget, Void)                       \
  ASM(MaglevOptimizeCodeOrTailCallOptimizedCodeSlot,                           \
      MaglevOptimizeCodeOrTailCallOptimizedCodeSlot)                           \
                                                                               \
  /* Code life-cycle */                                                        \
  TFC(CompileLazy, JSTrampoline)                                               \
  TFC(CompileLazyDeoptimizedCode, JSTrampoline)                                \
  TFC(InstantiateAsmJs, JSTrampoline)                                          \
  ASM(NotifyDeoptimized, Void)                                                 \
                                                                               \
  /* Trampolines called when returning from a deoptimization that expects   */ \
  /* to continue in a JavaScript builtin to finish the functionality of a   */ \
  /* an TF-inlined version of builtin that has side-effects.                */ \
  /*                                                                        */ \
  /* The trampolines work as follows:                                       */ \
  /*   1. Trampoline restores input register values that                    */ \
  /*      the builtin expects from a BuiltinContinuationFrame.              */ \
  /*   2. Trampoline tears down BuiltinContinuationFrame.                   */ \
  /*   3. Trampoline jumps to the builtin's address.                        */ \
  /*   4. Builtin executes as if invoked by the frame above it.             */ \
  /*   5. When the builtin returns, execution resumes normally in the       */ \
  /*      calling frame, processing any return result from the JavaScript   */ \
  /*      builtin as if it had called the builtin directly.                 */ \
  /*                                                                        */ \
  /* There are two variants of the stub that differ in their handling of a  */ \
  /* value returned by the next frame deeper on the stack. For LAZY deopts, */ \
  /* the return value (e.g. rax on x64) is explicitly passed as an extra    */ \
  /* stack parameter to the JavaScript builtin by the "WithResult"          */ \
  /* trampoline variant. The plain variant is used in EAGER deopt contexts  */ \
  /* and has no such special handling. */                                      \
  ASM(ContinueToCodeStubBuiltin, ContinueToBuiltin)                            \
  ASM(ContinueToCodeStubBuiltinWithResult, ContinueToBuiltin)                  \
  ASM(ContinueToJavaScriptBuiltin, ContinueToBuiltin)                          \
  ASM(ContinueToJavaScriptBuiltinWithResult, ContinueToBuiltin)                \
                                                                               \
  /* API callback handling */                                                  \
  ASM(CallApiCallbackGeneric, CallApiCallbackGeneric)                          \
  ASM(CallApiCallbackOptimizedNoProfiling, CallApiCallbackOptimized)           \
  ASM(CallApiCallbackOptimized, CallApiCallbackOptimized)                      \
  ASM(CallApiGetter, ApiGetter)                                                \
  TFC(HandleApiCallOrConstruct, JSTrampoline)                                  \
  CPP(HandleApiConstruct)                                                      \
  CPP(HandleApiCallAsFunctionDelegate)                                         \
  CPP(HandleApiCallAsConstructorDelegate)                                      \
                                                                               \
  /* Adapters for Turbofan into runtime */                                     \
  TFC(AllocateInYoungGeneration, Allocate)                                     \
  TFC(AllocateInOldGeneration, Allocate)                                       \
  IF_WASM(TFC, WasmAllocateInYoungGeneration, Allocate)                        \
  IF_WASM(TFC, WasmAllocateInOldGeneration, Allocate)                          \
                                                                               \
  TFC(NewHeapNumber, NewHeapNumber)                                            \
                                                                               \
  /* TurboFan support builtins */                                              \
  TFS(CopyFastSmiOrObjectElements, NeedsContext::kNo, kObject)                 \
  TFC(GrowFastDoubleElements, GrowArrayElements)                               \
  TFC(GrowFastSmiOrObjectElements, GrowArrayElements)                          \
                                                                               \
  /* Debugger */                                                               \
  TFJ(DebugBreakTrampoline, kDontAdaptArgumentsSentinel)                       \
  ASM(RestartFrameTrampoline, RestartFrameTrampoline)                          \
                                                                               \
  /* Type conversions */                                                       \
  TFC(ToNumber, TypeConversion)                                                \
  TFC(ToBigInt, TypeConversion)                                                \
  TFC(ToNumber_Baseline, TypeConversion_Baseline)                              \
  TFC(ToNumeric_Baseline, TypeConversion_Baseline)                             \
  TFC(PlainPrimitiveToNumber, TypeConversionNoContext)                         \
  TFC(ToNumberConvertBigInt, TypeConversion)                                   \
  TFC(ToBigIntConvertNumber, TypeConversion)                                   \
  TFC(Typeof, Typeof)                                                          \
  TFC(Typeof_Baseline, UnaryOp_Baseline)                                       \
  TFC(BigIntToI64, BigIntToI64)                                                \
  TFC(BigIntToI32Pair, BigIntToI32Pair)                                        \
  TFC(I64ToBigInt, I64ToBigInt)                                                \
  TFC(I32PairToBigInt, I32PairToBigInt)                                        \
                                                                               \
  /* Type conversions continuations */                                         \
  TFC(ToBooleanLazyDeoptContinuation, SingleParameterOnStack)                  \
  TFC(MathCeilContinuation, SingleParameterOnStack)                            \
  TFC(MathFloorContinuation, SingleParameterOnStack)                           \
  TFC(MathRoundContinuation, SingleParameterOnStack)                           \
                                                                               \
  /* Handlers */                                                               \
  TFH(KeyedLoadIC_PolymorphicName, LoadWithVector)                             \
  TFH(KeyedStoreIC_Megamorphic, StoreWithVector)                               \
  TFH(DefineKeyedOwnIC_Megamorphic, StoreNoFeedback)                           \
  TFH(LoadGlobalIC_NoFeedback, LoadGlobalNoFeedback)                           \
  TFH(LoadIC_FunctionPrototype, LoadWithVector)                                \
  TFH(LoadIC_StringLength, LoadWithVector)                                     \
  TFH(LoadIC_StringWrapperLength, LoadWithVector)                              \
  TFH(LoadIC_NoFeedback, LoadNoFeedback)                                       \
  TFH(StoreGlobalIC_Slow, StoreWithVector)                                     \
  TFH(StoreIC_NoFeedback, StoreNoFeedback)                                     \
  TFH(DefineNamedOwnIC_NoFeedback, StoreNoFeedback)                            \
  TFH(KeyedLoadIC_SloppyArguments, LoadWithVector)                             \
  TFH(LoadIndexedInterceptorIC, LoadWithVector)                                \
  TFH(KeyedStoreIC_SloppyArguments_InBounds, StoreWithVector)                  \
  TFH(KeyedStoreIC_SloppyArguments_NoTransitionGrowAndHandleCOW,               \
      StoreWithVector)                                                         \
  TFH(KeyedStoreIC_SloppyArguments_NoTransitionIgnoreTypedArrayOOB,            \
      StoreWithVector)                                                         \
  TFH(KeyedStoreIC_SloppyArguments_NoTransitionHandleCOW, StoreWithVector)     \
  TFH(StoreFastElementIC_InBounds, StoreWithVector)                            \
  TFH(StoreFastElementIC_NoTransitionGrowAndHandleCOW, StoreWithVector)        \
  TFH(StoreFastElementIC_NoTransitionIgnoreTypedArrayOOB, StoreWithVector)     \
  TFH(StoreFastElementIC_NoTransitionHandleCOW, StoreWithVector)               \
  TFH(ElementsTransitionAndStore_InBounds, StoreTransition)                    \
  TFH(ElementsTransitionAndStore_NoTransitionGrowAndHandleCOW,                 \
      StoreTransition)                                                         \
  TFH(ElementsTransitionAndStore_NoTransitionIgnoreTypedArrayOOB,              \
      StoreTransition)                                                         \
  TFH(ElementsTransitionAndStore_NoTransitionHandleCOW, StoreTransition)       \
  TFH(KeyedHasIC_PolymorphicName, LoadWithVector)                              \
  TFH(KeyedHasIC_SloppyArguments, LoadWithVector)                              \
  TFH(HasIndexedInterceptorIC, LoadWithVector)                                 \
                                                                               \
  /* Microtask helpers */                                                      \
  TFS(EnqueueMicrotask, NeedsContext::kYes, kMicrotask)                        \
  ASM(RunMicrotasksTrampoline, RunMicrotasksEntry)                             \
  TFC(RunMicrotasks, RunMicrotasks)                                            \
                                                                               \
  /* Object property helpers */                                                \
  TFS(HasProperty, NeedsContext::kYes, kObject, kKey)                          \
  TFS(DeleteProperty, NeedsContext::kYes, kObject, kKey, kLanguageMode)        \
  /* ES #sec-copydataproperties */                                             \
  TFS(CopyDataProperties, NeedsContext::kYes, kTarget, kSource)                \
  TFS(SetDataProperties, NeedsContext::kYes, kTarget, kSource)                 \
  TFC(CopyDataPropertiesWithExcludedPropertiesOnStack,                         \
      CopyDataPropertiesWithExcludedPropertiesOnStack)                         \
  TFC(CopyDataPropertiesWithExcludedProperties,                                \
      CopyDataPropertiesWithExcludedProperties)                                \
                                                                               \
  /* Abort */                                                                  \
  TFC(Abort, Abort)                                                            \
  TFC(AbortCSADcheck, Abort)                                                   \
                                                                               \
  /* Built-in functions for Javascript */                                      \
  /* Special internal builtins */                                              \
  CPP(EmptyFunction)                                                           \
  CPP(Illegal)                                                                 \
  CPP(IllegalInvocationThrower)                                                \
  CPP(StrictPoisonPillThrower)                                                 \
  CPP(UnsupportedThrower)                                                      \
  TFJ(ReturnReceiver, kJSArgcReceiverSlots, kReceiver)                         \
                                                                               \
  /* AbstractModuleSource */                                                   \
  CPP(AbstractModuleSourceToStringTag)                                         \
                                                                               \
  /* Array */                                                                  \
  TFC(ArrayConstructor, JSTrampoline)                                          \
  TFC(ArrayConstructorImpl, ArrayConstructor)                                  \
  TFC(ArrayNoArgumentConstructor_PackedSmi_DontOverride,                       \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArrayNoArgumentConstructor_HoleySmi_DontOverride,                        \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArrayNoArgumentConstructor_PackedSmi_DisableAllocationSites,             \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArrayNoArgumentConstructor_HoleySmi_DisableAllocationSites,              \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArrayNoArgumentConstructor_Packed_DisableAllocationSites,                \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArrayNoArgumentConstructor_Holey_DisableAllocationSites,                 \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArrayNoArgumentConstructor_PackedDouble_DisableAllocationSites,          \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArrayNoArgumentConstructor_HoleyDouble_DisableAllocationSites,           \
      ArrayNoArgumentConstructor)                                              \
  TFC(ArraySingleArgumentConstructor_PackedSmi_DontOverride,                   \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArraySingleArgumentConstructor_HoleySmi_DontOverride,                    \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArraySingleArgumentConstructor_PackedSmi_DisableAllocationSites,         \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArraySingleArgumentConstructor_HoleySmi_DisableAllocationSites,          \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArraySingleArgumentConstructor_Packed_DisableAllocationSites,            \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArraySingleArgumentConstructor_Holey_DisableAllocationSites,             \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArraySingleArgumentConstructor_PackedDouble_DisableAllocationSites,      \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArraySingleArgumentConstructor_HoleyDouble_DisableAllocationSites,       \
      ArraySingleArgumentConstructor)                                          \
  TFC(ArrayNArgumentsConstructor, ArrayNArgumentsConstructor)                  \
  CPP(ArrayConcat)                                                             \
  /* ES6 #sec-array.prototype.fill */                                          \
  CPP(ArrayPrototypeFill)                                                      \
  /* ES7 #sec-array.prototype.includes */                                      \
  TFS(ArrayIncludesSmi, NeedsContext::kYes, kElements, kSearchElement,         \
      kLength, kFromIndex)                                                     \
  TFS(ArrayIncludesSmiOrObject, NeedsContext::kYes, kElements, kSearchElement, \
      kLength, kFromIndex)                                                     \
  TFS(ArrayIncludesPackedDoubles, NeedsContext::kYes, kElements,               \
      kSearchElement, kLength, kFromIndex)                                     \
  TFS(ArrayIncludesHoleyDoubles, NeedsContext::kYes, kElements,                \
      kSearchElement, kLength, kFromIndex)                                     \
  TFJ(ArrayIncludes, kDontAdaptArgumentsSentinel)                              \
  /* ES6 #sec-array.prototype.indexof */                                       \
  TFS(ArrayIndexOfSmi, NeedsContext::kYes, kElements, kSearchElement, kLength, \
      kFromIndex)                                                              \
  TFS(ArrayIndexOfSmiOrObject, NeedsContext::kYes, kElements, kSearchElement,  \
      kLength, kFromIndex)                                                     \
  TFS(ArrayIndexOfPackedDoubles, NeedsContext::kYes, kElements,                \
      kSearchElement, kLength, kFromIndex)                                     \
  TFS(ArrayIndexOfHoleyDoubles, NeedsContext::kYes, kElements, kSearchElement, \
      kLength, kFromIndex)                                                     \
  TFJ(ArrayIndexOf, kDontAdaptArgumentsSentinel)                               \
  /* ES6 #sec-array.prototype.pop */                                           \
  CPP(ArrayPop)                                                                \
  TFJ(ArrayPrototypePop, kDontAdaptArgumentsSentinel)                          \
  /* ES6 #sec-array.prototype.push */                                          \
  CPP(ArrayPush)                                                               \
  TFJ(ArrayPrototypePush, kDontAdaptArgumentsSentinel)                         \
  /* ES6 #sec-array.prototype.shift */                                         \
  CPP(ArrayShift)                                                              \
  /* ES6 #sec-array.prototype.unshift */                                       \
  CPP(ArrayUnshift)                                                            \
  /* Support for Array.from and other array-copying idioms */                  \
  TFS(CloneFastJSArray, NeedsContext::kYes, kSource)                           \
  TFS(CloneFastJSArrayFillingHoles, NeedsContext::kYes, kSource)               \
  TFS(ExtractFastJSArray, NeedsContext::kYes, kSource, kBegin, kCount)         \
  TFS(CreateArrayFromSlowBoilerplate, NeedsContext::kYes, kFeedbackVector,     \
      kSlot, kBoilerplateDescriptor, kFlags)                                   \
  TFS(CreateObjectFromSlowBoilerplate, NeedsContext::kYes, kFeedbackVector,    \
      kSlot, kBoilerplateDescriptor, kFlags)                                   \
  TFC(CreateArrayFromSlowBoilerplateHelper, CreateFromSlowBoilerplateHelper)   \
  TFC(CreateObjectFromSlowBoilerplateHelper, CreateFromSlowBoilerplateHelper)  \
  /* ES6 #sec-array.prototype.entries */                                       \
  TFJ(ArrayPrototypeEntries, kJSArgcReceiverSlots, kReceiver)                  \
  /* ES6 #sec-array.prototype.keys */                                          \
  TFJ(ArrayPrototypeKeys, kJSArgcReceiverSlots, kReceiver)                     \
  /* ES6 #sec-array.prototype.values */                                        \
  TFJ(ArrayPrototypeValues, kJSArgcReceiverSlots, kReceiver)                   \
  /* ES6 #sec-%arrayiteratorprototype%.next */                                 \
  TFJ(ArrayIteratorPrototypeNext, kJSArgcReceiverSlots, kReceiver)             \
                                                                               \
  /* ArrayBuffer */                                                            \
  /* ES #sec-arraybuffer-constructor */                                        \
  CPP(ArrayBufferConstructor)                                                  \
  CPP(ArrayBufferConstructor_DoNotInitialize)                                  \
  CPP(ArrayBufferPrototypeSlice)                                               \
  /* https://tc39.es/proposal-resizablearraybuffer/ */                         \
  CPP(ArrayBufferPrototypeResize)                                              \
  /* https://tc39.es/proposal-arraybuffer-transfer/ */                         \
  CPP(ArrayBufferPrototypeTransfer)                                            \
  CPP(ArrayBufferPrototypeTransferToFixedLength)                               \
                                                                               \
  /* AsyncFunction */                                                          \
  TFS(AsyncFunctionEnter, NeedsContext::kYes, kClosure, kReceiver)             \
  TFS(AsyncFunctionReject, NeedsContext::kYes, kAsyncFunctionObject, kReason)  \
  TFS(AsyncFunctionResolve, NeedsContext::kYes, kAsyncFunctionObject, kValue)  \
  TFC(AsyncFunctionLazyDeoptContinuation, AsyncFunctionStackParameter)         \
  TFS(AsyncFunctionAwait, NeedsContext::kYes, kAsyncFunctionObject, kValue)    \
  TFJ(AsyncFunctionAwaitRejectClosure, kJSArgcReceiverSlots + 1, kReceiver,    \
      kSentError)                                                              \
  TFJ(AsyncFunctionAwaitResolveClosure, kJSArgcReceiverSlots + 1, kReceiver,   \
      kSentValue)                                                              \
                                                                               \
  /* BigInt */                                                                 \
  CPP(BigIntConstructor)                                                       \
  CPP(BigIntAsUintN)                                                           \
  CPP(BigIntAsIntN)                                                            \
  CPP(BigIntPrototypeToLocaleString)                                           \
  CPP(BigIntPrototypeToString)                                                 \
  CPP(BigIntPrototypeValueOf)                                                  \
                                                                               \
  /* CallSite */                                                               \
  CPP(CallSitePrototypeGetColumnNumber)                                        \
  CPP(CallSitePrototypeGetEnclosingColumnNumber)                               \
  CPP(CallSitePrototypeGetEnclosingLineNumber)                                 \
  CPP(CallSitePrototypeGetEvalOrigin)                                          \
  CPP(CallSitePrototypeGetFileName)                                            \
  CPP(CallSitePrototypeGetFunction)                                            \
  CPP(CallSitePrototypeGetFunctionName)                                        \
  CPP(CallSitePrototypeGetLineNumber)                                          \
  CPP(CallSitePrototypeGetMethodName)                                          \
  CPP(CallSitePrototypeGetPosition)                                            \
  CPP(CallSitePrototypeGetPromiseIndex)                                        \
  CPP(CallSitePrototypeGetScriptHash)                                          \
  CPP(CallSitePrototypeGetScriptNameOrSourceURL)                               \
  CPP(CallSitePrototypeGetThis)                                                \
  CPP(CallSitePrototypeGetTypeName)                                            \
  CPP(CallSitePrototypeIsAsync)                                                \
  CPP(CallSitePrototypeIsConstructor)                                          \
  CPP(CallSitePrototypeIsEval)                                                 \
  CPP(CallSitePrototypeIsNative)                                               \
  CPP(CallSitePrototypeIsPromiseAll)                                           \
  CPP(CallSitePrototypeIsToplevel)                                             \
  CPP(CallSitePrototypeToString)                                               \
                                                                               \
  /* Console */                                                                \
  CPP(ConsoleDebug)                                                            \
  CPP(ConsoleError)                                                            \
  CPP(ConsoleInfo)                                                             \
  CPP(ConsoleLog)                                                              \
  CPP(ConsoleWarn)                                                             \
  CPP(ConsoleDir)                                                              \
  CPP(ConsoleDirXml)                                                           \
  CPP(ConsoleTable)                                                            \
  CPP(ConsoleTrace)                                                            \
  CPP(ConsoleGroup)                                                            \
  CPP(ConsoleGroupCollapsed)                                                   \
  CPP(ConsoleGroupEnd)                                                         \
  CPP(ConsoleClear)                                                            \
  CPP(ConsoleCount)                                                            \
  CPP(ConsoleCountReset)                                                       \
  CPP(ConsoleAssert)                                                           \
  CPP(ConsoleProfile)                                                          \
  CPP(ConsoleProfileEnd)                                                       \
  CPP(ConsoleTime)                                                             \
  CPP(ConsoleTimeLog)                                                          \
  CPP(ConsoleTimeEnd)                                                          \
  CPP(ConsoleTimeStamp)                                                        \
  CPP(ConsoleContext)                                                          \
                                                                               \
  /* DataView */                                                               \
  /* ES #sec-dataview-constructor */                                           \
  CPP(DataViewConstructor)                                                     \
                                                                               \
  /* Date */                                                                   \
  /* ES #sec-date-constructor */                                               \
  CPP(DateConstructor)                                                         \
  /* ES6 #sec-date.prototype.getdate */                                        \
  TFJ(DatePrototypeGetDate, kJSArgcReceiverSlots, kReceiver)                   \
  /* ES6 #sec-date.prototype.getday */                                         \
  TFJ(DatePrototypeGetDay, kJSArgcReceiverSlots, kReceiver)                    \
  /* ES6 #sec-date.prototype.getfullyear */                                    \
  TFJ(DatePrototypeGetFullYear, kJSArgcReceiverSlots, kReceiver)               \
  /* ES6 #sec-date.prototype.gethours */                                       \
  TFJ(DatePrototypeGetHours, kJSArgcReceiverSlots, kReceiver)                  \
  /* ES6 #sec-date.prototype.getmilliseconds */                                \
  TFJ(DatePrototypeGetMilliseconds, kJSArgcReceiverSlots, kReceiver)           \
  /* ES6 #sec-date.prototype.getminutes */                                     \
  TFJ(DatePrototypeGetMinutes, kJSArgcReceiverSlots, kReceiver)                \
  /* ES6 #sec-date.prototype.getmonth */                                       \
  TFJ(DatePrototypeGetMonth, kJSArgcReceiverSlots, kReceiver)                  \
  /* ES6 #sec-date.prototype.getseconds */                                     \
  TFJ(DatePrototypeGetSeconds, kJSArgcReceiverSlots, kReceiver)                \
  /* ES6 #sec-date.prototype.gettime */                                        \
  TFJ(DatePrototypeGetTime, kJSArgcReceiverSlots, kReceiver)                   \
  /* ES6 #sec-date.prototype.gettimezoneoffset */                              \
  TFJ(DatePrototypeGetTimezoneOffset, kJSArgcReceiverSlots, kReceiver)         \
  /* ES6 #sec-date.prototype.getutcdate */                                     \
  TFJ(DatePrototypeGetUTCDate, kJSArgcReceiverSlots, kReceiver)                \
  /* ES6 #sec-date.prototype.getutcday */                                      \
  TFJ(DatePrototypeGetUTCDay, kJSArgcReceiverSlots, kReceiver)                 \
  /* ES6 #sec-date.prototype.getutcfullyear */                                 \
  TFJ(DatePrototypeGetUTCFullYear, kJSArgcReceiverSlots, kReceiver)            \
  /* ES6 #sec-date.prototype.getutchours */                                    \
  TFJ(DatePrototypeGetUTCHours, kJSArgcReceiverSlots, kReceiver)               \
  /* ES6 #sec-date.prototype.getutcmilliseconds */                             \
  TFJ(DatePrototypeGetUTCMilliseconds, kJSArgcReceiverSlots, kReceiver)        \
  /* ES6 #sec-date.prototype.getutcminutes */                                  \
  TFJ(DatePrototypeGetUTCMinutes, kJSArgcReceiverSlots, kReceiver)             \
  /* ES6 #sec-date.prototype.getutcmonth */                                    \
  TFJ(DatePrototypeGetUTCMonth, kJSArgcReceiverSlots, kReceiver)               \
  /* ES6 #sec-date.prototype.getutcseconds */                                  \
  TFJ(DatePrototypeGetUTCSeconds, kJSArgcReceiverSlots, kReceiver)             \
  /* ES6 #sec-date.prototype.valueof */                                        \
  TFJ(DatePrototypeValueOf, kJSArgcReceiverSlots, kReceiver)                   \
  /* ES6 #sec-date.prototype-@@toprimitive */                                  \
  TFJ(DatePrototypeToPrimitive, kJSArgcReceiverSlots + 1, kReceiver, kHint)    \
  CPP(DatePrototypeGetYear)                                                    \
  CPP(DatePrototypeSetYear)                                                    \
  CPP(DateNow)                                                                 \
  CPP(DateParse)                                                               \
  CPP(DatePrototypeSetDate)                                                    \
  CPP(DatePrototypeSetFullYear)                                                \
  CPP(DatePrototypeSetHours)                                                   \
  CPP(DatePrototypeSetMilliseconds)                                            \
  CPP(DatePrototypeSetMinutes)                                                 \
  CPP(DatePrototypeSetMonth)                                                   \
  CPP(DatePrototypeSetSeconds)                                                 \
  CPP(DatePrototypeSetTime)                                                    \
  CPP(DatePrototypeSetUTCDate)                                                 \
  CPP(DatePrototypeSetUTCFullYear)                                             \
  CPP(DatePrototypeSetUTCHours)                                                \
  CPP(DatePrototypeSetUTCMilliseconds)                                         \
  CPP(DatePrototypeSetUTCMinutes)                                              \
  CPP(DatePrototypeSetUTCMonth)                                                \
  CPP(DatePrototypeSetUTCSeconds)                                              \
  CPP(DatePrototypeToDateString)                                               \
  CPP(DatePrototypeToISOString)                                                \
  CPP(DatePrototypeToUTCString)                                                \
  CPP(DatePrototypeToString)                                                   \
  CPP(DatePrototypeToTimeString)                                               \
  CPP(DatePrototypeToJson)                                                     \
  CPP(DateUTC)                                                                 \
                                                                               \
  /* DisposabeStack*/                                                          \
  CPP(DisposableStackConstructor)                                              \
  CPP(DisposableStackPrototypeUse)                                             \
  CPP(DisposableStackPrototypeDispose)                                         \
  CPP(DisposableStackPrototypeGetDisposed)                                     \
  CPP(DisposableStackPrototypeAdopt)                                           \
  CPP(DisposableStackPrototypeDefer)                                           \
  CPP(DisposableStackPrototypeMove)                                            \
                                                                               \
  /* Async DisposabeStack*/                                                    \
  CPP(AsyncDisposeFromSyncDispose)                                             \
                                                                               \
  /* Error */                                                                  \
  CPP(ErrorConstructor)                                                        \
  CPP(ErrorCaptureStackTrace)                                                  \
  CPP(ErrorPrototypeToString)                                                  \
                                                                               \
  /* Function */                                                               \
  CPP(FunctionConstructor)                                                     \
  ASM(FunctionPrototypeApply, JSTrampoline)                                    \
  CPP(FunctionPrototypeBind)                                                   \
  IF_WASM(CPP, WebAssemblyFunctionPrototypeBind)                               \
  ASM(FunctionPrototypeCall, JSTrampoline)                                     \
  /* ES6 #sec-function.prototype.tostring */                                   \
  CPP(FunctionPrototypeToString)                                               \
                                                                               \
  /* Belongs to Objects but is a dependency of GeneratorPrototypeResume */     \
  TFS(CreateIterResultObject, NeedsContext::kYes, kValue, kDone)               \
                                                                               \
  /* Generator and Async */                                                    \
  TFS(CreateGeneratorObject, NeedsContext::kYes, kClosure, kReceiver)          \
  CPP(GeneratorFunctionConstructor)                                            \
  /* ES6 #sec-generator.prototype.next */                                      \
  TFJ(GeneratorPrototypeNext, kDontAdaptArgumentsSentinel)                     \
  /* ES6 #sec-generator.prototype.return */                                    \
  TFJ(GeneratorPrototypeReturn, kDontAdaptArgumentsSentinel)                   \
  /* ES6 #sec-generator.prototype.throw */                                     \
  TFJ(GeneratorPrototypeThrow, kDontAdaptArgumentsSentinel)                    \
  CPP(AsyncFunctionConstructor)                                                \
  TFC(SuspendGeneratorBaseline, SuspendGeneratorBaseline)                      \
  TFC(ResumeGeneratorBaseline, ResumeGeneratorBaseline)                        \
                                                                               \
  /* Iterator Protocol */                                                      \
  TFC(GetIteratorWithFeedbackLazyDeoptContinuation, GetIteratorStackParameter) \
  TFC(CallIteratorWithFeedbackLazyDeoptContinuation, SingleParameterOnStack)   \
                                                                               \
  /* Global object */                                                          \
  CPP(GlobalDecodeURI)                                                         \
  CPP(GlobalDecodeURIComponent)                                                \
  CPP(GlobalEncodeURI)                                                         \
  CPP(GlobalEncodeURIComponent)                                                \
  CPP(GlobalEscape)                                                            \
  CPP(GlobalUnescape)                                                          \
  CPP(GlobalEval)                                                              \
  /* ES6 #sec-isfinite-number */                                               \
  TFJ(GlobalIsFinite, kJSArgcReceiverSlots + 1, kReceiver, kNumber)            \
  /* ES6 #sec-isnan-number */                                                  \
  TFJ(GlobalIsNaN, kJSArgcReceiverSlots + 1, kReceiver, kNumber)               \
                                                                               \
  /* JSON */                                                                   \
  CPP(JsonParse)                                                               \
  CPP(JsonStringify)                                                           \
  CPP(JsonRawJson)                                                             \
  CPP(JsonIsRawJson)                                                           \
                                                                               \
  /* ICs */                                                                    \
  TFH(LoadIC, LoadWithVector)                                                  \
  TFH(LoadIC_Megamorphic, LoadWithVector)                                      \
  TFH(LoadIC_Noninlined, LoadWithVector)                                       \
  TFH(LoadICTrampoline, Load)                                                  \
  TFH(LoadICBaseline, LoadBaseline)                                            \
  TFH(LoadICTrampoline_Megamorphic, Load)                                      \
  TFH(LoadSuperIC, LoadWithReceiverAndVector)                                  \
  TFH(LoadSuperICBaseline, LoadWithReceiverBaseline)                           \
  TFH(KeyedLoadIC, KeyedLoadWithVector)                                        \
  TFH(EnumeratedKeyedLoadIC, EnumeratedKeyedLoad)                              \
  TFH(KeyedLoadIC_Megamorphic, KeyedLoadWithVector)                            \
  TFH(KeyedLoadICTrampoline, KeyedLoad)                                        \
  TFH(KeyedLoadICBaseline, KeyedLoadBaseline)                                  \
  TFH(EnumeratedKeyedLoadICBaseline, EnumeratedKeyedLoadBaseline)              \
  TFH(KeyedLoadICTrampoline_Megamorphic, KeyedLoad)                            \
  TFH(StoreGlobalIC, StoreGlobalWithVector)                                    \
  TFH(StoreGlobalICTrampoline, StoreGlobal)                                    \
  TFH(StoreGlobalICBaseline, StoreGlobalBaseline)                              \
  TFH(StoreIC, StoreWithVector)                                                \
  TFH(StoreIC_Megamorphic, StoreWithVector)                                    \
  TFH(StoreICTrampoline, Store)                                                \
  TFH(StoreICTrampoline_Megamorphic, Store)                                    \
  TFH(StoreICBaseline, StoreBaseline)                                          \
  TFH(DefineNamedOwnIC, StoreWithVector)                                       \
  TFH(DefineNamedOwnICTrampoline, Store)                                       \
  TFH(DefineNamedOwnICBaseline, StoreBaseline)                                 \
  TFH(KeyedStoreIC, StoreWithVector)                                           \
  TFH(KeyedStoreICTrampoline, Store)                                           \
  TFH(KeyedStoreICTrampoline_Megamorphic, Store)                               \
  TFH(KeyedStoreICBaseline, StoreBaseline)                                     \
  TFH(DefineKeyedOwnIC, DefineKeyedOwnWithVector)                              \
  TFH(DefineKeyedOwnICTrampoline, DefineKeyedOwn)                              \
  TFH(DefineKeyedOwnICBaseline, DefineKeyedOwnBaseline)                        \
  TFH(StoreInArrayLiteralIC, StoreWithVector)                                  \
  TFH(StoreInArrayLiteralICBaseline, StoreBaseline)                            \
  TFH(LookupContextTrampoline, LookupTrampoline)                               \
  TFH(LookupContextBaseline, LookupBaseline)                                   \
  TFH(LookupContextInsideTypeofTrampoline, LookupTrampoline)                   \
  TFH(LookupContextInsideTypeofBaseline, LookupBaseline)                       \
  TFH(LoadGlobalIC, LoadGlobalWithVector)                                      \
  TFH(LoadGlobalICInsideTypeof, LoadGlobalWithVector)                          \
  TFH(LoadGlobalICTrampoline, LoadGlobal)                                      \
  TFH(LoadGlobalICBaseline, LoadGlobalBaseline)                                \
  TFH(LoadGlobalICInsideTypeofTrampoline, LoadGlobal)                          \
  TFH(LoadGlobalICInsideTypeofBaseline, LoadGlobalBaseline)                    \
  TFH(LookupGlobalIC, LookupWithVector)                                        \
  TFH(LookupGlobalICTrampoline, LookupTrampoline)                              \
  TFH(LookupGlobalICBaseline, LookupBaseline)                                  \
  TFH(LookupGlobalICInsideTypeof, LookupWithVector)                            \
  TFH(LookupGlobalICInsideTypeofTrampoline, LookupTrampoline)                  \
  TFH(LookupGlobalICInsideTypeofBaseline, LookupBaseline)                      \
  TFH(CloneObjectIC, CloneObjectWithVector)                                    \
  TFH(CloneObjectICBaseline, CloneObjectBaseline)                              \
  TFH(CloneObjectIC_Slow, CloneObjectWithVector)                               \
  TFH(KeyedHasIC, KeyedHasICWithVector)                                        \
  TFH(KeyedHasICBaseline, KeyedHasICBaseline)                                  \
  TFH(KeyedHasIC_Megamorphic, KeyedHasICWithVector)                            \
                                                                               \
  /* IterableToList */                                                         \
  /* ES #sec-iterabletolist */                                                 \
  TFS(IterableToList, NeedsContext::kYes, kIterable, kIteratorFn)              \
  TFS(IterableToFixedArray, NeedsContext::kYes, kIterable, kIteratorFn)        \
  TFS(IterableToListWithSymbolLookup, NeedsContext::kYes, kIterable)           \
  TFS(IterableToFixedArrayWithSymbolLookupSlow, NeedsContext::kYes, kIterable) \
  TFS(IterableToListMayPreserveHoles, NeedsContext::kYes, kIterable,           \
      kIteratorFn)                                                             \
  TFS(IterableToListConvertHoles, NeedsContext::kYes, kIterable, kIteratorFn)  \
  IF_WASM(TFS, IterableToFixedArrayForWasm, NeedsContext::kYes, kIterable,     \
          kExpectedLength)                                                     \
                                                                               \
  /* #sec-createstringlistfromiterable */                                      \
  TFS(StringListFromIterable, NeedsContext::kYes, kIterable)                   \
                                                                               \
  /* Map */                                                                    \
  TFS(FindOrderedHashMapEntry, NeedsContext::kYes, kTable, kKey)               \
  TFJ(MapConstructor, kDontAdaptArgumentsSentinel)                             \
  TFJ(MapPrototypeSet, kJSArgcReceiverSlots + 2, kReceiver, kKey, kValue)      \
  TFJ(MapPrototypeDelete, kJSArgcReceiverSlots + 1, kReceiver, kKey)           \
  TFJ(MapPrototypeGet, kJSArgcReceiverSlots + 1, kReceiver, kKey)              \
  TFJ(MapPrototypeHas, kJSArgcReceiverSlots + 1, kReceiver, kKey)              \
  CPP(MapPrototypeClear)                                                       \
  /* ES #sec-map.prototype.entries */                                          \
  TFJ(MapPrototypeEntries, kJSArgcReceiverSlots, kReceiver)                    \
  /* ES #sec-get-map.prototype.size */                                         \
  TFJ(MapPrototypeGetSize, kJSArgcReceiverSlots, kReceiver)                    \
  /* ES #sec-map.prototype.forEach */                                          \
  TFJ(MapPrototypeForEach, kDontAdaptArgumentsSentinel)                        \
  /* ES #sec-map.prototype.keys */                                             \
  TFJ(MapPrototypeKeys, kJSArgcReceiverSlots, kReceiver)                       \
  /* ES #sec-map.prototype.values */                                           \
  TFJ(MapPrototypeValues, kJSArgcReceiverSlots, kReceiver)                     \
  /* ES #sec-%mapiteratorprototype%.next */                                    \
  TFJ(MapIteratorPrototypeNext, kJSArgcReceiverSlots, kReceiver)               \
  TFS(MapIteratorToList, NeedsContext::kYes, kSource)                          \
                                                                               \
  /* ES #sec-number-constructor */                                             \
  CPP(NumberPrototypeToExponential)                                            \
  CPP(NumberPrototypeToFixed)                                                  \
  CPP(NumberPrototypeToLocaleString)                                           \
  CPP(NumberPrototypeToPrecision)                                              \
  TFC(SameValue, CompareNoContext)                                             \
  TFC(SameValueNumbersOnly, CompareNoContext)                                  \
                                                                               \
  /* Binary ops with feedback collection */                                    \
  TFC(Add_Baseline, BinaryOp_Baseline)                                         \
  TFC(AddSmi_Baseline, BinarySmiOp_Baseline)                                   \
  TFC(Subtract_Baseline, BinaryOp_Baseline)                                    \
  TFC(SubtractSmi_Baseline, BinarySmiOp_Baseline)                              \
  TFC(Multiply_Baseline, BinaryOp_Baseline)                                    \
  TFC(MultiplySmi_Baseline, BinarySmiOp_Baseline)                              \
  TFC(Divide_Baseline, BinaryOp_Baseline)                                      \
  TFC(DivideSmi_Baseline, BinarySmiOp_Baseline)                                \
  TFC(Modulus_Baseline, BinaryOp_Baseline)                                     \
  TFC(ModulusSmi_Baseline, BinarySmiOp_Baseline)                               \
  TFC(Exponentiate_Baseline, BinaryOp_Baseline)                                \
  TFC(ExponentiateSmi_Baseline, BinarySmiOp_Baseline)                          \
  TFC(BitwiseAnd_Baseline, BinaryOp_Baseline)                                  \
  TFC(BitwiseAndSmi_Baseline, BinarySmiOp_Baseline)                            \
  TFC(BitwiseOr_Baseline, BinaryOp_Baseline)                                   \
  TFC(BitwiseOrSmi_Baseline, BinarySmiOp_Baseline)                             \
  TFC(BitwiseXor_Baseline, BinaryOp_Baseline)                                  \
  TFC(BitwiseXorSmi_Baseline, BinarySmiOp_Baseline)                            \
  TFC(ShiftLeft_Baseline, BinaryOp_Baseline)                                   \
  TFC(ShiftLeftSmi_Baseline, BinarySmiOp_Baseline)                             \
  TFC(ShiftRight_Baseline, BinaryOp_Baseline)                                  \
  TFC(ShiftRightSmi_Baseline, BinarySmiOp_Baseline)                            \
  TFC(ShiftRightLogical_Baseline, BinaryOp_Baseline)                           \
  TFC(ShiftRightLogicalSmi_Baseline, BinarySmiOp_Baseline)                     \
                                                                               \
  TFC(Add_WithFeedback, BinaryOp_WithFeedback)                                 \
  TFC(Subtract_WithFeedback, BinaryOp_WithFeedback)                            \
  TFC(Multiply_WithFeedback, BinaryOp_WithFeedback)                            \
  TFC(Divide_WithFeedback, BinaryOp_WithFeedback)                              \
  TFC(Modulus_WithFeedback, BinaryOp_WithFeedback)                             \
  TFC(Exponentiate_WithFeedback, BinaryOp_WithFeedback)                        \
  TFC(BitwiseAnd_WithFeedback, BinaryOp_WithFeedback)                          \
  TFC(BitwiseOr_WithFeedback, BinaryOp_WithFeedback)                           \
  TFC(BitwiseXor_WithFeedback, BinaryOp_WithFeedback)                          \
  TFC(ShiftLeft_WithFeedback, BinaryOp_WithFeedback)                           \
  TFC(ShiftRight_WithFeedback, BinaryOp_WithFeedback)                          \
  TFC(ShiftRightLogical_WithFeedback, BinaryOp_WithFeedback)                   \
                                                                               \
  /* Compare ops with feedback collection */                                   \
  TFC(Equal_Baseline, Compare_Baseline)                                        \
  TFC(StrictEqual_Baseline, Compare_Baseline)                                  \
  TFC(LessThan_Baseline, Compare_Baseline)                                     \
  TFC(GreaterThan_Baseline, Compare_Baseline)                                  \
  TFC(LessThanOrEqual_Baseline, Compare_Baseline)                              \
  TFC(GreaterThanOrEqual_Baseline, Compare_Baseline)                           \
                                                                               \
  TFC(Equal_WithFeedback, Compare_WithFeedback)                                \
  TFC(StrictEqual_WithFeedback, Compare_WithFeedback)                          \
  TFC(LessThan_WithFeedback, Compare_WithFeedback)                             \
  TFC(GreaterThan_WithFeedback, Compare_WithFeedback)                          \
  TFC(LessThanOrEqual_WithFeedback, Compare_WithFeedback)                      \
  TFC(GreaterThanOrEqual_WithFeedback, Compare_WithFeedback)                   \
                                                                               \
  /* Unary ops with feedback collection */                                     \
  TFC(BitwiseNot_Baseline, UnaryOp_Baseline)                                   \
  TFC(Decrement_Baseline, UnaryOp_Baseline)                                    \
  TFC(Increment_Baseline, UnaryOp_Baseline)                                    \
  TFC(Negate_Baseline, UnaryOp_Baseline)                                       \
  TFC(BitwiseNot_WithFeedback, UnaryOp_WithFeedback)                           \
  TFC(Decrement_WithFeedback, UnaryOp_WithFeedback)                            \
  TFC(Increment_WithFeedback, UnaryOp_WithFeedback)                            \
  TFC(Negate_WithFeedback, UnaryOp_WithFeedback)                               \
                                                                               \
  /* Object */                                                                 \
  /* ES #sec-object-constructor */                                             \
  TFJ(ObjectAssign, kDontAdaptArgumentsSentinel)                               \
  /* ES #sec-object.create */                                                  \
  TFJ(ObjectCreate, kDontAdaptArgumentsSentinel)                               \
  CPP(ObjectDefineGetter)                                                      \
  CPP(ObjectDefineProperties)                                                  \
  CPP(ObjectDefineProperty)                                                    \
  CPP(ObjectDefineSetter)                                                      \
  TFJ(ObjectEntries, kJSArgcReceiverSlots + 1, kReceiver, kObject)             \
  CPP(ObjectFreeze)                                                            \
  TFJ(ObjectGetOwnPropertyDescriptor, kDontAdaptArgumentsSentinel)             \
  CPP(ObjectGetOwnPropertyDescriptors)                                         \
  TFJ(ObjectGetOwnPropertyNames, kJSArgcReceiverSlots + 1, kReceiver, kObject) \
  CPP(ObjectGetOwnPropertySymbols)                                             \
  TFJ(ObjectHasOwn, kJSArgcReceiverSlots + 2, kReceiver, kObject, kKey)        \
  TFJ(ObjectIs, kJSArgcReceiverSlots + 2, kReceiver, kLeft, kRight)            \
  CPP(ObjectIsFrozen)                                                          \
  CPP(ObjectIsSealed)                                                          \
  TFJ(ObjectKeys, kJSArgcReceiverSlots + 1, kReceiver, kObject)                \
  CPP(ObjectLookupGetter)                                                      \
  CPP(ObjectLookupSetter)                                                      \
  /* ES6 #sec-object.prototype.hasownproperty */                               \
  TFJ(ObjectPrototypeHasOwnProperty, kJSArgcReceiverSlots + 1, kReceiver,      \
      kKey)                                                                    \
  TFJ(ObjectPrototypeIsPrototypeOf, kJSArgcReceiverSlots + 1, kReceiver,       \
      kValue)                                                                  \
  CPP(ObjectPrototypePropertyIsEnumerable)                                     \
  CPP(ObjectPrototypeGetProto)                                                 \
  CPP(ObjectPrototypeSetProto)                                                 \
  CPP(ObjectSeal)                                                              \
  TFS(ObjectToString, NeedsContext::kYes, kReceiver)                           \
  TFJ(ObjectValues, kJSArgcReceiverSlots + 1, kReceiver, kObject)              \
                                                                               \
  /* instanceof */                                                             \
  TFC(OrdinaryHasInstance, Compare)                                            \
  TFC(InstanceOf, Compare)                                                     \
  TFC(InstanceOf_WithFeedback, Compare_WithFeedback)                           \
  TFC(InstanceOf_Baseline, Compare_Baseline)                                   \
                                                                               \
  /* for-in */                                                                 \
  TFS(ForInEnumerate, NeedsContext::kYes, kReceiver)                           \
  TFC(ForInPrepare, ForInPrepare)                                              \
  TFS(ForInFilter, NeedsContext::kYes, kKey, kObject)                          \
                                                                               \
  /* Reflect */                                                                \
  ASM(ReflectApply, JSTrampoline)                                              \
  ASM(ReflectConstruct, JSTrampoline)                                          \
  CPP(ReflectDefineProperty)                                                   \
  CPP(ReflectOwnKeys)                                                          \
  CPP(ReflectSet)                                                              \
                                                                               \
  /* RegExp */                                                                 \
  CPP(RegExpCapture1Getter)                                                    \
  CPP(RegExpCapture2Getter)                                                    \
  CPP(RegExpCapture3Getter)                                                    \
  CPP(RegExpCapture4Getter)                                                    \
  CPP(RegExpCapture5Getter)                                                    \
  CPP(RegExpCapture6Getter)                                                    \
  CPP(RegExpCapture7Getter)                                                    \
  CPP(RegExpCapture8Getter)                                                    \
  CPP(RegExpCapture9Getter)                                                    \
  /* ES #sec-regexp-pattern-flags */                                           \
  TFJ(RegExpConstructor, kJSArgcReceiverSlots + 2, kReceiver, kPattern,        \
      kFlags)                                                                  \
  CPP(RegExpInputGetter)                                                       \
  CPP(RegExpInputSetter)                                                       \
  CPP(RegExpLastMatchGetter)                                                   \
  CPP(RegExpLastParenGetter)                                                   \
  CPP(RegExpLeftContextGetter)                                                 \
  /* ES #sec-regexp.prototype.compile */                                       \
  TFJ(RegExpPrototypeCompile, kJSArgcReceiverSlots + 2, kReceiver, kPattern,   \
      kFlags)                                                                  \
  CPP(RegExpPrototypeToString)                                                 \
  CPP(RegExpRightContextGetter)                                                \
                                                                               \
  /* RegExp helpers */                                                         \
  TFS(RegExpExecAtom, NeedsContext::kYes, kRegExp, kString, kLastIndex,        \
      kMatchInfo)                                                              \
  TFS(RegExpExecInternal, NeedsContext::kYes, kRegExp, kString, kLastIndex,    \
      kMatchInfo)                                                              \
  ASM(RegExpInterpreterTrampoline, RegExpTrampoline)                           \
  ASM(RegExpExperimentalTrampoline, RegExpTrampoline)                          \
                                                                               \
  /* Set */                                                                    \
  TFS(FindOrderedHashSetEntry, NeedsContext::kYes, kTable, kKey)               \
  TFJ(SetConstructor, kDontAdaptArgumentsSentinel)                             \
  TFJ(SetPrototypeHas, kJSArgcReceiverSlots + 1, kReceiver, kKey)              \
  TFJ(SetPrototypeAdd, kJSArgcReceiverSlots + 1, kReceiver, kKey)              \
  TFJ(SetPrototypeDelete, kJSArgcReceiverSlots + 1, kReceiver, kKey)           \
  CPP(SetPrototypeClear)                                                       \
  /* ES #sec-set.prototype.entries */                                          \
  TFJ(SetPrototypeEntries, kJSArgcReceiverSlots, kReceiver)                    \
  /* ES #sec-get-set.prototype.size */                                         \
  TFJ(SetPrototypeGetSize, kJSArgcReceiverSlots, kReceiver)                    \
  /* ES #sec-set.prototype.foreach */                                          \
  TFJ(SetPrototypeForEach, kDontAdaptArgumentsSentinel)                        \
  /* ES #sec-set.prototype.values */                                           \
  TFJ(SetPrototypeValues, kJSArgcReceiverSlots, kReceiver)                     \
  /* ES #sec-%setiteratorprototype%.next */                                    \
  TFJ(SetIteratorPrototypeNext, kJSArgcReceiverSlots, kReceiver)               \
  TFS(SetOrSetIteratorToList, NeedsContext::kYes, kSource)                     \
                                                                               \
  /* ShadowRealm */                                                            \
  CPP(ShadowRealmConstructor)                                                  \
  TFS(ShadowRealmGetWrappedValue, NeedsContext::kYes, kCreationContext,        \
      kTargetContext, kValue)                                                  \
  CPP(ShadowRealmPrototypeEvaluate)                                            \
  TFJ(ShadowRealmPrototypeImportValue, kJSArgcReceiverSlots + 2, kReceiver,    \
      kSpecifier, kExportName)                                                 \
  TFJ(ShadowRealmImportValueFulfilled, kJSArgcReceiverSlots + 1, kReceiver,    \
      kExports)                                                                \
  TFJ(ShadowRealmImportValueRejected, kJSArgcReceiverSlots + 1, kReceiver,     \
      kException)                                                              \
                                                                               \
  /* SharedArrayBuffer */                                                      \
  CPP(SharedArrayBufferPrototypeGetByteLength)                                 \
  CPP(SharedArrayBufferPrototypeSlice)                                         \
  /* https://tc39.es/proposal-resizablearraybuffer/ */                         \
  CPP(SharedArrayBufferPrototypeGrow)                                          \
                                                                               \
  TFJ(AtomicsLoad, kJSArgcReceiverSlots + 2, kReceiver, kArrayOrSharedObject,  \
      kIndexOrFieldName)                                                       \
  TFJ(AtomicsStore, kJSArgcReceiverSlots + 3, kReceiver, kArrayOrSharedObject, \
      kIndexOrFieldName, kValue)                                               \
  TFJ(AtomicsExchange, kJSArgcReceiverSlots + 3, kReceiver,                    \
      kArrayOrSharedObject, kIndexOrFieldName, kValue)                         \
  TFJ(AtomicsCompareExchange, kJSArgcReceiverSlots + 4, kReceiver,             \
      kArrayOrSharedObject, kIndexOrFieldName, kOldValue, kNewValue)           \
  TFJ(AtomicsAdd, kJSArgcReceiverSlots + 3, kReceiver, kArray, kIndex, kValue) \
  TFJ(AtomicsSub, kJSArgcReceiverSlots + 3, kReceiver, kArray, kIndex, kValue) \
  TFJ(AtomicsAnd, kJSArgcReceiverSlots + 3, kReceiver, kArray, kIndex, kValue) \
  TFJ(AtomicsOr, kJSArgcReceiverSlots + 3, kReceiver, kArray, kIndex, kValue)  \
  TFJ(AtomicsXor, kJSArgcReceiverSlots + 3, kReceiver, kArray, kIndex, kValue) \
  CPP(AtomicsNotify)                                                           \
  CPP(AtomicsIsLockFree)                                                       \
  CPP(AtomicsWait)                                                             \
  CPP(AtomicsWaitAsync)                                                        \
  CPP(AtomicsPause)                                                            \
                                                                               \
  /* String */                                                                 \
  /* ES #sec-string.fromcodepoint */                                           \
  CPP(StringFromCodePoint)                                                     \
  /* ES6 #sec-string.fromcharcode */                                           \
  IF_TSA(TSJ, TFJ)(StringFromCharCode, kDontAdaptArgumentsSentinel)            \
  /* ES6 #sec-string.prototype.lastindexof */                                  \
  CPP(StringPrototypeLastIndexOf)                                              \
  /* ES #sec-string.prototype.matchAll */                                      \
  TFJ(StringPrototypeMatchAll, kJSArgcReceiverSlots + 1, kReceiver, kRegexp)   \
  /* ES6 #sec-string.prototype.localecompare */                                \
  CPP(StringPrototypeLocaleCompare)                                            \
  /* ES6 #sec-string.prototype.replace */                                      \
  TFJ(StringPrototypeReplace, kJSArgcReceiverSlots + 2, kReceiver, kSearch,    \
      kReplace)                                                                \
  /* ES6 #sec-string.prototype.split */                                        \
  TFJ(StringPrototypeSplit, kDontAdaptArgumentsSentinel)                       \
  /* ES6 #sec-string.raw */                                                    \
  CPP(StringRaw)                                                               \
                                                                               \
  /* Symbol */                                                                 \
  /* ES #sec-symbol-constructor */                                             \
  CPP(SymbolConstructor)                                                       \
  /* ES6 #sec-symbol.for */                                                    \
  CPP(SymbolFor)                                                               \
  /* ES6 #sec-symbol.keyfor */                                                 \
  CPP(SymbolKeyFor)                                                            \
                                                                               \
  /* TypedArray */                                                             \
  /* ES #sec-typedarray-constructors */                                        \
  TFJ(TypedArrayBaseConstructor, kJSArgcReceiverSlots, kReceiver)              \
  TFJ(TypedArrayConstructor, kDontAdaptArgumentsSentinel)                      \
  CPP(TypedArrayPrototypeBuffer)                                               \
  /* ES6 #sec-get-%typedarray%.prototype.bytelength */                         \
  TFJ(TypedArrayPrototypeByteLength, kJSArgcReceiverSlots, kReceiver)          \
  /* ES6 #sec-get-%typedarray%.prototype.byteoffset */                         \
  TFJ(TypedArrayPrototypeByteOffset, kJSArgcReceiverSlots, kReceiver)          \
  /* ES6 #sec-get-%typedarray%.prototype.length */                             \
  TFJ(TypedArrayPrototypeLength, kJSArgcReceiverSlots, kReceiver)              \
  /* ES6 #sec-%typedarray%.prototype.copywithin */                             \
  CPP(TypedArrayPrototypeCopyWithin)                                           \
  /* ES6 #sec-%typedarray%.prototype.fill */                                   \
  CPP(TypedArrayPrototypeFill)                                                 \
  /* ES7 #sec-%typedarray%.prototype.includes */                               \
  CPP(TypedArrayPrototypeIncludes)                                             \
  /* ES6 #sec-%typedarray%.prototype.indexof */                                \
  CPP(TypedArrayPrototypeIndexOf)                                              \
  /* ES6 #sec-%typedarray%.prototype.lastindexof */                            \
  CPP(TypedArrayPrototypeLastIndexOf)                                          \
  /* ES6 #sec-%typedarray%.prototype.reverse */                                \
  CPP(TypedArrayPrototypeReverse)                                              \
  /* ES6 #sec-get-%typedarray%.prototype-@@tostringtag */                      \
  TFJ(TypedArrayPrototypeToStringTag, kJSArgcReceiverSlots, kReceiver)         \
  /* ES6 %TypedArray%.prototype.map */                                         \
  TFJ(TypedArrayPrototypeMap, kDontAdaptArgumentsSentinel)                     \
                                                                               \
  /* Wasm */                                                                   \
  IF_WASM_DRUMBRAKE(ASM, WasmInterpreterEntry, WasmDummy)                      \
  IF_WASM_DRUMBRAKE(ASM, GenericJSToWasmInterpreterWrapper, WasmDummy)         \
  IF_WASM_DRUMBRAKE(ASM, WasmInterpreterCWasmEntry, WasmDummy)                 \
  IF_WASM_DRUMBRAKE(ASM, GenericWasmToJSInterpreterWrapper, WasmDummy)         \
                                                                               \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem32S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem32U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_F32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_F64LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem32S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem32U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem32S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem32U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_F32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_F64LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8S, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8U, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32S, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32U, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadMem, WasmDummy)              \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadMem, WasmDummy)              \
                                                                               \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8S_LocalSet, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8U_LocalSet, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16S_LocalSet, WasmDummy)  \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16U_LocalSet, WasmDummy)  \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8S_LocalSet, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8U_LocalSet, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16S_LocalSet, WasmDummy)  \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16U_LocalSet, WasmDummy)  \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32S_LocalSet, WasmDummy)  \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32U_LocalSet, WasmDummy)  \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem_LocalSet, WasmDummy)     \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem_LocalSet, WasmDummy)     \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadMem_LocalSet, WasmDummy)     \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadMem_LocalSet, WasmDummy)     \
                                                                               \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem8, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem16, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem8, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem16, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem32, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem, WasmDummy)             \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem, WasmDummy)             \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32StoreMem, WasmDummy)             \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64StoreMem, WasmDummy)             \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem8, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem16, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem8, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem16, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem32, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem, WasmDummy)             \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem, WasmDummy)             \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32StoreMem, WasmDummy)             \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64StoreMem, WasmDummy)             \
                                                                               \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadStoreMem, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadStoreMem, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32LoadStoreMem, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64LoadStoreMem, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadStoreMem, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadStoreMem, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadStoreMem, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadStoreMem, WasmDummy)         \
                                                                               \
  IF_WASM(ASM, JSToWasmWrapperAsm, WasmJSToWasmWrapper)                        \
  IF_WASM(ASM, WasmReturnPromiseOnSuspendAsm, WasmJSToWasmWrapper)             \
  IF_WASM(ASM, WasmToJsWrapperAsm, WasmDummy)                                  \
  IF_WASM(TFC, WasmToJsWrapperCSA, WasmToJSWrapper)                            \
  IF_WASM(TFC, WasmToJsWrapperInvalidSig, WasmToJSWrapper)                     \
  IF_WASM(ASM, WasmSuspend, WasmSuspend)                                       \
  IF_WASM(ASM, WasmResume, WasmDummyWithJSLinkage)                             \
  IF_WASM(ASM, WasmReject, WasmDummyWithJSLinkage)                             \
  IF_WASM(ASM, WasmTrapHandlerLandingPad, WasmDummy)                           \
  IF_WASM(ASM, WasmCompileLazy, WasmDummy)                                     \
  IF_WASM(ASM, WasmLiftoffFrameSetup, WasmDummy)                               \
  IF_WASM(ASM, WasmDebugBreak, WasmDummy)                                      \
  IF_WASM(ASM, WasmOnStackReplace, WasmDummy)                                  \
  IF_WASM(TFC, WasmFloat32ToNumber, WasmFloat32ToNumber)                       \
  IF_WASM(TFC, WasmFloat64ToNumber, WasmFloat64ToTagged)                       \
  IF_WASM(TFC, WasmFloat64ToString, WasmFloat64ToTagged)                       \
  IF_WASM(TFC, JSToWasmLazyDeoptContinuation, SingleParameterOnStack)          \
  IF_WASM(ASM, WasmToOnHeapWasmToJsTrampoline, WasmDummy)                      \
                                                                               \
  /* WeakMap */                                                                \
  TFJ(WeakMapConstructor, kDontAdaptArgumentsSentinel)                         \
  TFS(WeakMapLookupHashIndex, NeedsContext::kYes, kTable, kKey)                \
  TFJ(WeakMapGet, kJSArgcReceiverSlots + 1, kReceiver, kKey)                   \
  TFJ(WeakMapPrototypeHas, kJSArgcReceiverSlots + 1, kReceiver, kKey)          \
  TFJ(WeakMapPrototypeSet, kJSArgcReceiverSlots + 2, kReceiver, kKey, kValue)  \
  TFJ(WeakMapPrototypeDelete, kJSArgcReceiverSlots + 1, kReceiver, kKey)       \
                                                                               \
  /* WeakSet */                                                                \
  TFJ(WeakSetConstructor, kDontAdaptArgumentsSentinel)                         \
  TFJ(WeakSetPrototypeHas, kJSArgcReceiverSlots + 1, kReceiver, kKey)          \
  TFJ(WeakSetPrototypeAdd, kJSArgcReceiverSlots + 1, kReceiver, kValue)        \
  TFJ(WeakSetPrototypeDelete, kJSArgcReceiverSlots + 1, kReceiver, kValue)     \
                                                                               \
  /* WeakSet / WeakMap Helpers */                                              \
  TFS(WeakCollectionDelete, NeedsContext::kYes, kCollection, kKey)             \
  TFS(WeakCollectionSet, NeedsContext::kYes, kCollection, kKey, kValue)        \
                                                                               \
  /* JS Structs and friends */                                                 \
  CPP(SharedSpaceJSObjectHasInstance)                                          \
  CPP(SharedStructTypeConstructor)                                             \
  CPP(SharedStructTypeIsSharedStruct)                                          \
  CPP(SharedStructConstructor)                                                 \
  CPP(SharedArrayConstructor)                                                  \
  CPP(SharedArrayIsSharedArray)                                                \
  CPP(AtomicsMutexConstructor)                                                 \
  CPP(AtomicsMutexIsMutex)                                                     \
  CPP(AtomicsMutexLock)                                                        \
  CPP(AtomicsMutexLockAsync)                                                   \
  CPP(AtomicsMutexLockWithTimeout)                                             \
  CPP(AtomicsMutexTryLock)                                                     \
  CPP(AtomicsMutexAsyncUnlockResolveHandler)                                   \
  CPP(AtomicsMutexAsyncUnlockRejectHandler)                                    \
  CPP(AtomicsConditionConstructor)                                             \
  CPP(AtomicsConditionAcquireLock)                                             \
  CPP(AtomicsConditionIsCondition)                                             \
  CPP(AtomicsConditionWait)                                                    \
  CPP(AtomicsConditionNotify)                                                  \
  CPP(AtomicsConditionWaitAsync)                                               \
                                                                               \
  /* AsyncGenerator */                                                         \
                                                                               \
  TFS(AsyncGeneratorResolve, NeedsContext::kYes, kGenerator, kValue, kDone)    \
  TFS(AsyncGeneratorReject, NeedsContext::kYes, kGenerator, kValue)            \
  TFS(AsyncGeneratorYieldWithAwait, NeedsContext::kYes, kGenerator, kValue)    \
  TFS(AsyncGeneratorReturn, NeedsContext::kYes, kGenerator, kValue)            \
  TFS(AsyncGeneratorResumeNext, NeedsContext::kYes, kGenerator)                \
                                                                               \
  /* AsyncGeneratorFunction( p1, p2, ... pn, body ) */                         \
  /* proposal-async-iteration/#sec-asyncgeneratorfunction-constructor */       \
  CPP(AsyncGeneratorFunctionConstructor)                                       \
  /* AsyncGenerator.prototype.next ( value ) */                                \
  /* proposal-async-iteration/#sec-asyncgenerator-prototype-next */            \
  TFJ(AsyncGeneratorPrototypeNext, kDontAdaptArgumentsSentinel)                \
  /* AsyncGenerator.prototype.return ( value ) */                              \
  /* proposal-async-iteration/#sec-asyncgenerator-prototype-return */          \
  TFJ(AsyncGeneratorPrototypeReturn, kDontAdaptArgumentsSentinel)              \
  /* AsyncGenerator.prototype.throw ( exception ) */                           \
  /* proposal-async-iteration/#sec-asyncgenerator-prototype-throw */           \
  TFJ(AsyncGeneratorPrototypeThrow, kDontAdaptArgumentsSentinel)               \
                                                                               \
  /* Await (proposal-async-iteration/#await), with resume behaviour */         \
  /* specific to Async Generators. Internal / Not exposed to JS code. */       \
  TFS(AsyncGeneratorAwait, NeedsContext::kYes, kAsyncGeneratorObject, kValue)  \
  TFJ(AsyncGeneratorAwaitResolveClosure, kJSArgcReceiverSlots + 1, kReceiver,  \
      kValue)                                                                  \
  TFJ(AsyncGeneratorAwaitRejectClosure, kJSArgcReceiverSlots + 1, kReceiver,   \
      kValue)                                                                  \
  TFJ(AsyncGeneratorYieldWithAwaitResolveClosure, kJSArgcReceiverSlots + 1,    \
      kReceiver, kValue)                                                       \
  TFJ(AsyncGeneratorReturnClosedResolveClosure, kJSArgcReceiverSlots + 1,      \
      kReceiver, kValue)                                                       \
  TFJ(AsyncGeneratorReturnClosedRejectClosure, kJSArgcReceiverSlots + 1,       \
      kReceiver, kValue)                                                       \
  TFJ(AsyncGeneratorReturnResolveClosure, kJSArgcReceiverSlots + 1, kReceiver, \
      kValue)                                                                  \
                                                                               \
  /* Async-from-Sync Iterator */                                               \
                                                                               \
  /* %AsyncFromSyncIteratorPrototype% */                                       \
  /* See tc39.github.io/proposal-async-iteration/ */                           \
  /* #sec-%asyncfromsynciteratorprototype%-object) */                          \
  TFJ(AsyncFromSyncIteratorPrototypeNext, kDontAdaptArgumentsSentinel)         \
  /* #sec-%asyncfromsynciteratorprototype%.throw */                            \
  TFJ(AsyncFromSyncIteratorPrototypeThrow, kDontAdaptArgumentsSentinel)        \
  /* #sec-%asyncfromsynciteratorprototype%.return */                           \
  TFJ(AsyncFromSyncIteratorPrototypeReturn, kDontAdaptArgumentsSentinel)       \
  /* #sec-asyncfromsynciteratorcontinuation */                                 \
  TFJ(AsyncFromSyncIteratorCloseSyncAndRethrow, kJSArgcReceiverSlots + 1,      \
      kReceiver, kError)                                                       \
  /* #sec-async-iterator-value-unwrap-functions */                             \
  TFJ(AsyncIteratorValueUnwrap, kJSArgcReceiverSlots + 1, kReceiver, kValue)   \
                                                                               \
  /* CEntry */                                                                 \
  ASM(CEntry_Return1_ArgvInRegister_NoBuiltinExit, InterpreterCEntry1)         \
  ASM(CEntry_Return1_ArgvOnStack_BuiltinExit, CEntry1ArgvOnStack)              \
  ASM(CEntry_Return1_ArgvOnStack_NoBuiltinExit, CEntryDummy)                   \
  ASM(CEntry_Return2_ArgvInRegister_NoBuiltinExit, InterpreterCEntry2)         \
  ASM(CEntry_Return2_ArgvOnStack_BuiltinExit, CEntryDummy)                     \
  ASM(CEntry_Return2_ArgvOnStack_NoBuiltinExit, CEntryDummy)                   \
  ASM(WasmCEntry, CEntryDummy)                                                 \
  ASM(DirectCEntry, CEntryDummy)                                               \
                                                                               \
  /* String helpers */                                                         \
  TFS(StringAdd_CheckNone, NeedsContext::kYes, kLeft, kRight)                  \
  TFS(SubString, NeedsContext::kYes, kString, kFrom, kTo)                      \
                                                                               \
  /* Miscellaneous */                                                          \
  ASM(DoubleToI, Void)                                                         \
  TFC(GetProperty, GetProperty)                                                \
  TFS(GetPropertyWithReceiver, NeedsContext::kYes, kObject, kKey, kReceiver,   \
      kOnNonExistent)                                                          \
  TFS(SetProperty, NeedsContext::kYes, kReceiver, kKey, kValue)                \
  TFS(CreateDataProperty, NeedsContext::kYes, kReceiver, kKey, kValue)         \
  TFS(GetOwnPropertyDescriptor, NeedsContext::kYes, kReceiver, kKey)           \
  ASM(MemCopyUint8Uint8, CCall)                                                \
  ASM(MemMove, CCall)                                                          \
  TFC(FindNonDefaultConstructorOrConstruct,                                    \
      FindNonDefaultConstructorOrConstruct)                                    \
  TFS(OrdinaryGetOwnPropertyDescriptor, NeedsContext::kYes, kReceiver, kKey)   \
                                                                               \
  /* Trace */                                                                  \
  CPP(IsTraceCategoryEnabled)                                                  \
  CPP(Trace)                                                                   \
                                                                               \
  /* Weak refs */                                                              \
  CPP(FinalizationRegistryUnregister)                                          \
                                                                               \
  /* Async modules */                                                          \
  TFJ(AsyncModuleEvaluate, kDontAdaptArgumentsSentinel)                        \
                                                                               \
  /* CallAsyncModule* are spec anonymyous functions */                         \
  CPP(CallAsyncModuleFulfilled)                                                \
  CPP(CallAsyncModuleRejected)                                                 \
                                                                               \
  /* Temporal */                                                               \
  /* Temporal #sec-temporal.now.timezone */                                    \
  CPP(TemporalNowTimeZone)                                                     \
  /* Temporal #sec-temporal.now.instant */                                     \
  CPP(TemporalNowInstant)                                                      \
  /* Temporal #sec-temporal.now.plaindatetime */                               \
  CPP(TemporalNowPlainDateTime)                                                \
  /* Temporal #sec-temporal.now.plaindatetimeiso */                            \
  CPP(TemporalNowPlainDateTimeISO)                                             \
  /* Temporal #sec-temporal.now.zoneddatetime */                               \
  CPP(TemporalNowZonedDateTime)                                                \
  /* Temporal #sec-temporal.now.zoneddatetimeiso */                            \
  CPP(TemporalNowZonedDateTimeISO)                                             \
  /* Temporal #sec-temporal.now.plaindate */                                   \
  CPP(TemporalNowPlainDate)                                                    \
  /* Temporal #sec-temporal.now.plaindateiso */                                \
  CPP(TemporalNowPlainDateISO)                                                 \
  /* There are no Temporal.now.plainTime */                                    \
  /* See https://github.com/tc39/proposal-temporal/issues/1540 */              \
  /* Temporal #sec-temporal.now.plaintimeiso */                                \
  CPP(TemporalNowPlainTimeISO)                                                 \
                                                                               \
  /* Temporal.PlaneDate */                                                     \
  /* Temporal #sec-temporal.plaindate */                                       \
  CPP(TemporalPlainDateConstructor)                                            \
  /* Temporal #sec-temporal.plaindate.from */                                  \
  CPP(TemporalPlainDateFrom)                                                   \
  /* Temporal #sec-temporal.plaindate.compare */                               \
  CPP(TemporalPlainDateCompare)                                                \
  /* Temporal #sec-get-temporal.plaindate.prototype.calendar */                \
  CPP(TemporalPlainDatePrototypeCalendar)                                      \
  /* Temporal #sec-get-temporal.plaindate.prototype.year */                    \
  CPP(TemporalPlainDatePrototypeYear)                                          \
  /* Temporal #sec-get-temporal.plaindate.prototype.month */                   \
  CPP(TemporalPlainDatePrototypeMonth)                                         \
  /* Temporal #sec-get-temporal.plaindate.prototype.monthcode */               \
  CPP(TemporalPlainDatePrototypeMonthCode)                                     \
  /* Temporal #sec-get-temporal.plaindate.prototype.day */                     \
  CPP(TemporalPlainDatePrototypeDay)                                           \
  /* Temporal #sec-get-temporal.plaindate.prototype.dayofweek */               \
  CPP(TemporalPlainDatePrototypeDayOfWeek)                                     \
  /* Temporal #sec-get-temporal.plaindate.prototype.dayofyear */               \
  CPP(TemporalPlainDatePrototypeDayOfYear)                                     \
  /* Temporal #sec-get-temporal.plaindate.prototype.weekofyear */              \
  CPP(TemporalPlainDatePrototypeWeekOfYear)                                    \
  /* Temporal #sec-get-temporal.plaindate.prototype.daysinweek */              \
  CPP(TemporalPlainDatePrototypeDaysInWeek)                                    \
  /* Temporal #sec-get-temporal.plaindate.prototype.daysinmonth */             \
  CPP(TemporalPlainDatePrototypeDaysInMonth)                                   \
  /* Temporal #sec-get-temporal.plaindate.prototype.daysinyear */              \
  CPP(TemporalPlainDatePrototypeDaysInYear)                                    \
  /* Temporal #sec-get-temporal.plaindate.prototype.monthsinyear */            \
  CPP(TemporalPlainDatePrototypeMonthsInYear)                                  \
  /* Temporal #sec-get-temporal.plaindate.prototype.inleapyear */              \
  CPP(TemporalPlainDatePrototypeInLeapYear)                                    \
  /* Temporal #sec-temporal.plaindate.prototype.toplainyearmonth */            \
  CPP(TemporalPlainDatePrototypeToPlainYearMonth)                              \
  /* Temporal #sec-temporal.plaindate.prototype.toplainmonthday */             \
  CPP(TemporalPlainDatePrototypeToPlainMonthDay)                               \
  /* Temporal #sec-temporal.plaindate.prototype.getisofields */                \
  CPP(TemporalPlainDatePrototypeGetISOFields)                                  \
  /* Temporal #sec-temporal.plaindate.prototype.add */                         \
  CPP(TemporalPlainDatePrototypeAdd)                                           \
  /* Temporal #sec-temporal.plaindate.prototype.substract */                   \
  CPP(TemporalPlainDatePrototypeSubtract)                                      \
  /* Temporal #sec-temporal.plaindate.prototype.with */                        \
  CPP(TemporalPlainDatePrototypeWith)                                          \
  /* Temporal #sec-temporal.plaindate.prototype.withcalendar */                \
  CPP(TemporalPlainDatePrototypeWithCalendar)                                  \
  /* Temporal #sec-temporal.plaindate.prototype.until */                       \
  CPP(TemporalPlainDatePrototypeUntil)                                         \
  /* Temporal #sec-temporal.plaindate.prototype.since */                       \
  CPP(TemporalPlainDatePrototypeSince)                                         \
  /* Temporal #sec-temporal.plaindate.prototype.equals */                      \
  CPP(TemporalPlainDatePrototypeEquals)                                        \
  /* Temporal #sec-temporal.plaindate.prototype.toplaindatetime */             \
  CPP(TemporalPlainDatePrototypeToPlainDateTime)                               \
  /* Temporal #sec-temporal.plaindate.prototype.tozoneddatetime */             \
  CPP(TemporalPlainDatePrototypeToZonedDateTime)                               \
  /* Temporal #sec-temporal.plaindate.prototype.tostring */                    \
  CPP(TemporalPlainDatePrototypeToString)                                      \
  /* Temporal #sec-temporal.plaindate.prototype.tojson */                      \
  CPP(TemporalPlainDatePrototypeToJSON)                                        \
  /* Temporal #sec-temporal.plaindate.prototype.tolocalestring */              \
  CPP(TemporalPlainDatePrototypeToLocaleString)                                \
  /* Temporal #sec-temporal.plaindate.prototype.valueof */                     \
  CPP(TemporalPlainDatePrototypeValueOf)                                       \
                                                                               \
  /* Temporal.PlaneTime */                                                     \
  /* Temporal #sec-temporal.plaintime */                                       \
  CPP(TemporalPlainTimeConstructor)                                            \
  /* Temporal #sec-temporal.plaintime.from */                                  \
  CPP(TemporalPlainTimeFrom)                                                   \
  /* Temporal #sec-temporal.plaintime.compare */                               \
  CPP(TemporalPlainTimeCompare)                                                \
  /* Temporal #sec-get-temporal.plaintime.prototype.calendar */                \
  CPP(TemporalPlainTimePrototypeCalendar)                                      \
  /* Temporal #sec-get-temporal.plaintime.prototype.hour */                    \
  CPP(TemporalPlainTimePrototypeHour)                                          \
  /* Temporal #sec-get-temporal.plaintime.prototype.minute */                  \
  CPP(TemporalPlainTimePrototypeMinute)                                        \
  /* Temporal #sec-get-temporal.plaintime.prototype.second */                  \
  CPP(TemporalPlainTimePrototypeSecond)                                        \
  /* Temporal #sec-get-temporal.plaintime.prototype.millisecond */             \
  CPP(TemporalPlainTimePrototypeMillisecond)                                   \
  /* Temporal #sec-get-temporal.plaintime.prototype.microsecond */             \
  CPP(TemporalPlainTimePrototypeMicrosecond)                                   \
  /* Temporal #sec-get-temporal.plaintime.prototype.nanoseond */               \
  CPP(TemporalPlainTimePrototypeNanosecond)                                    \
  /* Temporal #sec-temporal.plaintime.prototype.add */                         \
  CPP(TemporalPlainTimePrototypeAdd)                                           \
  /* Temporal #sec-temporal.plaintime.prototype.subtract */                    \
  CPP(TemporalPlainTimePrototypeSubtract)                                      \
  /* Temporal #sec-temporal.plaintime.prototype.with */                        \
  CPP(TemporalPlainTimePrototypeWith)                                          \
  /* Temporal #sec-temporal.plaintime.prototype.until */                       \
  CPP(TemporalPlainTimePrototypeUntil)                                         \
  /* Temporal #sec-temporal.plaintime.prototype.since */                       \
  CPP(TemporalPlainTimePrototypeSince)                                         \
  /* Temporal #sec-temporal.plaintime.prototype.round */                       \
  CPP(TemporalPlainTimePrototypeRound)                                         \
  /* Temporal #sec-temporal.plaintime.prototype.equals */                      \
  CPP(TemporalPlainTimePrototypeEquals)                                        \
  /* Temporal #sec-temporal.plaintime.prototype.toplaindatetime */             \
  CPP(TemporalPlainTimePrototypeToPlainDateTime)                               \
  /* Temporal #sec-temporal.plaintime.prototype.tozoneddatetime */             \
  CPP(TemporalPlainTimePrototypeToZonedDateTime)                               \
  /* Temporal #sec-temporal.plaintime.prototype.getisofields */                \
  CPP(TemporalPlainTimePrototypeGetISOFields)                                  \
  /* Temporal #sec-temporal.plaintime.prototype.tostring */                    \
  CPP(TemporalPlainTimePrototypeToString)                                      \
  /* Temporal #sec-temporal.plaindtimeprototype.tojson */                      \
  CPP(TemporalPlainTimePrototypeToJSON)                                        \
  /* Temporal #sec-temporal.plaintime.prototype.tolocalestring */              \
  CPP(TemporalPlainTimePrototypeToLocaleString)                                \
  /* Temporal #sec-temporal.plaintime.prototype.valueof */                     \
  CPP(TemporalPlainTimePrototypeValueOf)                                       \
                                                                               \
  /* Temporal.PlaneDateTime */                                                 \
  /* Temporal #sec-temporal.plaindatetime */                                   \
  CPP(TemporalPlainDateTimeConstructor)                                        \
  /* Temporal #sec-temporal.plaindatetime.from */                              \
  CPP(TemporalPlainDateTimeFrom)                                               \
  /* Temporal #sec-temporal.plaindatetime.compare */                           \
  CPP(TemporalPlainDateTimeCompare)                                            \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.calendar */            \
  CPP(TemporalPlainDateTimePrototypeCalendar)                                  \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.year */                \
  CPP(TemporalPlainDateTimePrototypeYear)                                      \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.month */               \
  CPP(TemporalPlainDateTimePrototypeMonth)                                     \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.monthcode */           \
  CPP(TemporalPlainDateTimePrototypeMonthCode)                                 \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.day */                 \
  CPP(TemporalPlainDateTimePrototypeDay)                                       \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.hour */                \
  CPP(TemporalPlainDateTimePrototypeHour)                                      \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.minute */              \
  CPP(TemporalPlainDateTimePrototypeMinute)                                    \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.second */              \
  CPP(TemporalPlainDateTimePrototypeSecond)                                    \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.millisecond */         \
  CPP(TemporalPlainDateTimePrototypeMillisecond)                               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.microsecond */         \
  CPP(TemporalPlainDateTimePrototypeMicrosecond)                               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.nanosecond */          \
  CPP(TemporalPlainDateTimePrototypeNanosecond)                                \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.dayofweek */           \
  CPP(TemporalPlainDateTimePrototypeDayOfWeek)                                 \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.dayofyear */           \
  CPP(TemporalPlainDateTimePrototypeDayOfYear)                                 \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.weekofyear */          \
  CPP(TemporalPlainDateTimePrototypeWeekOfYear)                                \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.daysinweek */          \
  CPP(TemporalPlainDateTimePrototypeDaysInWeek)                                \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.daysinmonth */         \
  CPP(TemporalPlainDateTimePrototypeDaysInMonth)                               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.daysinyear */          \
  CPP(TemporalPlainDateTimePrototypeDaysInYear)                                \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.monthsinyear */        \
  CPP(TemporalPlainDateTimePrototypeMonthsInYear)                              \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.inleapyear */          \
  CPP(TemporalPlainDateTimePrototypeInLeapYear)                                \
  /* Temporal #sec-temporal.plaindatetime.prototype.with */                    \
  CPP(TemporalPlainDateTimePrototypeWith)                                      \
  /* Temporal #sec-temporal.plaindatetime.prototype.withplainTime */           \
  CPP(TemporalPlainDateTimePrototypeWithPlainTime)                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.withplainDate */           \
  CPP(TemporalPlainDateTimePrototypeWithPlainDate)                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.withcalendar */            \
  CPP(TemporalPlainDateTimePrototypeWithCalendar)                              \
  /* Temporal #sec-temporal.plaindatetime.prototype.add */                     \
  CPP(TemporalPlainDateTimePrototypeAdd)                                       \
  /* Temporal #sec-temporal.plaindatetime.prototype.subtract */                \
  CPP(TemporalPlainDateTimePrototypeSubtract)                                  \
  /* Temporal #sec-temporal.plaindatetime.prototype.until */                   \
  CPP(TemporalPlainDateTimePrototypeUntil)                                     \
  /* Temporal #sec-temporal.plaindatetime.prototype.since */                   \
  CPP(TemporalPlainDateTimePrototypeSince)                                     \
  /* Temporal #sec-temporal.plaindatetime.prototype.round */                   \
  CPP(TemporalPlainDateTimePrototypeRound)                                     \
  /* Temporal #sec-temporal.plaindatetime.prototype.equals */                  \
  CPP(TemporalPlainDateTimePrototypeEquals)                                    \
  /* Temporal #sec-temporal.plaindatetime.prototype.tostring */                \
  CPP(TemporalPlainDateTimePrototypeToString)                                  \
  /* Temporal #sec-temporal.plainddatetimeprototype.tojson */                  \
  CPP(TemporalPlainDateTimePrototypeToJSON)                                    \
  /* Temporal #sec-temporal.plaindatetime.prototype.tolocalestring */          \
  CPP(TemporalPlainDateTimePrototypeToLocaleString)                            \
  /* Temporal #sec-temporal.plaindatetime.prototype.valueof */                 \
  CPP(TemporalPlainDateTimePrototypeValueOf)                                   \
  /* Temporal #sec-temporal.plaindatetime.prototype.tozoneddatetime */         \
  CPP(TemporalPlainDateTimePrototypeToZonedDateTime)                           \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplaindate */             \
  CPP(TemporalPlainDateTimePrototypeToPlainDate)                               \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplainyearmonth */        \
  CPP(TemporalPlainDateTimePrototypeToPlainYearMonth)                          \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplainmonthday */         \
  CPP(TemporalPlainDateTimePrototypeToPlainMonthDay)                           \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplaintime */             \
  CPP(TemporalPlainDateTimePrototypeToPlainTime)                               \
  /* Temporal #sec-temporal.plaindatetime.prototype.getisofields */            \
  CPP(TemporalPlainDateTimePrototypeGetISOFields)                              \
                                                                               \
  /* Temporal.ZonedDateTime */                                                 \
  /* Temporal #sec-temporal.zoneddatetime */                                   \
  CPP(TemporalZonedDateTimeConstructor)                                        \
  /* Temporal #sec-temporal.zoneddatetime.from */                              \
  CPP(TemporalZonedDateTimeFrom)                                               \
  /* Temporal #sec-temporal.zoneddatetime.compare */                           \
  CPP(TemporalZonedDateTimeCompare)                                            \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.calendar */            \
  CPP(TemporalZonedDateTimePrototypeCalendar)                                  \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.timezone */            \
  CPP(TemporalZonedDateTimePrototypeTimeZone)                                  \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.year */                \
  CPP(TemporalZonedDateTimePrototypeYear)                                      \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.month */               \
  CPP(TemporalZonedDateTimePrototypeMonth)                                     \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.monthcode */           \
  CPP(TemporalZonedDateTimePrototypeMonthCode)                                 \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.day */                 \
  CPP(TemporalZonedDateTimePrototypeDay)                                       \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.hour */                \
  CPP(TemporalZonedDateTimePrototypeHour)                                      \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.minute */              \
  CPP(TemporalZonedDateTimePrototypeMinute)                                    \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.second */              \
  CPP(TemporalZonedDateTimePrototypeSecond)                                    \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.millisecond */         \
  CPP(TemporalZonedDateTimePrototypeMillisecond)                               \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.microsecond */         \
  CPP(TemporalZonedDateTimePrototypeMicrosecond)                               \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.nanosecond */          \
  CPP(TemporalZonedDateTimePrototypeNanosecond)                                \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochsecond */         \
  CPP(TemporalZonedDateTimePrototypeEpochSeconds)                              \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochmilliseconds */   \
  CPP(TemporalZonedDateTimePrototypeEpochMilliseconds)                         \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochmicroseconds */   \
  CPP(TemporalZonedDateTimePrototypeEpochMicroseconds)                         \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochnanoseconds */    \
  CPP(TemporalZonedDateTimePrototypeEpochNanoseconds)                          \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.dayofweek */           \
  CPP(TemporalZonedDateTimePrototypeDayOfWeek)                                 \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.dayofyear */           \
  CPP(TemporalZonedDateTimePrototypeDayOfYear)                                 \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.weekofyear */          \
  CPP(TemporalZonedDateTimePrototypeWeekOfYear)                                \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.hoursinday */          \
  CPP(TemporalZonedDateTimePrototypeHoursInDay)                                \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.daysinweek */          \
  CPP(TemporalZonedDateTimePrototypeDaysInWeek)                                \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.daysinmonth */         \
  CPP(TemporalZonedDateTimePrototypeDaysInMonth)                               \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.daysinyear */          \
  CPP(TemporalZonedDateTimePrototypeDaysInYear)                                \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.monthsinyear */        \
  CPP(TemporalZonedDateTimePrototypeMonthsInYear)                              \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.inleapyear */          \
  CPP(TemporalZonedDateTimePrototypeInLeapYear)                                \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.offsetnanoseconds */   \
  CPP(TemporalZonedDateTimePrototypeOffsetNanoseconds)                         \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.offset */              \
  CPP(TemporalZonedDateTimePrototypeOffset)                                    \
  /* Temporal #sec-temporal.zoneddatetime.prototype.with */                    \
  CPP(TemporalZonedDateTimePrototypeWith)                                      \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withplaintime */           \
  CPP(TemporalZonedDateTimePrototypeWithPlainTime)                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withplaindate */           \
  CPP(TemporalZonedDateTimePrototypeWithPlainDate)                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withtimezone */            \
  CPP(TemporalZonedDateTimePrototypeWithTimeZone)                              \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withcalendar */            \
  CPP(TemporalZonedDateTimePrototypeWithCalendar)                              \
  /* Temporal #sec-temporal.zoneddatetime.prototype.add */                     \
  CPP(TemporalZonedDateTimePrototypeAdd)                                       \
  /* Temporal #sec-temporal.zoneddatetime.prototype.subtract */                \
  CPP(TemporalZonedDateTimePrototypeSubtract)                                  \
  /* Temporal #sec-temporal.zoneddatetime.prototype.until */                   \
  CPP(TemporalZonedDateTimePrototypeUntil)                                     \
  /* Temporal #sec-temporal.zoneddatetime.prototype.since */                   \
  CPP(TemporalZonedDateTimePrototypeSince)                                     \
  /* Temporal #sec-temporal.zoneddatetime.prototype.round */                   \
  CPP(TemporalZonedDateTimePrototypeRound)                                     \
  /* Temporal #sec-temporal.zoneddatetime.prototype.equals */                  \
  CPP(TemporalZonedDateTimePrototypeEquals)                                    \
  /* Temporal #sec-temporal.zoneddatetime.prototype.tostring */                \
  CPP(TemporalZonedDateTimePrototypeToString)                                  \
  /* Temporal #sec-temporal.zonedddatetimeprototype.tojson */                  \
  CPP(TemporalZonedDateTimePrototypeToJSON)                                    \
  /* Temporal #sec-temporal.zoneddatetime.prototype.tolocalestring */          \
  CPP(TemporalZonedDateTimePrototypeToLocaleString)                            \
  /* Temporal #sec-temporal.zoneddatetime.prototype.valueof */                 \
  CPP(TemporalZonedDateTimePrototypeValueOf)                                   \
  /* Temporal #sec-temporal.zoneddatetime.prototype.startofday */              \
  CPP(TemporalZonedDateTimePrototypeStartOfDay)                                \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toinstant */               \
  CPP(TemporalZonedDateTimePrototypeToInstant)                                 \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplaindate */             \
  CPP(TemporalZonedDateTimePrototypeToPlainDate)                               \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplaintime */             \
  CPP(TemporalZonedDateTimePrototypeToPlainTime)                               \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplaindatetime */         \
  CPP(TemporalZonedDateTimePrototypeToPlainDateTime)                           \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplainyearmonth */        \
  CPP(TemporalZonedDateTimePrototypeToPlainYearMonth)                          \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplainmonthday */         \
  CPP(TemporalZonedDateTimePrototypeToPlainMonthDay)                           \
  /* Temporal #sec-temporal.zoneddatetime.prototype.getisofields */            \
  CPP(TemporalZonedDateTimePrototypeGetISOFields)                              \
                                                                               \
  /* Temporal.Duration */                                                      \
  /* Temporal #sec-temporal.duration */                                        \
  CPP(TemporalDurationConstructor)                                             \
  /* Temporal #sec-temporal.duration.from */                                   \
  CPP(TemporalDurationFrom)                                                    \
  /* Temporal #sec-temporal.duration.compare */                                \
  CPP(TemporalDurationCompare)                                                 \
  /* Temporal #sec-get-temporal.duration.prototype.years */                    \
  CPP(TemporalDurationPrototypeYears)                                          \
  /* Temporal #sec-get-temporal.duration.prototype.months */                   \
  CPP(TemporalDurationPrototypeMonths)                                         \
  /* Temporal #sec-get-temporal.duration.prototype.weeks */                    \
  CPP(TemporalDurationPrototypeWeeks)                                          \
  /* Temporal #sec-get-temporal.duration.prototype.days */                     \
  CPP(TemporalDurationPrototypeDays)                                           \
  /* Temporal #sec-get-temporal.duration.prototype.hours */                    \
  CPP(TemporalDurationPrototypeHours)                                          \
  /* Temporal #sec-get-temporal.duration.prototype.minutes */                  \
  CPP(TemporalDurationPrototypeMinutes)                                        \
  /* Temporal #sec-get-temporal.duration.prototype.seconds */                  \
  CPP(TemporalDurationPrototypeSeconds)                                        \
  /* Temporal #sec-get-temporal.duration.prototype.milliseconds */             \
  CPP(TemporalDurationPrototypeMilliseconds)                                   \
  /* Temporal #sec-get-temporal.duration.prototype.microseconds */             \
  CPP(TemporalDurationPrototypeMicroseconds)                                   \
  /* Temporal #sec-get-temporal.duration.prototype.nanoseconds */              \
  CPP(TemporalDurationPrototypeNanoseconds)                                    \
  /* Temporal #sec-get-temporal.duration.prototype.sign */                     \
  CPP(TemporalDurationPrototypeSign)                                           \
  /* Temporal #sec-get-temporal.duration.prototype.blank */                    \
  CPP(TemporalDurationPrototypeBlank)                                          \
  /* Temporal #sec-temporal.duration.prototype.with */                         \
  CPP(TemporalDurationPrototypeWith)                                           \
  /* Temporal #sec-temporal.duration.prototype.negated */                      \
  CPP(TemporalDurationPrototypeNegated)                                        \
  /* Temporal #sec-temporal.duration.prototype.abs */                          \
  CPP(TemporalDurationPrototypeAbs)                                            \
  /* Temporal #sec-temporal.duration.prototype.add */                          \
  CPP(TemporalDurationPrototypeAdd)                                            \
  /* Temporal #sec-temporal.duration.prototype.subtract */                     \
  CPP(TemporalDurationPrototypeSubtract)                                       \
  /* Temporal #sec-temporal.duration.prototype.round */                        \
  CPP(TemporalDurationPrototypeRound)                                          \
  /* Temporal #sec-temporal.duration.prototype.total */                        \
  CPP(TemporalDurationPrototypeTotal)                                          \
  /* Temporal #sec-temporal.duration.prototype.tostring */                     \
  CPP(TemporalDurationPrototypeToString)                                       \
  /* Temporal #sec-temporal.duration.tojson */                                 \
  CPP(TemporalDurationPrototypeToJSON)                                         \
  /* Temporal #sec-temporal.duration.prototype.tolocalestring */               \
  CPP(TemporalDurationPrototypeToLocaleString)                                 \
  /* Temporal #sec-temporal.duration.prototype.valueof */                      \
  CPP(TemporalDurationPrototypeValueOf)                                        \
                                                                               \
  /* Temporal.Instant */                                                       \
  /* Temporal #sec-temporal.instant */                                         \
  CPP(TemporalInstantConstructor)                                              \
  /* Temporal #sec-temporal.instant.from */                                    \
  CPP(TemporalInstantFrom)                                                     \
  /* Temporal #sec-temporal.instant.fromepochseconds */                        \
  CPP(TemporalInstantFromEpochSeconds)                                         \
  /* Temporal #sec-temporal.instant.fromepochmilliseconds */                   \
  CPP(TemporalInstantFromEpochMilliseconds)                                    \
  /* Temporal #sec-temporal.instant.fromepochmicroseconds */                   \
  CPP(TemporalInstantFromEpochMicroseconds)                                    \
  /* Temporal #sec-temporal.instant.fromepochnanoseconds */                    \
  CPP(TemporalInstantFromEpochNanoseconds)                                     \
  /* Temporal #sec-temporal.instant.compare */                                 \
  CPP(TemporalInstantCompare)                                                  \
  /* Temporal #sec-get-temporal.instant.prototype.epochseconds */              \
  CPP(TemporalInstantPrototypeEpochSeconds)                                    \
  /* Temporal #sec-get-temporal.instant.prototype.epochmilliseconds */         \
  CPP(TemporalInstantPrototypeEpochMilliseconds)                               \
  /* Temporal #sec-get-temporal.instant.prototype.epochmicroseconds */         \
  CPP(TemporalInstantPrototypeEpochMicroseconds)                               \
  /* Temporal #sec-get-temporal.instant.prototype.epochnanoseconds */          \
  CPP(TemporalInstantPrototypeEpochNanoseconds)                                \
  /* Temporal #sec-temporal.instant.prototype.add */                           \
  CPP(TemporalInstantPrototypeAdd)                                             \
  /* Temporal #sec-temporal.instant.prototype.subtract */                      \
  CPP(TemporalInstantPrototypeSubtract)                                        \
  /* Temporal #sec-temporal.instant.prototype.until */                         \
  CPP(TemporalInstantPrototypeUntil)                                           \
  /* Temporal #sec-temporal.instant.prototype.since */                         \
  CPP(TemporalInstantPrototypeSince)                                           \
  /* Temporal #sec-temporal.instant.prototype.round */                         \
  CPP(TemporalInstantPrototypeRound)                                           \
  /* Temporal #sec-temporal.instant.prototype.equals */                        \
  CPP(TemporalInstantPrototypeEquals)                                          \
  /* Temporal #sec-temporal.instant.prototype.tostring */                      \
  CPP(TemporalInstantPrototypeToString)                                        \
  /* Temporal #sec-temporal.instant.tojson */                                  \
  CPP(TemporalInstantPrototypeToJSON)                                          \
  /* Temporal #sec-temporal.instant.prototype.tolocalestring */                \
  CPP(TemporalInstantPrototypeToLocaleString)                                  \
  /* Temporal #sec-temporal.instant.prototype.valueof */                       \
  CPP(TemporalInstantPrototypeValueOf)                                         \
  /* Temporal #sec-temporal.instant.prototype.tozoneddatetime */               \
  CPP(TemporalInstantPrototypeToZonedDateTime)                                 \
  /* Temporal #sec-temporal.instant.prototype.tozoneddatetimeiso */            \
  CPP(TemporalInstantPrototypeToZonedDateTimeISO)                              \
                                                                               \
  /* Temporal.PlainYearMonth */                                                \
  /* Temporal #sec-temporal.plainyearmonth */                                  \
  CPP(TemporalPlainYearMonthConstructor)                                       \
  /* Temporal #sec-temporal.plainyearmonth.from */                             \
  CPP(TemporalPlainYearMonthFrom)                                              \
  /* Temporal #sec-temporal.plainyearmonth.compare */                          \
  CPP(TemporalPlainYearMonthCompare)                                           \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.calendar */           \
  CPP(TemporalPlainYearMonthPrototypeCalendar)                                 \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.year */               \
  CPP(TemporalPlainYearMonthPrototypeYear)                                     \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.month */              \
  CPP(TemporalPlainYearMonthPrototypeMonth)                                    \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.monthcode */          \
  CPP(TemporalPlainYearMonthPrototypeMonthCode)                                \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.daysinyear */         \
  CPP(TemporalPlainYearMonthPrototypeDaysInYear)                               \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.daysinmonth */        \
  CPP(TemporalPlainYearMonthPrototypeDaysInMonth)                              \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.monthsinyear */       \
  CPP(TemporalPlainYearMonthPrototypeMonthsInYear)                             \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.inleapyear */         \
  CPP(TemporalPlainYearMonthPrototypeInLeapYear)                               \
  /* Temporal #sec-temporal.plainyearmonth.prototype.with */                   \
  CPP(TemporalPlainYearMonthPrototypeWith)                                     \
  /* Temporal #sec-temporal.plainyearmonth.prototype.add */                    \
  CPP(TemporalPlainYearMonthPrototypeAdd)                                      \
  /* Temporal #sec-temporal.plainyearmonth.prototype.subtract */               \
  CPP(TemporalPlainYearMonthPrototypeSubtract)                                 \
  /* Temporal #sec-temporal.plainyearmonth.prototype.until */                  \
  CPP(TemporalPlainYearMonthPrototypeUntil)                                    \
  /* Temporal #sec-temporal.plainyearmonth.prototype.since */                  \
  CPP(TemporalPlainYearMonthPrototypeSince)                                    \
  /* Temporal #sec-temporal.plainyearmonth.prototype.equals */                 \
  CPP(TemporalPlainYearMonthPrototypeEquals)                                   \
  /* Temporal #sec-temporal.plainyearmonth.tostring */                         \
  CPP(TemporalPlainYearMonthPrototypeToString)                                 \
  /* Temporal #sec-temporal.plainyearmonth.tojson */                           \
  CPP(TemporalPlainYearMonthPrototypeToJSON)                                   \
  /* Temporal #sec-temporal.plainyearmonth.prototype.tolocalestring */         \
  CPP(TemporalPlainYearMonthPrototypeToLocaleString)                           \
  /* Temporal #sec-temporal.plainyearmonth.prototype.valueof */                \
  CPP(TemporalPlainYearMonthPrototypeValueOf)                                  \
  /* Temporal #sec-temporal.plainyearmonth.prototype.toplaindate */            \
  CPP(TemporalPlainYearMonthPrototypeToPlainDate)                              \
  /* Temporal #sec-temporal.plainyearmonth.prototype.getisofields */           \
  CPP(TemporalPlainYearMonthPrototypeGetISOFields)                             \
                                                                               \
  /* Temporal.PlainMonthDay */                                                 \
  /* Temporal #sec-temporal.plainmonthday */                                   \
  CPP(TemporalPlainMonthDayConstructor)                                        \
  /* Temporal #sec-temporal.plainmonthday.from */                              \
  CPP(TemporalPlainMonthDayFrom)                                               \
  /* There are no compare for PlainMonthDay */                                 \
  /* See https://github.com/tc39/proposal-temporal/issues/1547 */              \
  /* Temporal #sec-get-temporal.plainmonthday.prototype.calendar */            \
  CPP(TemporalPlainMonthDayPrototypeCalendar)                                  \
  /* Temporal #sec-get-temporal.plainmonthday.prototype.monthcode */           \
  CPP(TemporalPlainMonthDayPrototypeMonthCode)                                 \
  /* Temporal #sec-get-temporal.plainmonthday.prototype.day */                 \
  CPP(TemporalPlainMonthDayPrototypeDay)                                       \
  /* Temporal #sec-temporal.plainmonthday.prototype.with */                    \
  CPP(TemporalPlainMonthDayPrototypeWith)                                      \
  /* Temporal #sec-temporal.plainmonthday.prototype.equals */                  \
  CPP(TemporalPlainMonthDayPrototypeEquals)                                    \
  /* Temporal #sec-temporal.plainmonthday.prototype.tostring */                \
  CPP(TemporalPlainMonthDayPrototypeToString)                                  \
  /* Temporal #sec-temporal.plainmonthday.tojson */                            \
  CPP(TemporalPlainMonthDayPrototypeToJSON)                                    \
  /* Temporal #sec-temporal.plainmonthday.prototype.tolocalestring */          \
  CPP(TemporalPlainMonthDayPrototypeToLocaleString)                            \
  /* Temporal #sec-temporal.plainmonthday.prototype.valueof */                 \
  CPP(TemporalPlainMonthDayPrototypeValueOf)                                   \
  /* Temporal #sec-temporal.plainmonthday.prototype.toplaindate */             \
  CPP(TemporalPlainMonthDayPrototypeToPlainDate)                               \
  /* Temporal #sec-temporal.plainmonthday.prototype.getisofields */            \
  CPP(TemporalPlainMonthDayPrototypeGetISOFields)                              \
                                                                               \
  /* Temporal.TimeZone */                                                      \
  /* Temporal #sec-temporal.timezone */                                        \
  CPP(TemporalTimeZoneConstructor)                                             \
  /* Temporal #sec-temporal.timezone.from */                                   \
  CPP(TemporalTimeZoneFrom)                                                    \
  /* Temporal #sec-get-temporal.timezone.prototype.id */                       \
  CPP(TemporalTimeZonePrototypeId)                                             \
  /* Temporal #sec-temporal.timezone.prototype.getoffsetnanosecondsfor */      \
  CPP(TemporalTimeZonePrototypeGetOffsetNanosecondsFor)                        \
  /* Temporal #sec-temporal.timezone.prototype.getoffsetstringfor */           \
  CPP(TemporalTimeZonePrototypeGetOffsetStringFor)                             \
  /* Temporal #sec-temporal.timezone.prototype.getplaindatetimefor */          \
  CPP(TemporalTimeZonePrototypeGetPlainDateTimeFor)                            \
  /* Temporal #sec-temporal.timezone.prototype.getinstantfor */                \
  CPP(TemporalTimeZonePrototypeGetInstantFor)                                  \
  /* Temporal #sec-temporal.timezone.prototype.getpossibleinstantsfor */       \
  CPP(TemporalTimeZonePrototypeGetPossibleInstantsFor)                         \
  /* Temporal #sec-temporal.timezone.prototype.getnexttransition */            \
  CPP(TemporalTimeZonePrototypeGetNextTransition)                              \
  /* Temporal #sec-temporal.timezone.prototype.getprevioustransition */        \
  CPP(TemporalTimeZonePrototypeGetPreviousTransition)                          \
  /* Temporal #sec-temporal.timezone.prototype.tostring */                     \
  CPP(TemporalTimeZonePrototypeToString)                                       \
  /* Temporal #sec-temporal.timezone.prototype.tojson */                       \
  CPP(TemporalTimeZonePrototypeToJSON)                                         \
                                                                               \
  /* Temporal.Calendar */                                                      \
  /* Temporal #sec-temporal.calendar */                                        \
  CPP(TemporalCalendarConstructor)                                             \
  /* Temporal #sec-temporal.calendar.from */                                   \
  CPP(TemporalCalendarFrom)                                                    \
  /* Temporal #sec-get-temporal.calendar.prototype.id */                       \
  CPP(TemporalCalendarPrototypeId)                                             \
  /* Temporal #sec-temporal.calendar.prototype.datefromfields */               \
  CPP(TemporalCalendarPrototypeDateFromFields)                                 \
  /* Temporal #sec-temporal.calendar.prototype.yearmonthfromfields */          \
  CPP(TemporalCalendarPrototypeYearMonthFromFields)                            \
  /* Temporal #sec-temporal.calendar.prototype.monthdayfromfields */           \
  CPP(TemporalCalendarPrototypeMonthDayFromFields)                             \
  /* Temporal #sec-temporal.calendar.prototype.dateadd */                      \
  CPP(TemporalCalendarPrototypeDateAdd)                                        \
  /* Temporal #sec-temporal.calendar.prototype.dateuntil */                    \
  CPP(TemporalCalendarPrototypeDateUntil)                                      \
  /* Temporal #sec-temporal.calendar.prototype.year */                         \
  CPP(TemporalCalendarPrototypeYear)                                           \
  /* Temporal #sec-temporal.calendar.prototype.month */                        \
  CPP(TemporalCalendarPrototypeMonth)                                          \
  /* Temporal #sec-temporal.calendar.prototype.monthcode */                    \
  CPP(TemporalCalendarPrototypeMonthCode)                                      \
  /* Temporal #sec-temporal.calendar.prototype.day */                          \
  CPP(TemporalCalendarPrototypeDay)                                            \
  /* Temporal #sec-temporal.calendar.prototype.dayofweek */                    \
  CPP(TemporalCalendarPrototypeDayOfWeek)                                      \
  /* Temporal #sec-temporal.calendar.prototype.dayofyear */                    \
  CPP(TemporalCalendarPrototypeDayOfYear)                                      \
  /* Temporal #sec-temporal.calendar.prototype.weekofyear */                   \
  CPP(TemporalCalendarPrototypeWeekOfYear)                                     \
  /* Temporal #sec-temporal.calendar.prototype.daysinweek */                   \
  CPP(TemporalCalendarPrototypeDaysInWeek)                                     \
  /* Temporal #sec-temporal.calendar.prototype.daysinmonth */                  \
  CPP(TemporalCalendarPrototypeDaysInMonth)                                    \
  /* Temporal #sec-temporal.calendar.prototype.daysinyear */                   \
  CPP(TemporalCalendarPrototypeDaysInYear)                                     \
  /* Temporal #sec-temporal.calendar.prototype.monthsinyear */                 \
  CPP(TemporalCalendarPrototypeMonthsInYear)                                   \
  /* Temporal #sec-temporal.calendar.prototype.inleapyear */                   \
  CPP(TemporalCalendarPrototypeInLeapYear)                                     \
  /* Temporal #sec-temporal.calendar.prototype.fields */                       \
  TFJ(TemporalCalendarPrototypeFields, kJSArgcReceiverSlots, kIterable)        \
  /* Temporal #sec-temporal.calendar.prototype.mergefields */                  \
  CPP(TemporalCalendarPrototypeMergeFields)                                    \
  /* Temporal #sec-temporal.calendar.prototype.tostring */                     \
  CPP(TemporalCalendarPrototypeToString)                                       \
  /* Temporal #sec-temporal.calendar.prototype.tojson */                       \
  CPP(TemporalCalendarPrototypeToJSON)                                         \
  /* Temporal #sec-date.prototype.totemporalinstant */                         \
  CPP(DatePrototypeToTemporalInstant)                                          \
                                                                               \
  /* "Private" (created but not exposed) Bulitins needed by Temporal */        \
  TFJ(StringFixedArrayFromIterable, kJSArgcReceiverSlots, kIterable)           \
  TFJ(TemporalInstantFixedArrayFromIterable, kJSArgcReceiverSlots, kIterable)

#define BUILTIN_LIST_BASE(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, ASM) \
  BUILTIN_LIST_BASE_TIER0(CPP, TFJ, TFC, TFS, TFH, ASM)           \
  BUILTIN_LIST_BASE_TIER1(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, ASM)

#ifdef V8_INTL_SUPPORT
#define BUILTIN_LIST_INTL(CPP, TFJ, TFS)                               \
  /* ecma402 #sec-intl.collator */                                     \
  CPP(CollatorConstructor)                                             \
  /* ecma 402 #sec-collator-compare-functions*/                        \
  CPP(CollatorInternalCompare)                                         \
  /* ecma402 #sec-intl.collator.prototype.compare */                   \
  CPP(CollatorPrototypeCompare)                                        \
  /* ecma402 #sec-intl.collator.supportedlocalesof */                  \
  CPP(CollatorSupportedLocalesOf)                                      \
  /* ecma402 #sec-intl.collator.prototype.resolvedoptions */           \
  CPP(CollatorPrototypeResolvedOptions)                                \
  /* ecma402 #sup-date.prototype.tolocaledatestring */                 \
  CPP(DatePrototypeToLocaleDateString)                                 \
  /* ecma402 #sup-date.prototype.tolocalestring */                     \
  CPP(DatePrototypeToLocaleString)                                     \
  /* ecma402 #sup-date.prototype.tolocaletimestring */                 \
  CPP(DatePrototypeToLocaleTimeString)                                 \
  /* ecma402 #sec-intl.datetimeformat */                               \
  CPP(DateTimeFormatConstructor)                                       \
  /* ecma402 #sec-datetime-format-functions */                         \
  CPP(DateTimeFormatInternalFormat)                                    \
  /* ecma402 #sec-intl.datetimeformat.prototype.format */              \
  CPP(DateTimeFormatPrototypeFormat)                                   \
  /* ecma402 #sec-intl.datetimeformat.prototype.formatrange */         \
  CPP(DateTimeFormatPrototypeFormatRange)                              \
  /* ecma402 #sec-intl.datetimeformat.prototype.formatrangetoparts */  \
  CPP(DateTimeFormatPrototypeFormatRangeToParts)                       \
  /* ecma402 #sec-intl.datetimeformat.prototype.formattoparts */       \
  CPP(DateTimeFormatPrototypeFormatToParts)                            \
  /* ecma402 #sec-intl.datetimeformat.prototype.resolvedoptions */     \
  CPP(DateTimeFormatPrototypeResolvedOptions)                          \
  /* ecma402 #sec-intl.datetimeformat.supportedlocalesof */            \
  CPP(DateTimeFormatSupportedLocalesOf)                                \
  /* ecma402 #sec-Intl.DisplayNames */                                 \
  CPP(DisplayNamesConstructor)                                         \
  /* ecma402 #sec-Intl.DisplayNames.prototype.of */                    \
  CPP(DisplayNamesPrototypeOf)                                         \
  /* ecma402 #sec-Intl.DisplayNames.prototype.resolvedOptions */       \
  CPP(DisplayNamesPrototypeResolvedOptions)                            \
  /* ecma402 #sec-Intl.DisplayNames.supportedLocalesOf */              \
  CPP(DisplayNamesSupportedLocalesOf)                                  \
  /* ecma402 #sec-intl-durationformat-constructor */                   \
  CPP(DurationFormatConstructor)                                       \
  /* ecma402 #sec-Intl.DurationFormat.prototype.format */              \
  CPP(DurationFormatPrototypeFormat)                                   \
  /* ecma402 #sec-Intl.DurationFormat.prototype.formatToParts */       \
  CPP(DurationFormatPrototypeFormatToParts)                            \
  /* ecma402 #sec-Intl.DurationFormat.prototype.resolvedOptions */     \
  CPP(DurationFormatPrototypeResolvedOptions)                          \
  /* ecma402 #sec-Intl.DurationFormat.supportedLocalesOf */            \
  CPP(DurationFormatSupportedLocalesOf)                                \
  /* ecma402 #sec-intl.getcanonicallocales */                          \
  CPP(IntlGetCanonicalLocales)                                         \
  /* ecma402 #sec-intl.supportedvaluesof */                            \
  CPP(IntlSupportedValuesOf)                                           \
  /* ecma402 #sec-intl-listformat-constructor */                       \
  CPP(ListFormatConstructor)                                           \
  /* ecma402 #sec-intl-list-format.prototype.format */                 \
  TFJ(ListFormatPrototypeFormat, kDontAdaptArgumentsSentinel)          \
  /* ecma402 #sec-intl-list-format.prototype.formattoparts */          \
  TFJ(ListFormatPrototypeFormatToParts, kDontAdaptArgumentsSentinel)   \
  /* ecma402 #sec-intl.listformat.prototype.resolvedoptions */         \
  CPP(ListFormatPrototypeResolvedOptions)                              \
  /* ecma402 #sec-intl.ListFormat.supportedlocalesof */                \
  CPP(ListFormatSupportedLocalesOf)                                    \
  /* ecma402 #sec-intl-locale-constructor */                           \
  CPP(LocaleConstructor)                                               \
  /* ecma402 #sec-Intl.Locale.prototype.baseName */                    \
  CPP(LocalePrototypeBaseName)                                         \
  /* ecma402 #sec-Intl.Locale.prototype.calendar */                    \
  CPP(LocalePrototypeCalendar)                                         \
  /* ecma402 #sec-Intl.Locale.prototype.calendars */                   \
  CPP(LocalePrototypeCalendars)                                        \
  /* ecma402 #sec-Intl.Locale.prototype.caseFirst */                   \
  CPP(LocalePrototypeCaseFirst)                                        \
  /* ecma402 #sec-Intl.Locale.prototype.collation */                   \
  CPP(LocalePrototypeCollation)                                        \
  /* ecma402 #sec-Intl.Locale.prototype.collations */                  \
  CPP(LocalePrototypeCollations)                                       \
  /* ecma402 #sec-Intl.Locale.prototype.firstDayOfWeek */              \
  CPP(LocalePrototypeFirstDayOfWeek)                                   \
  /* ecma402 #sec-Intl.Locale.prototype.getCalendars */                \
  CPP(LocalePrototypeGetCalendars)                                     \
  /* ecma402 #sec-Intl.Locale.prototype.getCollations */               \
  CPP(LocalePrototypeGetCollations)                                    \
  /* ecma402 #sec-Intl.Locale.prototype.getHourCycles */               \
  CPP(LocalePrototypeGetHourCycles)                                    \
  /* ecma402 #sec-Intl.Locale.prototype.getNumberingSystems */         \
  CPP(LocalePrototypeGetNumberingSystems)                              \
  /* ecma402 #sec-Intl.Locale.prototype.getTimeZones */                \
  CPP(LocalePrototypeGetTimeZones)                                     \
  /* ecma402 #sec-Intl.Locale.prototype.getTextInfo */                 \
  CPP(LocalePrototypeGetTextInfo)                                      \
  /* ecma402 #sec-Intl.Locale.prototype.getWeekInfo */                 \
  CPP(LocalePrototypeGetWeekInfo)                                      \
  /* ecma402 #sec-Intl.Locale.prototype.hourCycle */                   \
  CPP(LocalePrototypeHourCycle)                                        \
  /* ecma402 #sec-Intl.Locale.prototype.hourCycles */                  \
  CPP(LocalePrototypeHourCycles)                                       \
  /* ecma402 #sec-Intl.Locale.prototype.language */                    \
  CPP(LocalePrototypeLanguage)                                         \
  /* ecma402 #sec-Intl.Locale.prototype.maximize */                    \
  CPP(LocalePrototypeMaximize)                                         \
  /* ecma402 #sec-Intl.Locale.prototype.minimize */                    \
  CPP(LocalePrototypeMinimize)                                         \
  /* ecma402 #sec-Intl.Locale.prototype.numeric */                     \
  CPP(LocalePrototypeNumeric)                                          \
  /* ecma402 #sec-Intl.Locale.prototype.numberingSystem */             \
  CPP(LocalePrototypeNumberingSystem)                                  \
  /* ecma402 #sec-Intl.Locale.prototype.numberingSystems */            \
  CPP(LocalePrototypeNumberingSystems)                                 \
  /* ecma402 #sec-Intl.Locale.prototype.region */                      \
  CPP(LocalePrototypeRegion)                                           \
  /* ecma402 #sec-Intl.Locale.prototype.script */                      \
  CPP(LocalePrototypeScript)                                           \
  /* ecma402 #sec-Intl.Locale.prototype.textInfo */                    \
  CPP(LocalePrototypeTextInfo)                                         \
  /* ecma402 #sec-Intl.Locale.prototype.timezones */                   \
  CPP(LocalePrototypeTimeZones)                                        \
  /* ecma402 #sec-Intl.Locale.prototype.toString */                    \
  CPP(LocalePrototypeToString)                                         \
  /* ecma402 #sec-Intl.Locale.prototype.weekInfo */                    \
  CPP(LocalePrototypeWeekInfo)                                         \
  /* ecma402 #sec-intl.numberformat */                                 \
  CPP(NumberFormatConstructor)                                         \
  /* ecma402 #sec-number-format-functions */                           \
  CPP(NumberFormatInternalFormatNumber)                                \
  /* ecma402 #sec-intl.numberformat.prototype.format */                \
  CPP(NumberFormatPrototypeFormatNumber)                               \
  /* ecma402 #sec-intl.numberformat.prototype.formatrange */           \
  CPP(NumberFormatPrototypeFormatRange)                                \
  /* ecma402 #sec-intl.numberformat.prototype.formatrangetoparts */    \
  CPP(NumberFormatPrototypeFormatRangeToParts)                         \
  /* ecma402 #sec-intl.numberformat.prototype.formattoparts */         \
  CPP(NumberFormatPrototypeFormatToParts)                              \
  /* ecma402 #sec-intl.numberformat.prototype.resolvedoptions */       \
  CPP(NumberFormatPrototypeResolvedOptions)                            \
  /* ecma402 #sec-intl.numberformat.supportedlocalesof */              \
  CPP(NumberFormatSupportedLocalesOf)                                  \
  /* ecma402 #sec-intl.pluralrules */                                  \
  CPP(PluralRulesConstructor)                                          \
  /* ecma402 #sec-intl.pluralrules.prototype.resolvedoptions */        \
  CPP(PluralRulesPrototypeResolvedOptions)                             \
  /* ecma402 #sec-intl.pluralrules.prototype.select */                 \
  CPP(PluralRulesPrototypeSelect)                                      \
  /* ecma402 #sec-intl.pluralrules.prototype.selectrange */            \
  CPP(PluralRulesPrototypeSelectRange)                                 \
  /* ecma402 #sec-intl.pluralrules.supportedlocalesof */               \
  CPP(PluralRulesSupportedLocalesOf)                                   \
  /* ecma402 #sec-intl.RelativeTimeFormat.constructor */               \
  CPP(RelativeTimeFormatConstructor)                                   \
  /* ecma402 #sec-intl.RelativeTimeFormat.prototype.format */          \
  CPP(RelativeTimeFormatPrototypeFormat)                               \
  /* ecma402 #sec-intl.RelativeTimeFormat.prototype.formatToParts */   \
  CPP(RelativeTimeFormatPrototypeFormatToParts)                        \
  /* ecma402 #sec-intl.RelativeTimeFormat.prototype.resolvedOptions */ \
  CPP(RelativeTimeFormatPrototypeResolvedOptions)                      \
  /* ecma402 #sec-intl.RelativeTimeFormat.supportedlocalesof */        \
  CPP(RelativeTimeFormatSupportedLocalesOf)                            \
  /* ecma402 #sec-Intl.Segmenter */                                    \
  CPP(SegmenterConstructor)                                            \
  /* ecma402 #sec-Intl.Segmenter.prototype.resolvedOptions */          \
  CPP(SegmenterPrototypeResolvedOptions)                               \
  /* ecma402 #sec-Intl.Segmenter.prototype.segment  */                 \
  CPP(SegmenterPrototypeSegment)                                       \
  /* ecma402  #sec-Intl.Segmenter.supportedLocalesOf */                \
  CPP(SegmenterSupportedLocalesOf)                                     \
  /* ecma402 #sec-segment-iterator-prototype-next */                   \
  CPP(SegmentIteratorPrototypeNext)                                    \
  /* ecma402 #sec-%segmentsprototype%.containing */                    \
  CPP(SegmentsPrototypeContaining)                                     \
  /* ecma402 #sec-%segmentsprototype%-@@iterator */                    \
  CPP(SegmentsPrototypeIterator)                                       \
  /* ES #sec-string.prototype.normalize */                             \
  CPP(StringPrototypeNormalizeIntl)                                    \
  /* ecma402 #sup-string.prototype.tolocalelowercase */                \
  TFJ(StringPrototypeToLocaleLowerCase, kDontAdaptArgumentsSentinel)   \
  /* ecma402 #sup-string.prototype.tolocaleuppercase */                \
  CPP(StringPrototypeToLocaleUpperCase)                                \
  /* ES #sec-string.prototype.tolowercase */                           \
  TFJ(StringPrototypeToLowerCaseIntl, kJSArgcReceiverSlots, kReceiver) \
  /* ES #sec-string.prototype.touppercase */                           \
  CPP(StringPrototypeToUpperCaseIntl)                                  \
  TFS(StringToLowerCaseIntl, NeedsContext::kYes, kString)              \
                                                                       \
  /* Temporal */                                                       \
  /* Temporal #sec-temporal.calendar.prototype.era */                  \
  CPP(TemporalCalendarPrototypeEra)                                    \
  /* Temporal #sec-temporal.calendar.prototype.erayear */              \
  CPP(TemporalCalendarPrototypeEraYear)                                \
  /* Temporal #sec-get-temporal.plaindate.prototype.era */             \
  CPP(TemporalPlainDatePrototypeEra)                                   \
  /* Temporal #sec-get-temporal.plaindate.prototype.erayear */         \
  CPP(TemporalPlainDatePrototypeEraYear)                               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.era */         \
  CPP(TemporalPlainDateTimePrototypeEra)                               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.erayear */     \
  CPP(TemporalPlainDateTimePrototypeEraYear)                           \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.era */        \
  CPP(TemporalPlainYearMonthPrototypeEra)                              \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.erayear */    \
  CPP(TemporalPlainYearMonthPrototypeEraYear)                          \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.era */         \
  CPP(TemporalZonedDateTimePrototypeEra)                               \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.erayear */     \
  CPP(TemporalZonedDateTimePrototypeEraYear)                           \
                                                                       \
  CPP(V8BreakIteratorConstructor)                                      \
  CPP(V8BreakIteratorInternalAdoptText)                                \
  CPP(V8BreakIteratorInternalBreakType)                                \
  CPP(V8BreakIteratorInternalCurrent)                                  \
  CPP(V8BreakIteratorInternalFirst)                                    \
  CPP(V8BreakIteratorInternalNext)                                     \
  CPP(V8BreakIteratorPrototypeAdoptText)                               \
  CPP(V8BreakIteratorPrototypeBreakType)                               \
  CPP(V8BreakIteratorPrototypeCurrent)                                 \
  CPP(V8BreakIteratorPrototypeFirst)                                   \
  CPP(V8BreakIteratorPrototypeNext)                                    \
  CPP(V8BreakIteratorPrototypeResolvedOptions)                         \
  CPP(V8BreakIteratorSupportedLocalesOf)
#else
#define BUILTIN_LIST_INTL(CPP, TFJ, TFS)      \
  /* no-op fallback version */                \
  CPP(StringPrototypeNormalize)               \
  /* same as toLowercase; fallback version */ \
  CPP(StringPrototypeToLocaleLowerCase)       \
  /* same as toUppercase; fallback version */ \
  CPP(StringPrototypeToLocaleUpperCase)       \
  /* (obsolete) Unibrow version */            \
  CPP(StringPrototypeToLowerCase)             \
  /* (obsolete) Unibrow version */            \
  CPP(StringPrototypeToUpperCase)
#endif  // V8_INTL_SUPPORT

#define BUILTIN_LIST(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, BCH, ASM) \
  BUILTIN_LIST_BASE(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, ASM)       \
  BUILTIN_LIST_FROM_TORQUE(CPP, TFJ, TFC, TFS, TFH, ASM)          \
  BUILTIN_LIST_INTL(CPP, TFJ, TFS)                                \
  BUILTIN_LIST_BYTECODE_HANDLERS(BCH)

// See the comment on top of BUILTIN_LIST_BASE_TIER0 for an explanation of
// tiers.
#define BUILTIN_LIST_TIER0(CPP, TFJ, TFC, TFS, TFH, BCH, ASM) \
  BUILTIN_LIST_BASE_TIER0(CPP, TFJ, TFC, TFS, TFH, ASM)

#define BUILTIN_LIST_TIER1(CPP, TSJ, TFJ, TFC, TFS, TFH, BCH, ASM) \
  BUILTIN_LIST_BASE_TIER1(CPP, TSJ, TFJ, TFC, TFS, TFH, ASM)       \
  BUILTIN_LIST_FROM_TORQUE(CPP, TFJ, TFC, TFS, TFH, ASM)           \
  BUILTIN_LIST_INTL(CPP, TFJ, TFS)                                 \
  BUILTIN_LIST_BYTECODE_HANDLERS(BCH)

// The exception thrown in the following builtins are caught
// internally and result in a promise rejection.
#define BUILTIN_PROMISE_REJECTION_PREDICTION_LIST(V) \
  V(AsyncFromSyncIteratorPrototypeNext)              \
  V(AsyncFromSyncIteratorPrototypeReturn)            \
  V(AsyncFromSyncIteratorPrototypeThrow)             \
  V(AsyncFunctionAwait)                              \
  V(AsyncGeneratorResolve)                           \
  V(AsyncGeneratorAwait)                             \
  V(PromiseAll)                                      \
  V(PromiseAny)                                      \
  V(PromiseConstructor)                              \
  V(PromiseConstructorLazyDeoptContinuation)         \
  V(PromiseFulfillReactionJob)                       \
  V(PromiseRejectReactionJob)                        \
  V(PromiseRace)                                     \
  V(ResolvePromise)

#define IGNORE_BUILTIN(...)

#define BUILTIN_LIST_C(V)                                                      \
  BUILTIN_LIST(V, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN,              \
               IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_TSJ(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, V, IGNORE_BUILTIN, IGNORE_BUILTIN,              \
               IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_TFJ(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, V, IGNORE_BUILTIN,              \
               IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_TSC(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, V,              \
               IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_TFC(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               V, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN,              \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_TFS(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, V, IGNORE_BUILTIN, IGNORE_BUILTIN,              \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_TFH(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, IGNORE_BUILTIN, V, IGNORE_BUILTIN,              \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_BCH(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, V,              \
               IGNORE_BUILTIN)

#define BUILTIN_LIST_A(V)                                                      \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               V)

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_DEFINITIONS_H_
