// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_DEFINITIONS_H_
#define V8_BUILTINS_BUILTINS_DEFINITIONS_H_

#include "builtins-generated/bytecodes-builtins-list.h"
#include "src/common/globals.h"

// include generated header
#include "torque-generated/builtin-definitions.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS
// EXPAND is needed to work around MSVC's broken __VA_ARGS__ expansion.
#define IF_TSA(TSA_MACRO, CSA_MACRO, ...) EXPAND(TSA_MACRO(__VA_ARGS__))
#else
// EXPAND is needed to work around MSVC's broken __VA_ARGS__ expansion.
#define IF_TSA(TSA_MACRO, CSA_MACRO, ...) EXPAND(CSA_MACRO(__VA_ARGS__))
#endif

// CPP: Builtin in C++. Entered via BUILTIN_EXIT frame.
//      Args: name, formal parameter count
// TFJ: Builtin in Turbofan, with JS linkage (callable as Javascript function).
//      Args: name, formal parameter count, explicit argument names...
// TSJ: Builtin in Turboshaft, with JS linkage (callable as Javascript
//      function).
//      Args: name, formal parameter count, explicit argument names...
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
#define BUILTIN_LIST_BASE_TIER0(CPP, TFJ, TFC, TFS, TFH, ASM)               \
  /* Deoptimization entries. */                                             \
  ASM(DeoptimizationEntry_Eager, DeoptimizationEntry)                       \
  ASM(DeoptimizationEntry_Lazy, DeoptimizationEntry)                        \
                                                                            \
  /* GC write barrier. */                                                   \
  TFC(RecordWriteSaveFP, WriteBarrier)                                      \
  TFC(RecordWriteIgnoreFP, WriteBarrier)                                    \
  TFC(EphemeronKeyBarrierSaveFP, WriteBarrier)                              \
  TFC(EphemeronKeyBarrierIgnoreFP, WriteBarrier)                            \
                                                                            \
  /* TODO(ishell): dummy builtin added here just to keep the Tier0 table */ \
  /* size unmodified to avoid unexpected performance implications. */       \
  /* It should be removed. */                                               \
  CPP(DummyBuiltin, kDontAdaptArgumentsSentinel)

#ifdef V8_ENABLE_LEAPTIERING

/* Tiering related builtins
 *
 * These builtins are used for tiering. Some special conventions apply. They,
 * - can be passed to the JSDispatchTable::SetTieringRequest to be executed
 *   instead of the actual JSFunction's code.
 * - need to uninstall themselves using JSDispatchTable::ResetTieringRequest.
 * - need to tail call the actual JSFunction's code.
 *
 * Also, there are lifecycle considerations since the tiering requests are
 * mutually exclusive.
 *
 * For RCS the optimizing builtins should include the work `Optimize` in their
 * name (see tools/callstats_groups.py).
 *
 * */
#define BUILTIN_LIST_BASE_TIERING(TFC)             \
  TFC(FunctionLogNextExecution, JSTrampoline)      \
  TFC(StartMaglevOptimizeJob, JSTrampoline)        \
  TFC(StartTurbofanOptimizeJob, JSTrampoline)      \
  TFC(OptimizeMaglevEager, JSTrampoline)           \
  TFC(OptimizeTurbofanEager, JSTrampoline)         \
  TFC(MarkReoptimizeLazyDeoptimized, JSTrampoline) \
  TFC(MarkLazyDeoptimized, JSTrampoline)

#else

#define BUILTIN_LIST_BASE_TIERING(TFC)                       \
  /* TODO(saelo): should this use a different descriptor? */ \
  TFC(CompileLazyDeoptimizedCode, JSTrampoline)

#endif

#define BUILTIN_LIST_BASE_TIER1(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, ASM)        \
  /* GC write barriers */                                                      \
  TFC(IndirectPointerBarrierSaveFP, IndirectPointerWriteBarrier)               \
  TFC(IndirectPointerBarrierIgnoreFP, IndirectPointerWriteBarrier)             \
                                                                               \
  /* Adaptors for CPP builtins with various formal parameter counts. */        \
  /* We split these versions for simplicity (not all architectures have */     \
  /* enough registers for extra CEntry arguments) and speculatively for */     \
  /* performance reasons. */                                                   \
  TFC(AdaptorWithBuiltinExitFrame0, CppBuiltinAdaptor)                         \
  TFC(AdaptorWithBuiltinExitFrame1, CppBuiltinAdaptor)                         \
  TFC(AdaptorWithBuiltinExitFrame2, CppBuiltinAdaptor)                         \
  TFC(AdaptorWithBuiltinExitFrame3, CppBuiltinAdaptor)                         \
  TFC(AdaptorWithBuiltinExitFrame4, CppBuiltinAdaptor)                         \
  TFC(AdaptorWithBuiltinExitFrame5, CppBuiltinAdaptor)                         \
                                                                               \
  /* TSAN support for stores in generated code. */                             \
  IF_TSAN(TFC, TSANRelaxedStore8IgnoreFP, TSANStore)                           \
  IF_TSAN(TFC, TSANRelaxedStore8SaveFP, TSANStore)                             \
  IF_TSAN(TFC, TSANRelaxedStore16IgnoreFP, TSANStore)                          \
  IF_TSAN(TFC, TSANRelaxedStore16SaveFP, TSANStore)                            \
  IF_TSAN(TFC, TSANRelaxedStore32IgnoreFP, TSANStore)                          \
  IF_TSAN(TFC, TSANRelaxedStore32SaveFP, TSANStore)                            \
  IF_TSAN(TFC, TSANRelaxedStore64IgnoreFP, TSANStore)                          \
  IF_TSAN(TFC, TSANRelaxedStore64SaveFP, TSANStore)                            \
  IF_TSAN(TFC, TSANSeqCstStore8IgnoreFP, TSANStore)                            \
  IF_TSAN(TFC, TSANSeqCstStore8SaveFP, TSANStore)                              \
  IF_TSAN(TFC, TSANSeqCstStore16IgnoreFP, TSANStore)                           \
  IF_TSAN(TFC, TSANSeqCstStore16SaveFP, TSANStore)                             \
  IF_TSAN(TFC, TSANSeqCstStore32IgnoreFP, TSANStore)                           \
  IF_TSAN(TFC, TSANSeqCstStore32SaveFP, TSANStore)                             \
  IF_TSAN(TFC, TSANSeqCstStore64IgnoreFP, TSANStore)                           \
  IF_TSAN(TFC, TSANSeqCstStore64SaveFP, TSANStore)                             \
                                                                               \
  /* TSAN support for loads in generated code. */                              \
  IF_TSAN(TFC, TSANRelaxedLoad32IgnoreFP, TSANLoad)                            \
  IF_TSAN(TFC, TSANRelaxedLoad32SaveFP, TSANLoad)                              \
  IF_TSAN(TFC, TSANRelaxedLoad64IgnoreFP, TSANLoad)                            \
  IF_TSAN(TFC, TSANRelaxedLoad64SaveFP, TSANLoad)                              \
                                                                               \
  /* Calls */                                                                  \
  /* ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList) */              \
  ASM(CallFunction_ReceiverIsNullOrUndefined, CallTrampoline)                  \
  ASM(CallFunction_ReceiverIsNotNullOrUndefined, CallTrampoline)               \
  ASM(CallFunction_ReceiverIsAny, CallTrampoline)                              \
  /* ES6 section 9.4.1.1 [[Call]] ( thisArgument, argumentsList) */            \
  ASM(CallBoundFunction, CallTrampoline)                                       \
  /* #sec-wrapped-function-exotic-objects-call-thisargument-argumentslist */   \
  TFC(CallWrappedFunction, CallTrampoline)                                     \
  /* ES6 section 7.3.12 Call(F, V, [argumentsList]) */                         \
  ASM(Call_ReceiverIsNullOrUndefined, CallTrampoline)                          \
  ASM(Call_ReceiverIsNotNullOrUndefined, CallTrampoline)                       \
  ASM(Call_ReceiverIsAny, CallTrampoline)                                      \
  TFC(Call_ReceiverIsNullOrUndefined_Baseline_Compact,                         \
      CallTrampoline_Baseline_Compact)                                         \
  TFC(Call_ReceiverIsNullOrUndefined_Baseline, CallTrampoline_Baseline)        \
  TFC(Call_ReceiverIsNotNullOrUndefined_Baseline_Compact,                      \
      CallTrampoline_Baseline_Compact)                                         \
  TFC(Call_ReceiverIsNotNullOrUndefined_Baseline, CallTrampoline_Baseline)     \
  TFC(Call_ReceiverIsAny_Baseline_Compact, CallTrampoline_Baseline_Compact)    \
  TFC(Call_ReceiverIsAny_Baseline, CallTrampoline_Baseline)                    \
  TFC(Call_ReceiverIsNullOrUndefined_WithFeedback,                             \
      CallTrampoline_WithFeedback)                                             \
  TFC(Call_ReceiverIsNotNullOrUndefined_WithFeedback,                          \
      CallTrampoline_WithFeedback)                                             \
  TFC(Call_ReceiverIsAny_WithFeedback, CallTrampoline_WithFeedback)            \
                                                                               \
  /* ES6 section 9.5.12[[Call]] ( thisArgument, argumentsList ) */             \
  TFC(CallProxy, CallTrampoline)                                               \
  ASM(CallVarargs, CallVarargs)                                                \
  TFC(CallWithSpread, CallWithSpread)                                          \
  TFC(CallWithSpread_Baseline, CallWithSpread_Baseline)                        \
  TFC(CallWithSpread_WithFeedback, CallWithSpread_WithFeedback)                \
  TFC(CallWithArrayLike, CallWithArrayLike)                                    \
  TFC(CallWithArrayLike_WithFeedback, CallWithArrayLike_WithFeedback)          \
  ASM(CallForwardVarargs, CallForwardVarargs)                                  \
  ASM(CallFunctionForwardVarargs, CallForwardVarargs)                          \
  /* Call an API callback via a {FunctionTemplateInfo}, doing appropriate */   \
  /* access and compatible receiver checks. */                                 \
  TFC(CallFunctionTemplate_Generic, CallFunctionTemplateGeneric)               \
  TFC(CallFunctionTemplate_CheckAccess, CallFunctionTemplate)                  \
  TFC(CallFunctionTemplate_CheckCompatibleReceiver, CallFunctionTemplate)      \
  TFC(CallFunctionTemplate_CheckAccessAndCompatibleReceiver,                   \
      CallFunctionTemplate)                                                    \
                                                                               \
  /* Construct */                                                              \
  /* ES6 section 9.2.2 [[Construct]] ( argumentsList, newTarget) */            \
  ASM(ConstructFunction, JSTrampoline)                                         \
  /* ES6 section 9.4.1.2 [[Construct]] (argumentsList, newTarget) */           \
  ASM(ConstructBoundFunction, JSTrampoline)                                    \
  ASM(ConstructedNonConstructable, JSTrampoline)                               \
  /* ES6 section 7.3.13 Construct (F, [argumentsList], [newTarget]) */         \
  ASM(Construct, ConstructStub)                                                \
  ASM(ConstructVarargs, ConstructVarargs)                                      \
  TFC(ConstructWithSpread, ConstructWithSpread)                                \
  TFC(ConstructWithSpread_Baseline, ConstructWithSpread_Baseline)              \
  TFC(ConstructWithSpread_WithFeedback, ConstructWithSpread_WithFeedback)      \
  TFC(ConstructWithArrayLike, ConstructWithArrayLike)                          \
  ASM(ConstructForwardVarargs, ConstructForwardVarargs)                        \
  ASM(ConstructForwardAllArgs, ConstructForwardAllArgs)                        \
  TFC(ConstructForwardAllArgs_Baseline, ConstructForwardAllArgs_Baseline)      \
  TFC(ConstructForwardAllArgs_WithFeedback,                                    \
      ConstructForwardAllArgs_WithFeedback)                                    \
  ASM(ConstructFunctionForwardVarargs, ConstructForwardVarargs)                \
  TFC(Construct_Baseline, Construct_Baseline)                                  \
  TFC(Construct_WithFeedback, Construct_WithFeedback)                          \
  ASM(JSConstructStubGeneric, ConstructStub)                                   \
  ASM(JSBuiltinsConstructStub, ConstructStub)                                  \
  TFC(FastNewObject, FastNewObject)                                            \
  TFS(FastNewClosure, NeedsContext::kYes, kSharedFunctionInfo, kFeedbackCell)  \
  /* ES6 section 9.5.14 [[Construct]] ( argumentsList, newTarget) */           \
  TFC(ConstructProxy, JSTrampoline)                                            \
                                                                               \
  /* Apply and entries */                                                      \
  ASM(JSEntry, JSEntry)                                                        \
  ASM(JSConstructEntry, JSEntry)                                               \
  ASM(JSRunMicrotasksEntry, RunMicrotasksEntry)                                \
  /* Call a JSValue. */                                                        \
  ASM(JSEntryTrampoline, JSEntry)                                              \
  /* Construct a JSValue. */                                                   \
  ASM(JSConstructEntryTrampoline, JSEntry)                                     \
  ASM(ResumeGeneratorTrampoline, ResumeGenerator)                              \
                                                                               \
  /* String helpers */                                                         \
  IF_TSA(TSC, TFC, StringFromCodePointAt, StringAtAsString)                    \
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
  ASM(InterpreterOnStackReplacement_ToBaseline, Void)                          \
                                                                               \
  /* Maglev Compiler */                                                        \
  ASM(MaglevFunctionEntryStackCheck_WithoutNewTarget, Void)                    \
  ASM(MaglevFunctionEntryStackCheck_WithNewTarget, Void)                       \
  ASM(MaglevOptimizeCodeOrTailCallOptimizedCodeSlot,                           \
      MaglevOptimizeCodeOrTailCallOptimizedCodeSlot)                           \
                                                                               \
  /* Code life-cycle */                                                        \
  TFC(CompileLazy, JSTrampoline)                                               \
  TFC(InstantiateAsmJs, JSTrampoline)                                          \
  ASM(NotifyDeoptimized, Void)                                                 \
                                                                               \
  BUILTIN_LIST_BASE_TIERING(TFC)                                               \
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
  CPP(HandleApiConstruct, kDontAdaptArgumentsSentinel)                         \
  CPP(HandleApiCallAsFunctionDelegate, kDontAdaptArgumentsSentinel)            \
  CPP(HandleApiCallAsConstructorDelegate, kDontAdaptArgumentsSentinel)         \
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
  CPP(EmptyFunction, kDontAdaptArgumentsSentinel)                              \
  CPP(EmptyFunction1, JSParameterCount(1))                                     \
  CPP(Illegal, kDontAdaptArgumentsSentinel)                                    \
  CPP(IllegalInvocationThrower, kDontAdaptArgumentsSentinel)                   \
  CPP(StrictPoisonPillThrower, JSParameterCount(0))                            \
  CPP(UnsupportedThrower, kDontAdaptArgumentsSentinel)                         \
  TFJ(ReturnReceiver, kJSArgcReceiverSlots, kReceiver)                         \
                                                                               \
  /* AbstractModuleSource */                                                   \
  CPP(AbstractModuleSourceToStringTag, JSParameterCount(0))                    \
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
  CPP(ArrayConcat, kDontAdaptArgumentsSentinel)                                \
  /* ES6 #sec-array.prototype.fill */                                          \
  CPP(ArrayPrototypeFill, kDontAdaptArgumentsSentinel)                         \
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
  CPP(ArrayPop, kDontAdaptArgumentsSentinel)                                   \
  TFJ(ArrayPrototypePop, kDontAdaptArgumentsSentinel)                          \
  /* ES6 #sec-array.prototype.push */                                          \
  CPP(ArrayPush, kDontAdaptArgumentsSentinel)                                  \
  TFJ(ArrayPrototypePush, kDontAdaptArgumentsSentinel)                         \
  /* ES6 #sec-array.prototype.shift */                                         \
  CPP(ArrayShift, kDontAdaptArgumentsSentinel)                                 \
  /* ES6 #sec-array.prototype.unshift */                                       \
  CPP(ArrayUnshift, kDontAdaptArgumentsSentinel)                               \
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
  CPP(ArrayBufferConstructor, JSParameterCount(1))                             \
  CPP(ArrayBufferConstructor_DoNotInitialize, kDontAdaptArgumentsSentinel)     \
  CPP(ArrayBufferPrototypeSlice, JSParameterCount(2))                          \
  /* https://tc39.es/proposal-resizablearraybuffer/ */                         \
  CPP(ArrayBufferPrototypeResize, JSParameterCount(1))                         \
  /* https://tc39.es/proposal-arraybuffer-transfer/ */                         \
  CPP(ArrayBufferPrototypeTransfer, kDontAdaptArgumentsSentinel)               \
  CPP(ArrayBufferPrototypeTransferToFixedLength, kDontAdaptArgumentsSentinel)  \
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
  CPP(BigIntConstructor, kDontAdaptArgumentsSentinel)                          \
  CPP(BigIntAsUintN, kDontAdaptArgumentsSentinel)                              \
  CPP(BigIntAsIntN, kDontAdaptArgumentsSentinel)                               \
  CPP(BigIntPrototypeToLocaleString, kDontAdaptArgumentsSentinel)              \
  CPP(BigIntPrototypeToString, kDontAdaptArgumentsSentinel)                    \
  CPP(BigIntPrototypeValueOf, kDontAdaptArgumentsSentinel)                     \
                                                                               \
  /* CallSite */                                                               \
  CPP(CallSitePrototypeGetColumnNumber, JSParameterCount(0))                   \
  CPP(CallSitePrototypeGetEnclosingColumnNumber, JSParameterCount(0))          \
  CPP(CallSitePrototypeGetEnclosingLineNumber, JSParameterCount(0))            \
  CPP(CallSitePrototypeGetEvalOrigin, JSParameterCount(0))                     \
  CPP(CallSitePrototypeGetFileName, JSParameterCount(0))                       \
  CPP(CallSitePrototypeGetFunction, JSParameterCount(0))                       \
  CPP(CallSitePrototypeGetFunctionName, JSParameterCount(0))                   \
  CPP(CallSitePrototypeGetLineNumber, JSParameterCount(0))                     \
  CPP(CallSitePrototypeGetMethodName, JSParameterCount(0))                     \
  CPP(CallSitePrototypeGetPosition, JSParameterCount(0))                       \
  CPP(CallSitePrototypeGetPromiseIndex, JSParameterCount(0))                   \
  CPP(CallSitePrototypeGetScriptHash, JSParameterCount(0))                     \
  CPP(CallSitePrototypeGetScriptNameOrSourceURL, JSParameterCount(0))          \
  CPP(CallSitePrototypeGetThis, JSParameterCount(0))                           \
  CPP(CallSitePrototypeGetTypeName, JSParameterCount(0))                       \
  CPP(CallSitePrototypeIsAsync, JSParameterCount(0))                           \
  CPP(CallSitePrototypeIsConstructor, JSParameterCount(0))                     \
  CPP(CallSitePrototypeIsEval, JSParameterCount(0))                            \
  CPP(CallSitePrototypeIsNative, JSParameterCount(0))                          \
  CPP(CallSitePrototypeIsPromiseAll, JSParameterCount(0))                      \
  CPP(CallSitePrototypeIsToplevel, JSParameterCount(0))                        \
  CPP(CallSitePrototypeToString, JSParameterCount(0))                          \
                                                                               \
  /* Console */                                                                \
  CPP(ConsoleDebug, kDontAdaptArgumentsSentinel)                               \
  CPP(ConsoleError, kDontAdaptArgumentsSentinel)                               \
  CPP(ConsoleInfo, kDontAdaptArgumentsSentinel)                                \
  CPP(ConsoleLog, kDontAdaptArgumentsSentinel)                                 \
  CPP(ConsoleWarn, kDontAdaptArgumentsSentinel)                                \
  CPP(ConsoleDir, kDontAdaptArgumentsSentinel)                                 \
  CPP(ConsoleDirXml, kDontAdaptArgumentsSentinel)                              \
  CPP(ConsoleTable, kDontAdaptArgumentsSentinel)                               \
  CPP(ConsoleTrace, kDontAdaptArgumentsSentinel)                               \
  CPP(ConsoleGroup, kDontAdaptArgumentsSentinel)                               \
  CPP(ConsoleGroupCollapsed, kDontAdaptArgumentsSentinel)                      \
  CPP(ConsoleGroupEnd, kDontAdaptArgumentsSentinel)                            \
  CPP(ConsoleClear, kDontAdaptArgumentsSentinel)                               \
  CPP(ConsoleCount, kDontAdaptArgumentsSentinel)                               \
  CPP(ConsoleCountReset, kDontAdaptArgumentsSentinel)                          \
  CPP(ConsoleAssert, kDontAdaptArgumentsSentinel)                              \
  CPP(ConsoleProfile, kDontAdaptArgumentsSentinel)                             \
  CPP(ConsoleProfileEnd, kDontAdaptArgumentsSentinel)                          \
  CPP(ConsoleTime, kDontAdaptArgumentsSentinel)                                \
  CPP(ConsoleTimeLog, kDontAdaptArgumentsSentinel)                             \
  CPP(ConsoleTimeEnd, kDontAdaptArgumentsSentinel)                             \
  CPP(ConsoleTimeStamp, kDontAdaptArgumentsSentinel)                           \
  CPP(ConsoleContext, kDontAdaptArgumentsSentinel)                             \
                                                                               \
  /* DataView */                                                               \
  /* ES #sec-dataview-constructor */                                           \
  CPP(DataViewConstructor, kDontAdaptArgumentsSentinel)                        \
                                                                               \
  /* Date */                                                                   \
  /* ES #sec-date-constructor */                                               \
  CPP(DateConstructor, kDontAdaptArgumentsSentinel)                            \
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
  CPP(DatePrototypeGetYear, JSParameterCount(0))                               \
  CPP(DatePrototypeSetYear, kDontAdaptArgumentsSentinel)                       \
  CPP(DateNow, kDontAdaptArgumentsSentinel)                                    \
  CPP(DateParse, kDontAdaptArgumentsSentinel)                                  \
  CPP(DatePrototypeSetDate, kDontAdaptArgumentsSentinel)                       \
  CPP(DatePrototypeSetFullYear, kDontAdaptArgumentsSentinel)                   \
  CPP(DatePrototypeSetHours, kDontAdaptArgumentsSentinel)                      \
  CPP(DatePrototypeSetMilliseconds, kDontAdaptArgumentsSentinel)               \
  CPP(DatePrototypeSetMinutes, kDontAdaptArgumentsSentinel)                    \
  CPP(DatePrototypeSetMonth, kDontAdaptArgumentsSentinel)                      \
  CPP(DatePrototypeSetSeconds, kDontAdaptArgumentsSentinel)                    \
  CPP(DatePrototypeSetTime, kDontAdaptArgumentsSentinel)                       \
  CPP(DatePrototypeSetUTCDate, kDontAdaptArgumentsSentinel)                    \
  CPP(DatePrototypeSetUTCFullYear, kDontAdaptArgumentsSentinel)                \
  CPP(DatePrototypeSetUTCHours, kDontAdaptArgumentsSentinel)                   \
  CPP(DatePrototypeSetUTCMilliseconds, kDontAdaptArgumentsSentinel)            \
  CPP(DatePrototypeSetUTCMinutes, kDontAdaptArgumentsSentinel)                 \
  CPP(DatePrototypeSetUTCMonth, kDontAdaptArgumentsSentinel)                   \
  CPP(DatePrototypeSetUTCSeconds, kDontAdaptArgumentsSentinel)                 \
  CPP(DatePrototypeToDateString, kDontAdaptArgumentsSentinel)                  \
  CPP(DatePrototypeToISOString, kDontAdaptArgumentsSentinel)                   \
  CPP(DatePrototypeToUTCString, kDontAdaptArgumentsSentinel)                   \
  CPP(DatePrototypeToString, kDontAdaptArgumentsSentinel)                      \
  CPP(DatePrototypeToTimeString, kDontAdaptArgumentsSentinel)                  \
  CPP(DatePrototypeToJson, kDontAdaptArgumentsSentinel)                        \
  CPP(DateUTC, kDontAdaptArgumentsSentinel)                                    \
                                                                               \
  /* DisposabeStack*/                                                          \
  CPP(DisposableStackConstructor, kDontAdaptArgumentsSentinel)                 \
  CPP(DisposableStackPrototypeUse, JSParameterCount(1))                        \
  CPP(DisposableStackPrototypeDispose, JSParameterCount(0))                    \
  CPP(DisposableStackPrototypeGetDisposed, JSParameterCount(0))                \
  CPP(DisposableStackPrototypeAdopt, JSParameterCount(2))                      \
  CPP(DisposableStackPrototypeDefer, JSParameterCount(1))                      \
  CPP(DisposableStackPrototypeMove, JSParameterCount(0))                       \
                                                                               \
  /* Async DisposabeStack*/                                                    \
  CPP(AsyncDisposableStackOnFulfilled, JSParameterCount(0))                    \
  CPP(AsyncDisposableStackOnRejected, JSParameterCount(0))                     \
  CPP(AsyncDisposeFromSyncDispose, JSParameterCount(0))                        \
  CPP(AsyncDisposableStackConstructor, kDontAdaptArgumentsSentinel)            \
  CPP(AsyncDisposableStackPrototypeUse, JSParameterCount(1))                   \
  CPP(AsyncDisposableStackPrototypeDisposeAsync, JSParameterCount(0))          \
  CPP(AsyncDisposableStackPrototypeGetDisposed, JSParameterCount(0))           \
  CPP(AsyncDisposableStackPrototypeAdopt, JSParameterCount(2))                 \
  CPP(AsyncDisposableStackPrototypeDefer, JSParameterCount(1))                 \
  CPP(AsyncDisposableStackPrototypeMove, JSParameterCount(0))                  \
                                                                               \
  /* Error */                                                                  \
  CPP(ErrorConstructor, kDontAdaptArgumentsSentinel)                           \
  CPP(ErrorCaptureStackTrace, kDontAdaptArgumentsSentinel)                     \
  CPP(ErrorPrototypeToString, JSParameterCount(0))                             \
  CPP(ErrorIsError, JSParameterCount(1))                                       \
                                                                               \
  /* Function */                                                               \
  CPP(FunctionConstructor, kDontAdaptArgumentsSentinel)                        \
  ASM(FunctionPrototypeApply, JSTrampoline)                                    \
  CPP(FunctionPrototypeBind, kDontAdaptArgumentsSentinel)                      \
  IF_WASM(CPP, WebAssemblyFunctionPrototypeBind, kDontAdaptArgumentsSentinel)  \
  ASM(FunctionPrototypeCall, JSTrampoline)                                     \
  /* ES6 #sec-function.prototype.tostring */                                   \
  CPP(FunctionPrototypeToString, kDontAdaptArgumentsSentinel)                  \
                                                                               \
  /* Belongs to Objects but is a dependency of GeneratorPrototypeResume */     \
  TFS(CreateIterResultObject, NeedsContext::kYes, kValue, kDone)               \
                                                                               \
  /* Generator and Async */                                                    \
  TFS(CreateGeneratorObject, NeedsContext::kYes, kClosure, kReceiver)          \
  CPP(GeneratorFunctionConstructor, kDontAdaptArgumentsSentinel)               \
  /* ES6 #sec-generator.prototype.next */                                      \
  TFJ(GeneratorPrototypeNext, kDontAdaptArgumentsSentinel)                     \
  /* ES6 #sec-generator.prototype.return */                                    \
  TFJ(GeneratorPrototypeReturn, kDontAdaptArgumentsSentinel)                   \
  /* ES6 #sec-generator.prototype.throw */                                     \
  TFJ(GeneratorPrototypeThrow, kDontAdaptArgumentsSentinel)                    \
  CPP(AsyncFunctionConstructor, kDontAdaptArgumentsSentinel)                   \
  TFC(SuspendGeneratorBaseline, SuspendGeneratorBaseline)                      \
  TFC(ResumeGeneratorBaseline, ResumeGeneratorBaseline)                        \
                                                                               \
  /* Iterator Protocol */                                                      \
  TFC(GetIteratorWithFeedbackLazyDeoptContinuation, GetIteratorStackParameter) \
  TFC(CallIteratorWithFeedbackLazyDeoptContinuation, SingleParameterOnStack)   \
                                                                               \
  /* Global object */                                                          \
  CPP(GlobalDecodeURI, kDontAdaptArgumentsSentinel)                            \
  CPP(GlobalDecodeURIComponent, kDontAdaptArgumentsSentinel)                   \
  CPP(GlobalEncodeURI, kDontAdaptArgumentsSentinel)                            \
  CPP(GlobalEncodeURIComponent, kDontAdaptArgumentsSentinel)                   \
  CPP(GlobalEscape, kDontAdaptArgumentsSentinel)                               \
  CPP(GlobalUnescape, kDontAdaptArgumentsSentinel)                             \
  CPP(GlobalEval, kDontAdaptArgumentsSentinel)                                 \
  /* ES6 #sec-isfinite-number */                                               \
  TFJ(GlobalIsFinite, kJSArgcReceiverSlots + 1, kReceiver, kNumber)            \
  /* ES6 #sec-isnan-number */                                                  \
  TFJ(GlobalIsNaN, kJSArgcReceiverSlots + 1, kReceiver, kNumber)               \
                                                                               \
  /* JSON */                                                                   \
  CPP(JsonParse, kDontAdaptArgumentsSentinel)                                  \
  CPP(JsonStringify, JSParameterCount(3))                                      \
  CPP(JsonRawJson, JSParameterCount(1))                                        \
  CPP(JsonIsRawJson, JSParameterCount(1))                                      \
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
  TFH(LookupScriptContextTrampoline, LookupTrampoline)                         \
  TFH(LookupContextBaseline, LookupBaseline)                                   \
  TFH(LookupScriptContextBaseline, LookupBaseline)                             \
  TFH(LookupContextInsideTypeofTrampoline, LookupTrampoline)                   \
  TFH(LookupScriptContextInsideTypeofTrampoline, LookupTrampoline)             \
  TFH(LookupContextInsideTypeofBaseline, LookupBaseline)                       \
  TFH(LookupScriptContextInsideTypeofBaseline, LookupBaseline)                 \
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
  CPP(MapPrototypeClear, JSParameterCount(0))                                  \
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
  CPP(NumberPrototypeToExponential, kDontAdaptArgumentsSentinel)               \
  CPP(NumberPrototypeToFixed, kDontAdaptArgumentsSentinel)                     \
  CPP(NumberPrototypeToLocaleString, kDontAdaptArgumentsSentinel)              \
  CPP(NumberPrototypeToPrecision, kDontAdaptArgumentsSentinel)                 \
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
  IF_TSA(TSC, TFC, BitwiseNot_WithFeedback, UnaryOp_WithFeedback)              \
  TFC(Decrement_WithFeedback, UnaryOp_WithFeedback)                            \
  TFC(Increment_WithFeedback, UnaryOp_WithFeedback)                            \
  TFC(Negate_WithFeedback, UnaryOp_WithFeedback)                               \
                                                                               \
  /* Object */                                                                 \
  /* ES #sec-object-constructor */                                             \
  TFJ(ObjectAssign, kDontAdaptArgumentsSentinel)                               \
  /* ES #sec-object.create */                                                  \
  TFJ(ObjectCreate, kDontAdaptArgumentsSentinel)                               \
  CPP(ObjectDefineGetter, JSParameterCount(2))                                 \
  CPP(ObjectDefineProperties, JSParameterCount(2))                             \
  CPP(ObjectDefineProperty, JSParameterCount(3))                               \
  CPP(ObjectDefineSetter, JSParameterCount(2))                                 \
  TFJ(ObjectEntries, kJSArgcReceiverSlots + 1, kReceiver, kObject)             \
  CPP(ObjectFreeze, kDontAdaptArgumentsSentinel)                               \
  TFJ(ObjectGetOwnPropertyDescriptor, kDontAdaptArgumentsSentinel)             \
  CPP(ObjectGetOwnPropertyDescriptors, kDontAdaptArgumentsSentinel)            \
  TFJ(ObjectGetOwnPropertyNames, kJSArgcReceiverSlots + 1, kReceiver, kObject) \
  CPP(ObjectGetOwnPropertySymbols, kDontAdaptArgumentsSentinel)                \
  TFJ(ObjectHasOwn, kJSArgcReceiverSlots + 2, kReceiver, kObject, kKey)        \
  TFJ(ObjectIs, kJSArgcReceiverSlots + 2, kReceiver, kLeft, kRight)            \
  CPP(ObjectIsFrozen, kDontAdaptArgumentsSentinel)                             \
  CPP(ObjectIsSealed, kDontAdaptArgumentsSentinel)                             \
  TFJ(ObjectKeys, kJSArgcReceiverSlots + 1, kReceiver, kObject)                \
  CPP(ObjectLookupGetter, JSParameterCount(1))                                 \
  CPP(ObjectLookupSetter, JSParameterCount(1))                                 \
  /* ES6 #sec-object.prototype.hasownproperty */                               \
  TFJ(ObjectPrototypeHasOwnProperty, kJSArgcReceiverSlots + 1, kReceiver,      \
      kKey)                                                                    \
  TFJ(ObjectPrototypeIsPrototypeOf, kJSArgcReceiverSlots + 1, kReceiver,       \
      kValue)                                                                  \
  CPP(ObjectPrototypePropertyIsEnumerable, kDontAdaptArgumentsSentinel)        \
  CPP(ObjectPrototypeGetProto, JSParameterCount(0))                            \
  CPP(ObjectPrototypeSetProto, JSParameterCount(1))                            \
  CPP(ObjectSeal, kDontAdaptArgumentsSentinel)                                 \
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
  CPP(ReflectDefineProperty, JSParameterCount(3))                              \
  CPP(ReflectOwnKeys, JSParameterCount(1))                                     \
  CPP(ReflectSet, kDontAdaptArgumentsSentinel)                                 \
                                                                               \
  /* RegExp */                                                                 \
  CPP(RegExpCapture1Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture2Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture3Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture4Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture5Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture6Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture7Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture8Getter, JSParameterCount(0))                               \
  CPP(RegExpCapture9Getter, JSParameterCount(0))                               \
  /* ES #sec-regexp-pattern-flags */                                           \
  TFJ(RegExpConstructor, kJSArgcReceiverSlots + 2, kReceiver, kPattern,        \
      kFlags)                                                                  \
  CPP(RegExpInputGetter, JSParameterCount(0))                                  \
  CPP(RegExpInputSetter, JSParameterCount(1))                                  \
  CPP(RegExpLastMatchGetter, JSParameterCount(0))                              \
  CPP(RegExpLastParenGetter, JSParameterCount(0))                              \
  CPP(RegExpLeftContextGetter, JSParameterCount(0))                            \
  /* ES #sec-regexp.prototype.compile */                                       \
  TFJ(RegExpPrototypeCompile, kJSArgcReceiverSlots + 2, kReceiver, kPattern,   \
      kFlags)                                                                  \
  CPP(RegExpPrototypeToString, kDontAdaptArgumentsSentinel)                    \
  CPP(RegExpRightContextGetter, JSParameterCount(0))                           \
                                                                               \
  /* RegExp helpers */                                                         \
  TFS(RegExpExecAtom, NeedsContext::kYes, kRegExp, kString, kLastIndex,        \
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
  CPP(SetPrototypeClear, JSParameterCount(0))                                  \
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
  CPP(ShadowRealmConstructor, kDontAdaptArgumentsSentinel)                     \
  TFS(ShadowRealmGetWrappedValue, NeedsContext::kYes, kCreationContext,        \
      kTargetContext, kValue)                                                  \
  CPP(ShadowRealmPrototypeEvaluate, JSParameterCount(1))                       \
  TFJ(ShadowRealmPrototypeImportValue, kJSArgcReceiverSlots + 2, kReceiver,    \
      kSpecifier, kExportName)                                                 \
  TFJ(ShadowRealmImportValueFulfilled, kJSArgcReceiverSlots + 1, kReceiver,    \
      kExports)                                                                \
  TFJ(ShadowRealmImportValueRejected, kJSArgcReceiverSlots + 1, kReceiver,     \
      kException)                                                              \
                                                                               \
  /* SharedArrayBuffer */                                                      \
  CPP(SharedArrayBufferPrototypeGetByteLength, kDontAdaptArgumentsSentinel)    \
  CPP(SharedArrayBufferPrototypeSlice, JSParameterCount(2))                    \
  /* https://tc39.es/proposal-resizablearraybuffer/ */                         \
  CPP(SharedArrayBufferPrototypeGrow, JSParameterCount(1))                     \
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
  CPP(AtomicsNotify, JSParameterCount(3))                                      \
  CPP(AtomicsIsLockFree, JSParameterCount(1))                                  \
  CPP(AtomicsWait, JSParameterCount(4))                                        \
  CPP(AtomicsWaitAsync, JSParameterCount(4))                                   \
  CPP(AtomicsPause, kDontAdaptArgumentsSentinel)                               \
                                                                               \
  /* String */                                                                 \
  /* ES #sec-string.fromcodepoint */                                           \
  CPP(StringFromCodePoint, kDontAdaptArgumentsSentinel)                        \
  /* ES6 #sec-string.fromcharcode */                                           \
  IF_TSA(TSJ, TFJ, StringFromCharCode, kDontAdaptArgumentsSentinel)            \
  /* ES6 #sec-string.prototype.lastindexof */                                  \
  CPP(StringPrototypeLastIndexOf, kDontAdaptArgumentsSentinel)                 \
  /* ES #sec-string.prototype.matchAll */                                      \
  TFJ(StringPrototypeMatchAll, kJSArgcReceiverSlots + 1, kReceiver, kRegexp)   \
  /* ES6 #sec-string.prototype.replace */                                      \
  TFJ(StringPrototypeReplace, kJSArgcReceiverSlots + 2, kReceiver, kSearch,    \
      kReplace)                                                                \
  /* ES6 #sec-string.prototype.split */                                        \
  TFJ(StringPrototypeSplit, kDontAdaptArgumentsSentinel)                       \
  /* ES6 #sec-string.raw */                                                    \
  CPP(StringRaw, kDontAdaptArgumentsSentinel)                                  \
                                                                               \
  /* Symbol */                                                                 \
  /* ES #sec-symbol-constructor */                                             \
  CPP(SymbolConstructor, kDontAdaptArgumentsSentinel)                          \
  /* ES6 #sec-symbol.for */                                                    \
  CPP(SymbolFor, kDontAdaptArgumentsSentinel)                                  \
  /* ES6 #sec-symbol.keyfor */                                                 \
  CPP(SymbolKeyFor, kDontAdaptArgumentsSentinel)                               \
                                                                               \
  /* TypedArray */                                                             \
  /* ES #sec-typedarray-constructors */                                        \
  TFJ(TypedArrayBaseConstructor, kJSArgcReceiverSlots, kReceiver)              \
  TFJ(TypedArrayConstructor, kDontAdaptArgumentsSentinel)                      \
  CPP(TypedArrayPrototypeBuffer, kDontAdaptArgumentsSentinel)                  \
  /* ES6 #sec-get-%typedarray%.prototype.bytelength */                         \
  TFJ(TypedArrayPrototypeByteLength, kJSArgcReceiverSlots, kReceiver)          \
  /* ES6 #sec-get-%typedarray%.prototype.byteoffset */                         \
  TFJ(TypedArrayPrototypeByteOffset, kJSArgcReceiverSlots, kReceiver)          \
  /* ES6 #sec-get-%typedarray%.prototype.length */                             \
  TFJ(TypedArrayPrototypeLength, kJSArgcReceiverSlots, kReceiver)              \
  /* ES6 #sec-%typedarray%.prototype.copywithin */                             \
  CPP(TypedArrayPrototypeCopyWithin, kDontAdaptArgumentsSentinel)              \
  /* ES6 #sec-%typedarray%.prototype.fill */                                   \
  CPP(TypedArrayPrototypeFill, kDontAdaptArgumentsSentinel)                    \
  /* ES7 #sec-%typedarray%.prototype.includes */                               \
  CPP(TypedArrayPrototypeIncludes, kDontAdaptArgumentsSentinel)                \
  /* ES6 #sec-%typedarray%.prototype.indexof */                                \
  CPP(TypedArrayPrototypeIndexOf, kDontAdaptArgumentsSentinel)                 \
  /* ES6 #sec-%typedarray%.prototype.lastindexof */                            \
  CPP(TypedArrayPrototypeLastIndexOf, kDontAdaptArgumentsSentinel)             \
  /* ES6 #sec-%typedarray%.prototype.reverse */                                \
  CPP(TypedArrayPrototypeReverse, kDontAdaptArgumentsSentinel)                 \
  /* ES6 #sec-get-%typedarray%.prototype-@@tostringtag */                      \
  TFJ(TypedArrayPrototypeToStringTag, kJSArgcReceiverSlots, kReceiver)         \
  /* ES6 %TypedArray%.prototype.map */                                         \
  TFJ(TypedArrayPrototypeMap, kDontAdaptArgumentsSentinel)                     \
  /* proposal-arraybuffer-base64 #sec-uint8array.frombase64 */                 \
  CPP(Uint8ArrayFromBase64, kDontAdaptArgumentsSentinel)                       \
  /* proposal-arraybuffer-base64 #sec-uint8array.prototype.tobase64 */         \
  CPP(Uint8ArrayToBase64, kDontAdaptArgumentsSentinel)                         \
                                                                               \
  /* Wasm */                                                                   \
  IF_WASM_DRUMBRAKE(ASM, WasmInterpreterEntry, WasmDummy)                      \
  IF_WASM_DRUMBRAKE(ASM, GenericJSToWasmInterpreterWrapper, WasmDummy)         \
  IF_WASM_DRUMBRAKE(ASM, WasmInterpreterCWasmEntry, WasmDummy)                 \
  IF_WASM_DRUMBRAKE(ASM, GenericWasmToJSInterpreterWrapper, WasmDummy)         \
                                                                               \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem32S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem32U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_F32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_F64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem32S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem32U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem32S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem32U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_F32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_F64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8S_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8U_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32S_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32U_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadMem_s, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8S_LocalSet_s, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8U_LocalSet_s, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16S_LocalSet_s,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16U_LocalSet_s,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8S_LocalSet_s, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8U_LocalSet_s, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16S_LocalSet_s,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16U_LocalSet_s,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32S_LocalSet_s,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32U_LocalSet_s,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem_LocalSet_s, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem_LocalSet_s, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadMem_LocalSet_s, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadMem_LocalSet_s, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem8_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem16_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem8_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem16_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem32_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem8_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem16_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem8_s, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem16_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem32_s, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64StoreMem_s, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadStoreMem_s, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadStoreMem_s, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32LoadStoreMem_s, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64LoadStoreMem_s, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadStoreMem_s, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadStoreMem_s, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadStoreMem_s, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadStoreMem_s, WasmDummy)       \
                                                                               \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem32S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem32U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_I64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_F32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2r_F64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem32S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem32U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem32S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem32U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_I64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_F32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2r_F64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8S_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8U_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32S_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32U_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadMem_l, WasmDummy)            \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8S_LocalSet_l, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem8U_LocalSet_l, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16S_LocalSet_l,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem16U_LocalSet_l,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8S_LocalSet_l, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem8U_LocalSet_l, WasmDummy) \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16S_LocalSet_l,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem16U_LocalSet_l,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32S_LocalSet_l,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem32U_LocalSet_l,           \
                                  WasmDummy)                                   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadMem_LocalSet_l, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadMem_LocalSet_l, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadMem_LocalSet_l, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadMem_LocalSet_l, WasmDummy)   \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem8_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem16_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem8_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem16_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem32_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem8_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem16_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem8_l, WasmDummy)          \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem16_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem32_l, WasmDummy)         \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64StoreMem_l, WasmDummy)           \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I32LoadStoreMem_l, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_I64LoadStoreMem_l, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F32LoadStoreMem_l, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, r2s_F64LoadStoreMem_l, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I32LoadStoreMem_l, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_I64LoadStoreMem_l, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F32LoadStoreMem_l, WasmDummy)       \
  IF_WASM_DRUMBRAKE_INSTR_HANDLER(ASM, s2s_F64LoadStoreMem_l, WasmDummy)       \
                                                                               \
  IF_WASM(ASM, JSToWasmWrapperAsm, WasmJSToWasmWrapper)                        \
  IF_WASM(ASM, WasmReturnPromiseOnSuspendAsm, WasmJSToWasmWrapper)             \
  IF_WASM(ASM, JSToWasmStressSwitchStacksAsm, WasmJSToWasmWrapper)             \
  IF_WASM(ASM, WasmToJsWrapperAsm, WasmDummy)                                  \
  IF_WASM(TFC, WasmToJsWrapperCSA, WasmToJSWrapper)                            \
  IF_WASM(TFC, WasmToJsWrapperInvalidSig, WasmToJSWrapper)                     \
  IF_WASM(ASM, WasmSuspend, WasmSuspend)                                       \
  IF_WASM(ASM, WasmResume, JSTrampoline)                                       \
  IF_WASM(ASM, WasmReject, JSTrampoline)                                       \
  IF_WASM(ASM, WasmTrapHandlerLandingPad, WasmDummy)                           \
  IF_WASM(ASM, WasmCompileLazy, WasmDummy)                                     \
  IF_WASM(ASM, WasmLiftoffFrameSetup, WasmDummy)                               \
  IF_WASM(ASM, WasmDebugBreak, WasmDummy)                                      \
  IF_WASM(ASM, WasmOnStackReplace, WasmDummy)                                  \
  IF_WASM(ASM, WasmHandleStackOverflow, WasmHandleStackOverflow)               \
  IF_WASM(TFC, WasmFloat32ToNumber, WasmFloat32ToNumber)                       \
  IF_WASM(TFC, WasmFloat64ToNumber, WasmFloat64ToTagged)                       \
  IF_WASM(TFC, WasmFloat64ToString, WasmFloat64ToTagged)                       \
  IF_WASM(TFC, JSToWasmLazyDeoptContinuation, SingleParameterOnStack)          \
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
  CPP(SharedSpaceJSObjectHasInstance, kDontAdaptArgumentsSentinel)             \
  CPP(SharedStructTypeConstructor, kDontAdaptArgumentsSentinel)                \
  CPP(SharedStructTypeIsSharedStruct, JSParameterCount(1))                     \
  CPP(SharedStructConstructor, JSParameterCount(0))                            \
  CPP(SharedArrayConstructor, JSParameterCount(0))                             \
  CPP(SharedArrayIsSharedArray, JSParameterCount(1))                           \
  CPP(AtomicsMutexConstructor, JSParameterCount(0))                            \
  CPP(AtomicsMutexIsMutex, JSParameterCount(1))                                \
  CPP(AtomicsMutexLock, JSParameterCount(2))                                   \
  CPP(AtomicsMutexLockAsync, JSParameterCount(2))                              \
  CPP(AtomicsMutexLockWithTimeout, JSParameterCount(3))                        \
  CPP(AtomicsMutexTryLock, JSParameterCount(2))                                \
  CPP(AtomicsMutexAsyncUnlockResolveHandler, JSParameterCount(1))              \
  CPP(AtomicsMutexAsyncUnlockRejectHandler, JSParameterCount(1))               \
  CPP(AtomicsConditionConstructor, JSParameterCount(0))                        \
  CPP(AtomicsConditionAcquireLock, JSParameterCount(0))                        \
  CPP(AtomicsConditionIsCondition, JSParameterCount(1))                        \
  CPP(AtomicsConditionWait, kDontAdaptArgumentsSentinel)                       \
  CPP(AtomicsConditionNotify, kDontAdaptArgumentsSentinel)                     \
  CPP(AtomicsConditionWaitAsync, kDontAdaptArgumentsSentinel)                  \
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
  CPP(AsyncGeneratorFunctionConstructor, kDontAdaptArgumentsSentinel)          \
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
  IF_SHADOW_STACK(ASM, AdaptShadowStackForDeopt, Void)                         \
                                                                               \
  /* Trace */                                                                  \
  CPP(IsTraceCategoryEnabled, JSParameterCount(1))                             \
  CPP(Trace, JSParameterCount(5))                                              \
                                                                               \
  /* Weak refs */                                                              \
  CPP(FinalizationRegistryUnregister, kDontAdaptArgumentsSentinel)             \
                                                                               \
  /* Async modules */                                                          \
  TFJ(AsyncModuleEvaluate, kDontAdaptArgumentsSentinel)                        \
                                                                               \
  /* CallAsyncModule* are spec anonymyous functions */                         \
  CPP(CallAsyncModuleFulfilled, JSParameterCount(0))                           \
  CPP(CallAsyncModuleRejected, JSParameterCount(0))                            \
                                                                               \
  /* Temporal */                                                               \
  /* Temporal #sec-temporal.now.timezone */                                    \
  CPP(TemporalNowTimeZone, kDontAdaptArgumentsSentinel)                        \
  /* Temporal #sec-temporal.now.instant */                                     \
  CPP(TemporalNowInstant, kDontAdaptArgumentsSentinel)                         \
  /* Temporal #sec-temporal.now.plaindatetime */                               \
  CPP(TemporalNowPlainDateTime, kDontAdaptArgumentsSentinel)                   \
  /* Temporal #sec-temporal.now.plaindatetimeiso */                            \
  CPP(TemporalNowPlainDateTimeISO, kDontAdaptArgumentsSentinel)                \
  /* Temporal #sec-temporal.now.zoneddatetime */                               \
  CPP(TemporalNowZonedDateTime, kDontAdaptArgumentsSentinel)                   \
  /* Temporal #sec-temporal.now.zoneddatetimeiso */                            \
  CPP(TemporalNowZonedDateTimeISO, kDontAdaptArgumentsSentinel)                \
  /* Temporal #sec-temporal.now.plaindate */                                   \
  CPP(TemporalNowPlainDate, kDontAdaptArgumentsSentinel)                       \
  /* Temporal #sec-temporal.now.plaindateiso */                                \
  CPP(TemporalNowPlainDateISO, kDontAdaptArgumentsSentinel)                    \
  /* There are no Temporal.now.plainTime */                                    \
  /* See https://github.com/tc39/proposal-temporal/issues/1540 */              \
  /* Temporal #sec-temporal.now.plaintimeiso */                                \
  CPP(TemporalNowPlainTimeISO, kDontAdaptArgumentsSentinel)                    \
                                                                               \
  /* Temporal.PlaneDate */                                                     \
  /* Temporal #sec-temporal.plaindate */                                       \
  CPP(TemporalPlainDateConstructor, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-temporal.plaindate.from */                                  \
  CPP(TemporalPlainDateFrom, kDontAdaptArgumentsSentinel)                      \
  /* Temporal #sec-temporal.plaindate.compare */                               \
  CPP(TemporalPlainDateCompare, kDontAdaptArgumentsSentinel)                   \
  /* Temporal #sec-get-temporal.plaindate.prototype.calendar */                \
  CPP(TemporalPlainDatePrototypeCalendar, JSParameterCount(0))                 \
  /* Temporal #sec-get-temporal.plaindate.prototype.year */                    \
  CPP(TemporalPlainDatePrototypeYear, JSParameterCount(0))                     \
  /* Temporal #sec-get-temporal.plaindate.prototype.month */                   \
  CPP(TemporalPlainDatePrototypeMonth, JSParameterCount(0))                    \
  /* Temporal #sec-get-temporal.plaindate.prototype.monthcode */               \
  CPP(TemporalPlainDatePrototypeMonthCode, JSParameterCount(0))                \
  /* Temporal #sec-get-temporal.plaindate.prototype.day */                     \
  CPP(TemporalPlainDatePrototypeDay, JSParameterCount(0))                      \
  /* Temporal #sec-get-temporal.plaindate.prototype.dayofweek */               \
  CPP(TemporalPlainDatePrototypeDayOfWeek, JSParameterCount(0))                \
  /* Temporal #sec-get-temporal.plaindate.prototype.dayofyear */               \
  CPP(TemporalPlainDatePrototypeDayOfYear, JSParameterCount(0))                \
  /* Temporal #sec-get-temporal.plaindate.prototype.weekofyear */              \
  CPP(TemporalPlainDatePrototypeWeekOfYear, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.plaindate.prototype.daysinweek */              \
  CPP(TemporalPlainDatePrototypeDaysInWeek, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.plaindate.prototype.daysinmonth */             \
  CPP(TemporalPlainDatePrototypeDaysInMonth, JSParameterCount(0))              \
  /* Temporal #sec-get-temporal.plaindate.prototype.daysinyear */              \
  CPP(TemporalPlainDatePrototypeDaysInYear, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.plaindate.prototype.monthsinyear */            \
  CPP(TemporalPlainDatePrototypeMonthsInYear, JSParameterCount(0))             \
  /* Temporal #sec-get-temporal.plaindate.prototype.inleapyear */              \
  CPP(TemporalPlainDatePrototypeInLeapYear, JSParameterCount(0))               \
  /* Temporal #sec-temporal.plaindate.prototype.toplainyearmonth */            \
  CPP(TemporalPlainDatePrototypeToPlainYearMonth, kDontAdaptArgumentsSentinel) \
  /* Temporal #sec-temporal.plaindate.prototype.toplainmonthday */             \
  CPP(TemporalPlainDatePrototypeToPlainMonthDay, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plaindate.prototype.getisofields */                \
  CPP(TemporalPlainDatePrototypeGetISOFields, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.plaindate.prototype.add */                         \
  CPP(TemporalPlainDatePrototypeAdd, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-temporal.plaindate.prototype.substract */                   \
  CPP(TemporalPlainDatePrototypeSubtract, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.plaindate.prototype.with */                        \
  CPP(TemporalPlainDatePrototypeWith, kDontAdaptArgumentsSentinel)             \
  /* Temporal #sec-temporal.plaindate.prototype.withcalendar */                \
  CPP(TemporalPlainDatePrototypeWithCalendar, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.plaindate.prototype.until */                       \
  CPP(TemporalPlainDatePrototypeUntil, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.plaindate.prototype.since */                       \
  CPP(TemporalPlainDatePrototypeSince, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.plaindate.prototype.equals */                      \
  CPP(TemporalPlainDatePrototypeEquals, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.plaindate.prototype.toplaindatetime */             \
  CPP(TemporalPlainDatePrototypeToPlainDateTime, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plaindate.prototype.tozoneddatetime */             \
  CPP(TemporalPlainDatePrototypeToZonedDateTime, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plaindate.prototype.tostring */                    \
  CPP(TemporalPlainDatePrototypeToString, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.plaindate.prototype.tojson */                      \
  CPP(TemporalPlainDatePrototypeToJSON, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.plaindate.prototype.tolocalestring */              \
  CPP(TemporalPlainDatePrototypeToLocaleString, kDontAdaptArgumentsSentinel)   \
  /* Temporal #sec-temporal.plaindate.prototype.valueof */                     \
  CPP(TemporalPlainDatePrototypeValueOf, kDontAdaptArgumentsSentinel)          \
                                                                               \
  /* Temporal.PlaneTime */                                                     \
  /* Temporal #sec-temporal.plaintime */                                       \
  CPP(TemporalPlainTimeConstructor, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-temporal.plaintime.from */                                  \
  CPP(TemporalPlainTimeFrom, kDontAdaptArgumentsSentinel)                      \
  /* Temporal #sec-temporal.plaintime.compare */                               \
  CPP(TemporalPlainTimeCompare, kDontAdaptArgumentsSentinel)                   \
  /* Temporal #sec-get-temporal.plaintime.prototype.calendar */                \
  CPP(TemporalPlainTimePrototypeCalendar, JSParameterCount(0))                 \
  /* Temporal #sec-get-temporal.plaintime.prototype.hour */                    \
  CPP(TemporalPlainTimePrototypeHour, JSParameterCount(0))                     \
  /* Temporal #sec-get-temporal.plaintime.prototype.minute */                  \
  CPP(TemporalPlainTimePrototypeMinute, JSParameterCount(0))                   \
  /* Temporal #sec-get-temporal.plaintime.prototype.second */                  \
  CPP(TemporalPlainTimePrototypeSecond, JSParameterCount(0))                   \
  /* Temporal #sec-get-temporal.plaintime.prototype.millisecond */             \
  CPP(TemporalPlainTimePrototypeMillisecond, JSParameterCount(0))              \
  /* Temporal #sec-get-temporal.plaintime.prototype.microsecond */             \
  CPP(TemporalPlainTimePrototypeMicrosecond, JSParameterCount(0))              \
  /* Temporal #sec-get-temporal.plaintime.prototype.nanosecond */              \
  CPP(TemporalPlainTimePrototypeNanosecond, JSParameterCount(0))               \
  /* Temporal #sec-temporal.plaintime.prototype.add */                         \
  CPP(TemporalPlainTimePrototypeAdd, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-temporal.plaintime.prototype.subtract */                    \
  CPP(TemporalPlainTimePrototypeSubtract, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.plaintime.prototype.with */                        \
  CPP(TemporalPlainTimePrototypeWith, kDontAdaptArgumentsSentinel)             \
  /* Temporal #sec-temporal.plaintime.prototype.until */                       \
  CPP(TemporalPlainTimePrototypeUntil, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.plaintime.prototype.since */                       \
  CPP(TemporalPlainTimePrototypeSince, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.plaintime.prototype.round */                       \
  CPP(TemporalPlainTimePrototypeRound, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.plaintime.prototype.equals */                      \
  CPP(TemporalPlainTimePrototypeEquals, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.plaintime.prototype.toplaindatetime */             \
  CPP(TemporalPlainTimePrototypeToPlainDateTime, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plaintime.prototype.tozoneddatetime */             \
  CPP(TemporalPlainTimePrototypeToZonedDateTime, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plaintime.prototype.getisofields */                \
  CPP(TemporalPlainTimePrototypeGetISOFields, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.plaintime.prototype.tostring */                    \
  CPP(TemporalPlainTimePrototypeToString, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.plaindtimeprototype.tojson */                      \
  CPP(TemporalPlainTimePrototypeToJSON, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.plaintime.prototype.tolocalestring */              \
  CPP(TemporalPlainTimePrototypeToLocaleString, kDontAdaptArgumentsSentinel)   \
  /* Temporal #sec-temporal.plaintime.prototype.valueof */                     \
  CPP(TemporalPlainTimePrototypeValueOf, kDontAdaptArgumentsSentinel)          \
                                                                               \
  /* Temporal.PlaneDateTime */                                                 \
  /* Temporal #sec-temporal.plaindatetime */                                   \
  CPP(TemporalPlainDateTimeConstructor, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.plaindatetime.from */                              \
  CPP(TemporalPlainDateTimeFrom, kDontAdaptArgumentsSentinel)                  \
  /* Temporal #sec-temporal.plaindatetime.compare */                           \
  CPP(TemporalPlainDateTimeCompare, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.calendar */            \
  CPP(TemporalPlainDateTimePrototypeCalendar, JSParameterCount(0))             \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.year */                \
  CPP(TemporalPlainDateTimePrototypeYear, JSParameterCount(0))                 \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.month */               \
  CPP(TemporalPlainDateTimePrototypeMonth, JSParameterCount(0))                \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.monthcode */           \
  CPP(TemporalPlainDateTimePrototypeMonthCode, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.day */                 \
  CPP(TemporalPlainDateTimePrototypeDay, JSParameterCount(0))                  \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.hour */                \
  CPP(TemporalPlainDateTimePrototypeHour, JSParameterCount(0))                 \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.minute */              \
  CPP(TemporalPlainDateTimePrototypeMinute, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.second */              \
  CPP(TemporalPlainDateTimePrototypeSecond, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.millisecond */         \
  CPP(TemporalPlainDateTimePrototypeMillisecond, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.microsecond */         \
  CPP(TemporalPlainDateTimePrototypeMicrosecond, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.nanosecond */          \
  CPP(TemporalPlainDateTimePrototypeNanosecond, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.dayofweek */           \
  CPP(TemporalPlainDateTimePrototypeDayOfWeek, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.dayofyear */           \
  CPP(TemporalPlainDateTimePrototypeDayOfYear, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.weekofyear */          \
  CPP(TemporalPlainDateTimePrototypeWeekOfYear, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.daysinweek */          \
  CPP(TemporalPlainDateTimePrototypeDaysInWeek, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.daysinmonth */         \
  CPP(TemporalPlainDateTimePrototypeDaysInMonth, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.daysinyear */          \
  CPP(TemporalPlainDateTimePrototypeDaysInYear, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.monthsinyear */        \
  CPP(TemporalPlainDateTimePrototypeMonthsInYear, JSParameterCount(0))         \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.inleapyear */          \
  CPP(TemporalPlainDateTimePrototypeInLeapYear, JSParameterCount(0))           \
  /* Temporal #sec-temporal.plaindatetime.prototype.with */                    \
  CPP(TemporalPlainDateTimePrototypeWith, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.plaindatetime.prototype.withplainTime */           \
  CPP(TemporalPlainDateTimePrototypeWithPlainTime,                             \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.withplainDate */           \
  CPP(TemporalPlainDateTimePrototypeWithPlainDate,                             \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.withcalendar */            \
  CPP(TemporalPlainDateTimePrototypeWithCalendar, kDontAdaptArgumentsSentinel) \
  /* Temporal #sec-temporal.plaindatetime.prototype.add */                     \
  CPP(TemporalPlainDateTimePrototypeAdd, kDontAdaptArgumentsSentinel)          \
  /* Temporal #sec-temporal.plaindatetime.prototype.subtract */                \
  CPP(TemporalPlainDateTimePrototypeSubtract, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.plaindatetime.prototype.until */                   \
  CPP(TemporalPlainDateTimePrototypeUntil, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.plaindatetime.prototype.since */                   \
  CPP(TemporalPlainDateTimePrototypeSince, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.plaindatetime.prototype.round */                   \
  CPP(TemporalPlainDateTimePrototypeRound, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.plaindatetime.prototype.equals */                  \
  CPP(TemporalPlainDateTimePrototypeEquals, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.plaindatetime.prototype.tostring */                \
  CPP(TemporalPlainDateTimePrototypeToString, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.plainddatetimeprototype.tojson */                  \
  CPP(TemporalPlainDateTimePrototypeToJSON, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.plaindatetime.prototype.tolocalestring */          \
  CPP(TemporalPlainDateTimePrototypeToLocaleString,                            \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.valueof */                 \
  CPP(TemporalPlainDateTimePrototypeValueOf, kDontAdaptArgumentsSentinel)      \
  /* Temporal #sec-temporal.plaindatetime.prototype.tozoneddatetime */         \
  CPP(TemporalPlainDateTimePrototypeToZonedDateTime,                           \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplaindate */             \
  CPP(TemporalPlainDateTimePrototypeToPlainDate, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplainyearmonth */        \
  CPP(TemporalPlainDateTimePrototypeToPlainYearMonth,                          \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplainmonthday */         \
  CPP(TemporalPlainDateTimePrototypeToPlainMonthDay,                           \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plaindatetime.prototype.toplaintime */             \
  CPP(TemporalPlainDateTimePrototypeToPlainTime, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plaindatetime.prototype.getisofields */            \
  CPP(TemporalPlainDateTimePrototypeGetISOFields, kDontAdaptArgumentsSentinel) \
                                                                               \
  /* Temporal.ZonedDateTime */                                                 \
  /* Temporal #sec-temporal.zoneddatetime */                                   \
  CPP(TemporalZonedDateTimeConstructor, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.zoneddatetime.from */                              \
  CPP(TemporalZonedDateTimeFrom, kDontAdaptArgumentsSentinel)                  \
  /* Temporal #sec-temporal.zoneddatetime.compare */                           \
  CPP(TemporalZonedDateTimeCompare, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.calendar */            \
  CPP(TemporalZonedDateTimePrototypeCalendar, JSParameterCount(0))             \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.timezone */            \
  CPP(TemporalZonedDateTimePrototypeTimeZone, JSParameterCount(0))             \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.year */                \
  CPP(TemporalZonedDateTimePrototypeYear, JSParameterCount(0))                 \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.month */               \
  CPP(TemporalZonedDateTimePrototypeMonth, JSParameterCount(0))                \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.monthcode */           \
  CPP(TemporalZonedDateTimePrototypeMonthCode, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.day */                 \
  CPP(TemporalZonedDateTimePrototypeDay, JSParameterCount(0))                  \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.hour */                \
  CPP(TemporalZonedDateTimePrototypeHour, JSParameterCount(0))                 \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.minute */              \
  CPP(TemporalZonedDateTimePrototypeMinute, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.second */              \
  CPP(TemporalZonedDateTimePrototypeSecond, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.millisecond */         \
  CPP(TemporalZonedDateTimePrototypeMillisecond, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.microsecond */         \
  CPP(TemporalZonedDateTimePrototypeMicrosecond, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.nanosecond */          \
  CPP(TemporalZonedDateTimePrototypeNanosecond, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochsecond */         \
  CPP(TemporalZonedDateTimePrototypeEpochSeconds, JSParameterCount(0))         \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochmilliseconds */   \
  CPP(TemporalZonedDateTimePrototypeEpochMilliseconds, JSParameterCount(0))    \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochmicroseconds */   \
  CPP(TemporalZonedDateTimePrototypeEpochMicroseconds, JSParameterCount(0))    \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.epochnanoseconds */    \
  CPP(TemporalZonedDateTimePrototypeEpochNanoseconds, JSParameterCount(0))     \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.dayofweek */           \
  CPP(TemporalZonedDateTimePrototypeDayOfWeek, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.dayofyear */           \
  CPP(TemporalZonedDateTimePrototypeDayOfYear, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.weekofyear */          \
  CPP(TemporalZonedDateTimePrototypeWeekOfYear, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.hoursinday */          \
  CPP(TemporalZonedDateTimePrototypeHoursInDay, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.daysinweek */          \
  CPP(TemporalZonedDateTimePrototypeDaysInWeek, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.daysinmonth */         \
  CPP(TemporalZonedDateTimePrototypeDaysInMonth, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.daysinyear */          \
  CPP(TemporalZonedDateTimePrototypeDaysInYear, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.monthsinyear */        \
  CPP(TemporalZonedDateTimePrototypeMonthsInYear, JSParameterCount(0))         \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.inleapyear */          \
  CPP(TemporalZonedDateTimePrototypeInLeapYear, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.offsetnanoseconds */   \
  CPP(TemporalZonedDateTimePrototypeOffsetNanoseconds, JSParameterCount(0))    \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.offset */              \
  CPP(TemporalZonedDateTimePrototypeOffset, JSParameterCount(0))               \
  /* Temporal #sec-temporal.zoneddatetime.prototype.with */                    \
  CPP(TemporalZonedDateTimePrototypeWith, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withplaintime */           \
  CPP(TemporalZonedDateTimePrototypeWithPlainTime,                             \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withplaindate */           \
  CPP(TemporalZonedDateTimePrototypeWithPlainDate,                             \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withtimezone */            \
  CPP(TemporalZonedDateTimePrototypeWithTimeZone, kDontAdaptArgumentsSentinel) \
  /* Temporal #sec-temporal.zoneddatetime.prototype.withcalendar */            \
  CPP(TemporalZonedDateTimePrototypeWithCalendar, kDontAdaptArgumentsSentinel) \
  /* Temporal #sec-temporal.zoneddatetime.prototype.add */                     \
  CPP(TemporalZonedDateTimePrototypeAdd, kDontAdaptArgumentsSentinel)          \
  /* Temporal #sec-temporal.zoneddatetime.prototype.subtract */                \
  CPP(TemporalZonedDateTimePrototypeSubtract, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.zoneddatetime.prototype.until */                   \
  CPP(TemporalZonedDateTimePrototypeUntil, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.zoneddatetime.prototype.since */                   \
  CPP(TemporalZonedDateTimePrototypeSince, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.zoneddatetime.prototype.round */                   \
  CPP(TemporalZonedDateTimePrototypeRound, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.zoneddatetime.prototype.equals */                  \
  CPP(TemporalZonedDateTimePrototypeEquals, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.zoneddatetime.prototype.tostring */                \
  CPP(TemporalZonedDateTimePrototypeToString, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.zonedddatetimeprototype.tojson */                  \
  CPP(TemporalZonedDateTimePrototypeToJSON, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.zoneddatetime.prototype.tolocalestring */          \
  CPP(TemporalZonedDateTimePrototypeToLocaleString,                            \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.valueof */                 \
  CPP(TemporalZonedDateTimePrototypeValueOf, kDontAdaptArgumentsSentinel)      \
  /* Temporal #sec-temporal.zoneddatetime.prototype.startofday */              \
  CPP(TemporalZonedDateTimePrototypeStartOfDay, kDontAdaptArgumentsSentinel)   \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toinstant */               \
  CPP(TemporalZonedDateTimePrototypeToInstant, kDontAdaptArgumentsSentinel)    \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplaindate */             \
  CPP(TemporalZonedDateTimePrototypeToPlainDate, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplaintime */             \
  CPP(TemporalZonedDateTimePrototypeToPlainTime, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplaindatetime */         \
  CPP(TemporalZonedDateTimePrototypeToPlainDateTime,                           \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplainyearmonth */        \
  CPP(TemporalZonedDateTimePrototypeToPlainYearMonth,                          \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.toplainmonthday */         \
  CPP(TemporalZonedDateTimePrototypeToPlainMonthDay,                           \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.zoneddatetime.prototype.getisofields */            \
  CPP(TemporalZonedDateTimePrototypeGetISOFields, kDontAdaptArgumentsSentinel) \
                                                                               \
  /* Temporal.Duration */                                                      \
  /* Temporal #sec-temporal.duration */                                        \
  CPP(TemporalDurationConstructor, kDontAdaptArgumentsSentinel)                \
  /* Temporal #sec-temporal.duration.from */                                   \
  CPP(TemporalDurationFrom, kDontAdaptArgumentsSentinel)                       \
  /* Temporal #sec-temporal.duration.compare */                                \
  CPP(TemporalDurationCompare, kDontAdaptArgumentsSentinel)                    \
  /* Temporal #sec-get-temporal.duration.prototype.years */                    \
  CPP(TemporalDurationPrototypeYears, JSParameterCount(0))                     \
  /* Temporal #sec-get-temporal.duration.prototype.months */                   \
  CPP(TemporalDurationPrototypeMonths, JSParameterCount(0))                    \
  /* Temporal #sec-get-temporal.duration.prototype.weeks */                    \
  CPP(TemporalDurationPrototypeWeeks, JSParameterCount(0))                     \
  /* Temporal #sec-get-temporal.duration.prototype.days */                     \
  CPP(TemporalDurationPrototypeDays, JSParameterCount(0))                      \
  /* Temporal #sec-get-temporal.duration.prototype.hours */                    \
  CPP(TemporalDurationPrototypeHours, JSParameterCount(0))                     \
  /* Temporal #sec-get-temporal.duration.prototype.minutes */                  \
  CPP(TemporalDurationPrototypeMinutes, JSParameterCount(0))                   \
  /* Temporal #sec-get-temporal.duration.prototype.seconds */                  \
  CPP(TemporalDurationPrototypeSeconds, JSParameterCount(0))                   \
  /* Temporal #sec-get-temporal.duration.prototype.milliseconds */             \
  CPP(TemporalDurationPrototypeMilliseconds, JSParameterCount(0))              \
  /* Temporal #sec-get-temporal.duration.prototype.microseconds */             \
  CPP(TemporalDurationPrototypeMicroseconds, JSParameterCount(0))              \
  /* Temporal #sec-get-temporal.duration.prototype.nanoseconds */              \
  CPP(TemporalDurationPrototypeNanoseconds, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.duration.prototype.sign */                     \
  CPP(TemporalDurationPrototypeSign, JSParameterCount(0))                      \
  /* Temporal #sec-get-temporal.duration.prototype.blank */                    \
  CPP(TemporalDurationPrototypeBlank, JSParameterCount(0))                     \
  /* Temporal #sec-temporal.duration.prototype.with */                         \
  CPP(TemporalDurationPrototypeWith, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-temporal.duration.prototype.negated */                      \
  CPP(TemporalDurationPrototypeNegated, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.duration.prototype.abs */                          \
  CPP(TemporalDurationPrototypeAbs, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-temporal.duration.prototype.add */                          \
  CPP(TemporalDurationPrototypeAdd, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-temporal.duration.prototype.subtract */                     \
  CPP(TemporalDurationPrototypeSubtract, kDontAdaptArgumentsSentinel)          \
  /* Temporal #sec-temporal.duration.prototype.round */                        \
  CPP(TemporalDurationPrototypeRound, kDontAdaptArgumentsSentinel)             \
  /* Temporal #sec-temporal.duration.prototype.total */                        \
  CPP(TemporalDurationPrototypeTotal, kDontAdaptArgumentsSentinel)             \
  /* Temporal #sec-temporal.duration.prototype.tostring */                     \
  CPP(TemporalDurationPrototypeToString, kDontAdaptArgumentsSentinel)          \
  /* Temporal #sec-temporal.duration.tojson */                                 \
  CPP(TemporalDurationPrototypeToJSON, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.duration.prototype.tolocalestring */               \
  CPP(TemporalDurationPrototypeToLocaleString, kDontAdaptArgumentsSentinel)    \
  /* Temporal #sec-temporal.duration.prototype.valueof */                      \
  CPP(TemporalDurationPrototypeValueOf, kDontAdaptArgumentsSentinel)           \
                                                                               \
  /* Temporal.Instant */                                                       \
  /* Temporal #sec-temporal.instant */                                         \
  CPP(TemporalInstantConstructor, kDontAdaptArgumentsSentinel)                 \
  /* Temporal #sec-temporal.instant.from */                                    \
  CPP(TemporalInstantFrom, kDontAdaptArgumentsSentinel)                        \
  /* Temporal #sec-temporal.instant.fromepochseconds */                        \
  CPP(TemporalInstantFromEpochSeconds, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.instant.fromepochmilliseconds */                   \
  CPP(TemporalInstantFromEpochMilliseconds, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.instant.fromepochmicroseconds */                   \
  CPP(TemporalInstantFromEpochMicroseconds, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.instant.fromepochnanoseconds */                    \
  CPP(TemporalInstantFromEpochNanoseconds, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.instant.compare */                                 \
  CPP(TemporalInstantCompare, kDontAdaptArgumentsSentinel)                     \
  /* Temporal #sec-get-temporal.instant.prototype.epochseconds */              \
  CPP(TemporalInstantPrototypeEpochSeconds, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.instant.prototype.epochmilliseconds */         \
  CPP(TemporalInstantPrototypeEpochMilliseconds, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.instant.prototype.epochmicroseconds */         \
  CPP(TemporalInstantPrototypeEpochMicroseconds, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.instant.prototype.epochnanoseconds */          \
  CPP(TemporalInstantPrototypeEpochNanoseconds, JSParameterCount(0))           \
  /* Temporal #sec-temporal.instant.prototype.add */                           \
  CPP(TemporalInstantPrototypeAdd, kDontAdaptArgumentsSentinel)                \
  /* Temporal #sec-temporal.instant.prototype.subtract */                      \
  CPP(TemporalInstantPrototypeSubtract, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.instant.prototype.until */                         \
  CPP(TemporalInstantPrototypeUntil, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-temporal.instant.prototype.since */                         \
  CPP(TemporalInstantPrototypeSince, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-temporal.instant.prototype.round */                         \
  CPP(TemporalInstantPrototypeRound, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-temporal.instant.prototype.equals */                        \
  CPP(TemporalInstantPrototypeEquals, kDontAdaptArgumentsSentinel)             \
  /* Temporal #sec-temporal.instant.prototype.tostring */                      \
  CPP(TemporalInstantPrototypeToString, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.instant.tojson */                                  \
  CPP(TemporalInstantPrototypeToJSON, kDontAdaptArgumentsSentinel)             \
  /* Temporal #sec-temporal.instant.prototype.tolocalestring */                \
  CPP(TemporalInstantPrototypeToLocaleString, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.instant.prototype.valueof */                       \
  CPP(TemporalInstantPrototypeValueOf, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-temporal.instant.prototype.tozoneddatetime */               \
  CPP(TemporalInstantPrototypeToZonedDateTime, kDontAdaptArgumentsSentinel)    \
  /* Temporal #sec-temporal.instant.prototype.tozoneddatetimeiso */            \
  CPP(TemporalInstantPrototypeToZonedDateTimeISO, kDontAdaptArgumentsSentinel) \
                                                                               \
  /* Temporal.PlainYearMonth */                                                \
  /* Temporal #sec-temporal.plainyearmonth */                                  \
  CPP(TemporalPlainYearMonthConstructor, kDontAdaptArgumentsSentinel)          \
  /* Temporal #sec-temporal.plainyearmonth.from */                             \
  CPP(TemporalPlainYearMonthFrom, kDontAdaptArgumentsSentinel)                 \
  /* Temporal #sec-temporal.plainyearmonth.compare */                          \
  CPP(TemporalPlainYearMonthCompare, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.calendar */           \
  CPP(TemporalPlainYearMonthPrototypeCalendar, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.year */               \
  CPP(TemporalPlainYearMonthPrototypeYear, JSParameterCount(0))                \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.month */              \
  CPP(TemporalPlainYearMonthPrototypeMonth, JSParameterCount(0))               \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.monthcode */          \
  CPP(TemporalPlainYearMonthPrototypeMonthCode, JSParameterCount(0))           \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.daysinyear */         \
  CPP(TemporalPlainYearMonthPrototypeDaysInYear, JSParameterCount(0))          \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.daysinmonth */        \
  CPP(TemporalPlainYearMonthPrototypeDaysInMonth, JSParameterCount(0))         \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.monthsinyear */       \
  CPP(TemporalPlainYearMonthPrototypeMonthsInYear, JSParameterCount(0))        \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.inleapyear */         \
  CPP(TemporalPlainYearMonthPrototypeInLeapYear, JSParameterCount(0))          \
  /* Temporal #sec-temporal.plainyearmonth.prototype.with */                   \
  CPP(TemporalPlainYearMonthPrototypeWith, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.plainyearmonth.prototype.add */                    \
  CPP(TemporalPlainYearMonthPrototypeAdd, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.plainyearmonth.prototype.subtract */               \
  CPP(TemporalPlainYearMonthPrototypeSubtract, kDontAdaptArgumentsSentinel)    \
  /* Temporal #sec-temporal.plainyearmonth.prototype.until */                  \
  CPP(TemporalPlainYearMonthPrototypeUntil, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.plainyearmonth.prototype.since */                  \
  CPP(TemporalPlainYearMonthPrototypeSince, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.plainyearmonth.prototype.equals */                 \
  CPP(TemporalPlainYearMonthPrototypeEquals, kDontAdaptArgumentsSentinel)      \
  /* Temporal #sec-temporal.plainyearmonth.tostring */                         \
  CPP(TemporalPlainYearMonthPrototypeToString, kDontAdaptArgumentsSentinel)    \
  /* Temporal #sec-temporal.plainyearmonth.tojson */                           \
  CPP(TemporalPlainYearMonthPrototypeToJSON, kDontAdaptArgumentsSentinel)      \
  /* Temporal #sec-temporal.plainyearmonth.prototype.tolocalestring */         \
  CPP(TemporalPlainYearMonthPrototypeToLocaleString,                           \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plainyearmonth.prototype.valueof */                \
  CPP(TemporalPlainYearMonthPrototypeValueOf, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.plainyearmonth.prototype.toplaindate */            \
  CPP(TemporalPlainYearMonthPrototypeToPlainDate, kDontAdaptArgumentsSentinel) \
  /* Temporal #sec-temporal.plainyearmonth.prototype.getisofields */           \
  CPP(TemporalPlainYearMonthPrototypeGetISOFields,                             \
      kDontAdaptArgumentsSentinel)                                             \
                                                                               \
  /* Temporal.PlainMonthDay */                                                 \
  /* Temporal #sec-temporal.plainmonthday */                                   \
  CPP(TemporalPlainMonthDayConstructor, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.plainmonthday.from */                              \
  CPP(TemporalPlainMonthDayFrom, kDontAdaptArgumentsSentinel)                  \
  /* There are no compare for PlainMonthDay */                                 \
  /* See https://github.com/tc39/proposal-temporal/issues/1547 */              \
  /* Temporal #sec-get-temporal.plainmonthday.prototype.calendar */            \
  CPP(TemporalPlainMonthDayPrototypeCalendar, JSParameterCount(0))             \
  /* Temporal #sec-get-temporal.plainmonthday.prototype.monthcode */           \
  CPP(TemporalPlainMonthDayPrototypeMonthCode, JSParameterCount(0))            \
  /* Temporal #sec-get-temporal.plainmonthday.prototype.day */                 \
  CPP(TemporalPlainMonthDayPrototypeDay, JSParameterCount(0))                  \
  /* Temporal #sec-temporal.plainmonthday.prototype.with */                    \
  CPP(TemporalPlainMonthDayPrototypeWith, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.plainmonthday.prototype.equals */                  \
  CPP(TemporalPlainMonthDayPrototypeEquals, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.plainmonthday.prototype.tostring */                \
  CPP(TemporalPlainMonthDayPrototypeToString, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.plainmonthday.tojson */                            \
  CPP(TemporalPlainMonthDayPrototypeToJSON, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.plainmonthday.prototype.tolocalestring */          \
  CPP(TemporalPlainMonthDayPrototypeToLocaleString,                            \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.plainmonthday.prototype.valueof */                 \
  CPP(TemporalPlainMonthDayPrototypeValueOf, kDontAdaptArgumentsSentinel)      \
  /* Temporal #sec-temporal.plainmonthday.prototype.toplaindate */             \
  CPP(TemporalPlainMonthDayPrototypeToPlainDate, kDontAdaptArgumentsSentinel)  \
  /* Temporal #sec-temporal.plainmonthday.prototype.getisofields */            \
  CPP(TemporalPlainMonthDayPrototypeGetISOFields, kDontAdaptArgumentsSentinel) \
                                                                               \
  /* Temporal.TimeZone */                                                      \
  /* Temporal #sec-temporal.timezone */                                        \
  CPP(TemporalTimeZoneConstructor, kDontAdaptArgumentsSentinel)                \
  /* Temporal #sec-temporal.timezone.from */                                   \
  CPP(TemporalTimeZoneFrom, kDontAdaptArgumentsSentinel)                       \
  /* Temporal #sec-get-temporal.timezone.prototype.id */                       \
  CPP(TemporalTimeZonePrototypeId, JSParameterCount(0))                        \
  /* Temporal #sec-temporal.timezone.prototype.getoffsetnanosecondsfor */      \
  CPP(TemporalTimeZonePrototypeGetOffsetNanosecondsFor,                        \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.timezone.prototype.getoffsetstringfor */           \
  CPP(TemporalTimeZonePrototypeGetOffsetStringFor,                             \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.timezone.prototype.getplaindatetimefor */          \
  CPP(TemporalTimeZonePrototypeGetPlainDateTimeFor,                            \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.timezone.prototype.getinstantfor */                \
  CPP(TemporalTimeZonePrototypeGetInstantFor, kDontAdaptArgumentsSentinel)     \
  /* Temporal #sec-temporal.timezone.prototype.getpossibleinstantsfor */       \
  CPP(TemporalTimeZonePrototypeGetPossibleInstantsFor,                         \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.timezone.prototype.getnexttransition */            \
  CPP(TemporalTimeZonePrototypeGetNextTransition, kDontAdaptArgumentsSentinel) \
  /* Temporal #sec-temporal.timezone.prototype.getprevioustransition */        \
  CPP(TemporalTimeZonePrototypeGetPreviousTransition,                          \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.timezone.prototype.tostring */                     \
  CPP(TemporalTimeZonePrototypeToString, kDontAdaptArgumentsSentinel)          \
  /* Temporal #sec-temporal.timezone.prototype.tojson */                       \
  CPP(TemporalTimeZonePrototypeToJSON, kDontAdaptArgumentsSentinel)            \
                                                                               \
  /* Temporal.Calendar */                                                      \
  /* Temporal #sec-temporal.calendar */                                        \
  CPP(TemporalCalendarConstructor, kDontAdaptArgumentsSentinel)                \
  /* Temporal #sec-temporal.calendar.from */                                   \
  CPP(TemporalCalendarFrom, kDontAdaptArgumentsSentinel)                       \
  /* Temporal #sec-get-temporal.calendar.prototype.id */                       \
  CPP(TemporalCalendarPrototypeId, JSParameterCount(0))                        \
  /* Temporal #sec-temporal.calendar.prototype.datefromfields */               \
  CPP(TemporalCalendarPrototypeDateFromFields, kDontAdaptArgumentsSentinel)    \
  /* Temporal #sec-temporal.calendar.prototype.yearmonthfromfields */          \
  CPP(TemporalCalendarPrototypeYearMonthFromFields,                            \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.calendar.prototype.monthdayfromfields */           \
  CPP(TemporalCalendarPrototypeMonthDayFromFields,                             \
      kDontAdaptArgumentsSentinel)                                             \
  /* Temporal #sec-temporal.calendar.prototype.dateadd */                      \
  CPP(TemporalCalendarPrototypeDateAdd, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-temporal.calendar.prototype.dateuntil */                    \
  CPP(TemporalCalendarPrototypeDateUntil, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.calendar.prototype.year */                         \
  CPP(TemporalCalendarPrototypeYear, kDontAdaptArgumentsSentinel)              \
  /* Temporal #sec-temporal.calendar.prototype.month */                        \
  CPP(TemporalCalendarPrototypeMonth, kDontAdaptArgumentsSentinel)             \
  /* Temporal #sec-temporal.calendar.prototype.monthcode */                    \
  CPP(TemporalCalendarPrototypeMonthCode, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.calendar.prototype.day */                          \
  CPP(TemporalCalendarPrototypeDay, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-temporal.calendar.prototype.dayofweek */                    \
  CPP(TemporalCalendarPrototypeDayOfWeek, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.calendar.prototype.dayofyear */                    \
  CPP(TemporalCalendarPrototypeDayOfYear, kDontAdaptArgumentsSentinel)         \
  /* Temporal #sec-temporal.calendar.prototype.weekofyear */                   \
  CPP(TemporalCalendarPrototypeWeekOfYear, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.calendar.prototype.daysinweek */                   \
  CPP(TemporalCalendarPrototypeDaysInWeek, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.calendar.prototype.daysinmonth */                  \
  CPP(TemporalCalendarPrototypeDaysInMonth, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.calendar.prototype.daysinyear */                   \
  CPP(TemporalCalendarPrototypeDaysInYear, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.calendar.prototype.monthsinyear */                 \
  CPP(TemporalCalendarPrototypeMonthsInYear, kDontAdaptArgumentsSentinel)      \
  /* Temporal #sec-temporal.calendar.prototype.inleapyear */                   \
  CPP(TemporalCalendarPrototypeInLeapYear, kDontAdaptArgumentsSentinel)        \
  /* Temporal #sec-temporal.calendar.prototype.fields */                       \
  TFJ(TemporalCalendarPrototypeFields, kJSArgcReceiverSlots + 1, kReceiver,    \
      kIterable)                                                               \
  /* Temporal #sec-temporal.calendar.prototype.mergefields */                  \
  CPP(TemporalCalendarPrototypeMergeFields, kDontAdaptArgumentsSentinel)       \
  /* Temporal #sec-temporal.calendar.prototype.tostring */                     \
  CPP(TemporalCalendarPrototypeToString, kDontAdaptArgumentsSentinel)          \
  /* Temporal #sec-temporal.calendar.prototype.tojson */                       \
  CPP(TemporalCalendarPrototypeToJSON, kDontAdaptArgumentsSentinel)            \
  /* Temporal #sec-date.prototype.totemporalinstant */                         \
  CPP(DatePrototypeToTemporalInstant, kDontAdaptArgumentsSentinel)             \
                                                                               \
  /* "Private" (created but not exposed) Bulitins needed by Temporal */        \
  TFJ(StringFixedArrayFromIterable, kJSArgcReceiverSlots + 1, kReceiver,       \
      kIterable)                                                               \
  TFJ(TemporalInstantFixedArrayFromIterable, kJSArgcReceiverSlots + 1,         \
      kReceiver, kIterable)

#define BUILTIN_LIST_BASE(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, ASM) \
  BUILTIN_LIST_BASE_TIER0(CPP, TFJ, TFC, TFS, TFH, ASM)           \
  BUILTIN_LIST_BASE_TIER1(CPP, TSJ, TFJ, TSC, TFC, TFS, TFH, ASM)

#ifdef V8_INTL_SUPPORT
#define BUILTIN_LIST_INTL(CPP, TFJ, TFS)                                       \
  /* ecma402 #sec-intl.collator */                                             \
  CPP(CollatorConstructor, kDontAdaptArgumentsSentinel)                        \
  /* ecma 402 #sec-collator-compare-functions*/                                \
  CPP(CollatorInternalCompare, JSParameterCount(2))                            \
  /* ecma402 #sec-intl.collator.prototype.compare */                           \
  CPP(CollatorPrototypeCompare, kDontAdaptArgumentsSentinel)                   \
  /* ecma402 #sec-intl.collator.supportedlocalesof */                          \
  CPP(CollatorSupportedLocalesOf, kDontAdaptArgumentsSentinel)                 \
  /* ecma402 #sec-intl.collator.prototype.resolvedoptions */                   \
  CPP(CollatorPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)           \
  /* ecma402 #sup-date.prototype.tolocaledatestring */                         \
  CPP(DatePrototypeToLocaleDateString, kDontAdaptArgumentsSentinel)            \
  /* ecma402 #sup-date.prototype.tolocalestring */                             \
  CPP(DatePrototypeToLocaleString, kDontAdaptArgumentsSentinel)                \
  /* ecma402 #sup-date.prototype.tolocaletimestring */                         \
  CPP(DatePrototypeToLocaleTimeString, kDontAdaptArgumentsSentinel)            \
  /* ecma402 #sec-intl.datetimeformat */                                       \
  CPP(DateTimeFormatConstructor, kDontAdaptArgumentsSentinel)                  \
  /* ecma402 #sec-datetime-format-functions */                                 \
  CPP(DateTimeFormatInternalFormat, JSParameterCount(1))                       \
  /* ecma402 #sec-intl.datetimeformat.prototype.format */                      \
  CPP(DateTimeFormatPrototypeFormat, kDontAdaptArgumentsSentinel)              \
  /* ecma402 #sec-intl.datetimeformat.prototype.formatrange */                 \
  CPP(DateTimeFormatPrototypeFormatRange, kDontAdaptArgumentsSentinel)         \
  /* ecma402 #sec-intl.datetimeformat.prototype.formatrangetoparts */          \
  CPP(DateTimeFormatPrototypeFormatRangeToParts, kDontAdaptArgumentsSentinel)  \
  /* ecma402 #sec-intl.datetimeformat.prototype.formattoparts */               \
  CPP(DateTimeFormatPrototypeFormatToParts, kDontAdaptArgumentsSentinel)       \
  /* ecma402 #sec-intl.datetimeformat.prototype.resolvedoptions */             \
  CPP(DateTimeFormatPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)     \
  /* ecma402 #sec-intl.datetimeformat.supportedlocalesof */                    \
  CPP(DateTimeFormatSupportedLocalesOf, kDontAdaptArgumentsSentinel)           \
  /* ecma402 #sec-Intl.DisplayNames */                                         \
  CPP(DisplayNamesConstructor, kDontAdaptArgumentsSentinel)                    \
  /* ecma402 #sec-Intl.DisplayNames.prototype.of */                            \
  CPP(DisplayNamesPrototypeOf, kDontAdaptArgumentsSentinel)                    \
  /* ecma402 #sec-Intl.DisplayNames.prototype.resolvedOptions */               \
  CPP(DisplayNamesPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)       \
  /* ecma402 #sec-Intl.DisplayNames.supportedLocalesOf */                      \
  CPP(DisplayNamesSupportedLocalesOf, kDontAdaptArgumentsSentinel)             \
  /* ecma402 #sec-intl-durationformat-constructor */                           \
  CPP(DurationFormatConstructor, kDontAdaptArgumentsSentinel)                  \
  /* ecma402 #sec-Intl.DurationFormat.prototype.format */                      \
  CPP(DurationFormatPrototypeFormat, kDontAdaptArgumentsSentinel)              \
  /* ecma402 #sec-Intl.DurationFormat.prototype.formatToParts */               \
  CPP(DurationFormatPrototypeFormatToParts, kDontAdaptArgumentsSentinel)       \
  /* ecma402 #sec-Intl.DurationFormat.prototype.resolvedOptions */             \
  CPP(DurationFormatPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)     \
  /* ecma402 #sec-Intl.DurationFormat.supportedLocalesOf */                    \
  CPP(DurationFormatSupportedLocalesOf, kDontAdaptArgumentsSentinel)           \
  /* ecma402 #sec-intl.getcanonicallocales */                                  \
  CPP(IntlGetCanonicalLocales, kDontAdaptArgumentsSentinel)                    \
  /* ecma402 #sec-intl.supportedvaluesof */                                    \
  CPP(IntlSupportedValuesOf, kDontAdaptArgumentsSentinel)                      \
  /* ecma402 #sec-intl-listformat-constructor */                               \
  CPP(ListFormatConstructor, kDontAdaptArgumentsSentinel)                      \
  /* ecma402 #sec-intl-list-format.prototype.format */                         \
  TFJ(ListFormatPrototypeFormat, kDontAdaptArgumentsSentinel)                  \
  /* ecma402 #sec-intl-list-format.prototype.formattoparts */                  \
  TFJ(ListFormatPrototypeFormatToParts, kDontAdaptArgumentsSentinel)           \
  /* ecma402 #sec-intl.listformat.prototype.resolvedoptions */                 \
  CPP(ListFormatPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)         \
  /* ecma402 #sec-intl.ListFormat.supportedlocalesof */                        \
  CPP(ListFormatSupportedLocalesOf, kDontAdaptArgumentsSentinel)               \
  /* ecma402 #sec-intl-locale-constructor */                                   \
  CPP(LocaleConstructor, kDontAdaptArgumentsSentinel)                          \
  /* ecma402 #sec-Intl.Locale.prototype.baseName */                            \
  CPP(LocalePrototypeBaseName, JSParameterCount(0))                            \
  /* ecma402 #sec-Intl.Locale.prototype.calendar */                            \
  CPP(LocalePrototypeCalendar, JSParameterCount(0))                            \
  /* ecma402 #sec-Intl.Locale.prototype.calendars */                           \
  CPP(LocalePrototypeCalendars, JSParameterCount(0))                           \
  /* ecma402 #sec-Intl.Locale.prototype.caseFirst */                           \
  CPP(LocalePrototypeCaseFirst, JSParameterCount(0))                           \
  /* ecma402 #sec-Intl.Locale.prototype.collation */                           \
  CPP(LocalePrototypeCollation, JSParameterCount(0))                           \
  /* ecma402 #sec-Intl.Locale.prototype.collations */                          \
  CPP(LocalePrototypeCollations, JSParameterCount(0))                          \
  /* ecma402 #sec-Intl.Locale.prototype.firstDayOfWeek */                      \
  CPP(LocalePrototypeFirstDayOfWeek, JSParameterCount(0))                      \
  /* ecma402 #sec-Intl.Locale.prototype.getCalendars */                        \
  CPP(LocalePrototypeGetCalendars, kDontAdaptArgumentsSentinel)                \
  /* ecma402 #sec-Intl.Locale.prototype.getCollations */                       \
  CPP(LocalePrototypeGetCollations, kDontAdaptArgumentsSentinel)               \
  /* ecma402 #sec-Intl.Locale.prototype.getHourCycles */                       \
  CPP(LocalePrototypeGetHourCycles, kDontAdaptArgumentsSentinel)               \
  /* ecma402 #sec-Intl.Locale.prototype.getNumberingSystems */                 \
  CPP(LocalePrototypeGetNumberingSystems, kDontAdaptArgumentsSentinel)         \
  /* ecma402 #sec-Intl.Locale.prototype.getTimeZones */                        \
  CPP(LocalePrototypeGetTimeZones, kDontAdaptArgumentsSentinel)                \
  /* ecma402 #sec-Intl.Locale.prototype.getTextInfo */                         \
  CPP(LocalePrototypeGetTextInfo, kDontAdaptArgumentsSentinel)                 \
  /* ecma402 #sec-Intl.Locale.prototype.getWeekInfo */                         \
  CPP(LocalePrototypeGetWeekInfo, kDontAdaptArgumentsSentinel)                 \
  /* ecma402 #sec-Intl.Locale.prototype.hourCycle */                           \
  CPP(LocalePrototypeHourCycle, JSParameterCount(0))                           \
  /* ecma402 #sec-Intl.Locale.prototype.hourCycles */                          \
  CPP(LocalePrototypeHourCycles, JSParameterCount(0))                          \
  /* ecma402 #sec-Intl.Locale.prototype.language */                            \
  CPP(LocalePrototypeLanguage, JSParameterCount(0))                            \
  /* ecma402 #sec-Intl.Locale.prototype.maximize */                            \
  CPP(LocalePrototypeMaximize, kDontAdaptArgumentsSentinel)                    \
  /* ecma402 #sec-Intl.Locale.prototype.minimize */                            \
  CPP(LocalePrototypeMinimize, kDontAdaptArgumentsSentinel)                    \
  /* ecma402 #sec-Intl.Locale.prototype.numeric */                             \
  CPP(LocalePrototypeNumeric, JSParameterCount(0))                             \
  /* ecma402 #sec-Intl.Locale.prototype.numberingSystem */                     \
  CPP(LocalePrototypeNumberingSystem, JSParameterCount(0))                     \
  /* ecma402 #sec-Intl.Locale.prototype.numberingSystems */                    \
  CPP(LocalePrototypeNumberingSystems, JSParameterCount(0))                    \
  /* ecma402 #sec-Intl.Locale.prototype.region */                              \
  CPP(LocalePrototypeRegion, JSParameterCount(0))                              \
  /* ecma402 #sec-Intl.Locale.prototype.script */                              \
  CPP(LocalePrototypeScript, JSParameterCount(0))                              \
  /* ecma402 #sec-Intl.Locale.prototype.textInfo */                            \
  CPP(LocalePrototypeTextInfo, JSParameterCount(0))                            \
  /* ecma402 #sec-Intl.Locale.prototype.timezones */                           \
  CPP(LocalePrototypeTimeZones, JSParameterCount(0))                           \
  /* ecma402 #sec-Intl.Locale.prototype.toString */                            \
  CPP(LocalePrototypeToString, kDontAdaptArgumentsSentinel)                    \
  /* ecma402 #sec-Intl.Locale.prototype.weekInfo */                            \
  CPP(LocalePrototypeWeekInfo, JSParameterCount(0))                            \
  /* ecma402 #sec-intl.numberformat */                                         \
  CPP(NumberFormatConstructor, kDontAdaptArgumentsSentinel)                    \
  /* ecma402 #sec-number-format-functions */                                   \
  CPP(NumberFormatInternalFormatNumber, JSParameterCount(1))                   \
  /* ecma402 #sec-intl.numberformat.prototype.format */                        \
  CPP(NumberFormatPrototypeFormatNumber, kDontAdaptArgumentsSentinel)          \
  /* ecma402 #sec-intl.numberformat.prototype.formatrange */                   \
  CPP(NumberFormatPrototypeFormatRange, kDontAdaptArgumentsSentinel)           \
  /* ecma402 #sec-intl.numberformat.prototype.formatrangetoparts */            \
  CPP(NumberFormatPrototypeFormatRangeToParts, kDontAdaptArgumentsSentinel)    \
  /* ecma402 #sec-intl.numberformat.prototype.formattoparts */                 \
  CPP(NumberFormatPrototypeFormatToParts, kDontAdaptArgumentsSentinel)         \
  /* ecma402 #sec-intl.numberformat.prototype.resolvedoptions */               \
  CPP(NumberFormatPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)       \
  /* ecma402 #sec-intl.numberformat.supportedlocalesof */                      \
  CPP(NumberFormatSupportedLocalesOf, kDontAdaptArgumentsSentinel)             \
  /* ecma402 #sec-intl.pluralrules */                                          \
  CPP(PluralRulesConstructor, kDontAdaptArgumentsSentinel)                     \
  /* ecma402 #sec-intl.pluralrules.prototype.resolvedoptions */                \
  CPP(PluralRulesPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)        \
  /* ecma402 #sec-intl.pluralrules.prototype.select */                         \
  CPP(PluralRulesPrototypeSelect, kDontAdaptArgumentsSentinel)                 \
  /* ecma402 #sec-intl.pluralrules.prototype.selectrange */                    \
  CPP(PluralRulesPrototypeSelectRange, kDontAdaptArgumentsSentinel)            \
  /* ecma402 #sec-intl.pluralrules.supportedlocalesof */                       \
  CPP(PluralRulesSupportedLocalesOf, kDontAdaptArgumentsSentinel)              \
  /* ecma402 #sec-intl.RelativeTimeFormat.constructor */                       \
  CPP(RelativeTimeFormatConstructor, kDontAdaptArgumentsSentinel)              \
  /* ecma402 #sec-intl.RelativeTimeFormat.prototype.format */                  \
  CPP(RelativeTimeFormatPrototypeFormat, kDontAdaptArgumentsSentinel)          \
  /* ecma402 #sec-intl.RelativeTimeFormat.prototype.formatToParts */           \
  CPP(RelativeTimeFormatPrototypeFormatToParts, kDontAdaptArgumentsSentinel)   \
  /* ecma402 #sec-intl.RelativeTimeFormat.prototype.resolvedOptions */         \
  CPP(RelativeTimeFormatPrototypeResolvedOptions, kDontAdaptArgumentsSentinel) \
  /* ecma402 #sec-intl.RelativeTimeFormat.supportedlocalesof */                \
  CPP(RelativeTimeFormatSupportedLocalesOf, kDontAdaptArgumentsSentinel)       \
  /* ecma402 #sec-Intl.Segmenter */                                            \
  CPP(SegmenterConstructor, kDontAdaptArgumentsSentinel)                       \
  /* ecma402 #sec-Intl.Segmenter.prototype.resolvedOptions */                  \
  CPP(SegmenterPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)          \
  /* ecma402 #sec-Intl.Segmenter.prototype.segment  */                         \
  CPP(SegmenterPrototypeSegment, kDontAdaptArgumentsSentinel)                  \
  /* ecma402  #sec-Intl.Segmenter.supportedLocalesOf */                        \
  CPP(SegmenterSupportedLocalesOf, kDontAdaptArgumentsSentinel)                \
  /* ecma402 #sec-segment-iterator-prototype-next */                           \
  CPP(SegmentIteratorPrototypeNext, kDontAdaptArgumentsSentinel)               \
  /* ecma402 #sec-%segmentsprototype%.containing */                            \
  CPP(SegmentsPrototypeContaining, kDontAdaptArgumentsSentinel)                \
  /* ecma402 #sec-%segmentsprototype%-@@iterator */                            \
  CPP(SegmentsPrototypeIterator, JSParameterCount(0))                          \
  /* ecma402 #sup-properties-of-the-string-prototype-object */                 \
  CPP(StringPrototypeLocaleCompareIntl, kDontAdaptArgumentsSentinel)           \
  /* ES #sec-string.prototype.normalize */                                     \
  CPP(StringPrototypeNormalizeIntl, kDontAdaptArgumentsSentinel)               \
  /* ecma402 #sup-string.prototype.tolocalelowercase */                        \
  TFJ(StringPrototypeToLocaleLowerCase, kDontAdaptArgumentsSentinel)           \
  /* ecma402 #sup-string.prototype.tolocaleuppercase */                        \
  CPP(StringPrototypeToLocaleUpperCase, kDontAdaptArgumentsSentinel)           \
  /* ES #sec-string.prototype.tolowercase */                                   \
  TFJ(StringPrototypeToLowerCaseIntl, kJSArgcReceiverSlots, kReceiver)         \
  /* ES #sec-string.prototype.touppercase */                                   \
  CPP(StringPrototypeToUpperCaseIntl, kDontAdaptArgumentsSentinel)             \
  TFS(StringToLowerCaseIntl, NeedsContext::kYes, kString)                      \
                                                                               \
  /* Temporal */                                                               \
  /* Temporal #sec-temporal.calendar.prototype.era */                          \
  CPP(TemporalCalendarPrototypeEra, kDontAdaptArgumentsSentinel)               \
  /* Temporal #sec-temporal.calendar.prototype.erayear */                      \
  CPP(TemporalCalendarPrototypeEraYear, kDontAdaptArgumentsSentinel)           \
  /* Temporal #sec-get-temporal.plaindate.prototype.era */                     \
  CPP(TemporalPlainDatePrototypeEra, JSParameterCount(0))                      \
  /* Temporal #sec-get-temporal.plaindate.prototype.erayear */                 \
  CPP(TemporalPlainDatePrototypeEraYear, JSParameterCount(0))                  \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.era */                 \
  CPP(TemporalPlainDateTimePrototypeEra, JSParameterCount(0))                  \
  /* Temporal #sec-get-temporal.plaindatetime.prototype.erayear */             \
  CPP(TemporalPlainDateTimePrototypeEraYear, JSParameterCount(0))              \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.era */                \
  CPP(TemporalPlainYearMonthPrototypeEra, JSParameterCount(0))                 \
  /* Temporal #sec-get-temporal.plainyearmonth.prototype.erayear */            \
  CPP(TemporalPlainYearMonthPrototypeEraYear, JSParameterCount(0))             \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.era */                 \
  CPP(TemporalZonedDateTimePrototypeEra, JSParameterCount(0))                  \
  /* Temporal #sec-get-temporal.zoneddatetime.prototype.erayear */             \
  CPP(TemporalZonedDateTimePrototypeEraYear, JSParameterCount(0))              \
                                                                               \
  CPP(V8BreakIteratorConstructor, kDontAdaptArgumentsSentinel)                 \
  CPP(V8BreakIteratorInternalAdoptText, JSParameterCount(1))                   \
  CPP(V8BreakIteratorInternalBreakType, JSParameterCount(0))                   \
  CPP(V8BreakIteratorInternalCurrent, JSParameterCount(0))                     \
  CPP(V8BreakIteratorInternalFirst, JSParameterCount(0))                       \
  CPP(V8BreakIteratorInternalNext, JSParameterCount(0))                        \
  CPP(V8BreakIteratorPrototypeAdoptText, kDontAdaptArgumentsSentinel)          \
  CPP(V8BreakIteratorPrototypeBreakType, kDontAdaptArgumentsSentinel)          \
  CPP(V8BreakIteratorPrototypeCurrent, kDontAdaptArgumentsSentinel)            \
  CPP(V8BreakIteratorPrototypeFirst, kDontAdaptArgumentsSentinel)              \
  CPP(V8BreakIteratorPrototypeNext, kDontAdaptArgumentsSentinel)               \
  CPP(V8BreakIteratorPrototypeResolvedOptions, kDontAdaptArgumentsSentinel)    \
  CPP(V8BreakIteratorSupportedLocalesOf, kDontAdaptArgumentsSentinel)
#else
#define BUILTIN_LIST_INTL(CPP, TFJ, TFS)                             \
  /* ES6 #sec-string.prototype.localecompare */                      \
  /* non-locale specific fallback version */                         \
  CPP(StringPrototypeLocaleCompare, JSParameterCount(1))             \
  /* no-op fallback version */                                       \
  CPP(StringPrototypeNormalize, kDontAdaptArgumentsSentinel)         \
  /* same as toLowercase; fallback version */                        \
  CPP(StringPrototypeToLocaleLowerCase, kDontAdaptArgumentsSentinel) \
  /* same as toUppercase; fallback version */                        \
  CPP(StringPrototypeToLocaleUpperCase, kDontAdaptArgumentsSentinel) \
  /* (obsolete) Unibrow version */                                   \
  CPP(StringPrototypeToLowerCase, kDontAdaptArgumentsSentinel)       \
  /* (obsolete) Unibrow version */                                   \
  CPP(StringPrototypeToUpperCase, kDontAdaptArgumentsSentinel)
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
  V(PromiseTry)                                      \
  V(ResolvePromise)                                  \
  V(AsyncDisposeFromSyncDispose)

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
