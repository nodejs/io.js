// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-evaluate.h"

#include "src/builtins/accessors.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/compiler.h"
#include "src/codegen/reloc-info.h"
#include "src/codegen/script-details.h"
#include "src/common/globals.h"
#include "src/debug/debug-frames.h"
#include "src/debug/debug-scopes.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/code-inl.h"
#include "src/objects/contexts.h"
#include "src/objects/string-set-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

namespace {
static MaybeHandle<SharedFunctionInfo> GetFunctionInfo(Isolate* isolate,
                                                       Handle<String> source,
                                                       REPLMode repl_mode) {
  ScriptDetails script_details(isolate->factory()->empty_string(),
                               ScriptOriginOptions(true, true));
  script_details.repl_mode = repl_mode;
  ScriptCompiler::CompilationDetails compilation_details;
  return Compiler::GetSharedFunctionInfoForScript(
      isolate, source, script_details, ScriptCompiler::kNoCompileOptions,
      ScriptCompiler::kNoCacheNoReason, NOT_NATIVES_CODE, &compilation_details);
}
}  // namespace

MaybeHandle<Object> DebugEvaluate::Global(Isolate* isolate,
                                          Handle<String> source,
                                          debug::EvaluateGlobalMode mode,
                                          REPLMode repl_mode) {
  Handle<SharedFunctionInfo> shared_info;
  if (!GetFunctionInfo(isolate, source, repl_mode).ToHandle(&shared_info)) {
    return MaybeHandle<Object>();
  }

  Handle<NativeContext> context = isolate->native_context();
  Handle<JSFunction> function =
      Factory::JSFunctionBuilder{isolate, shared_info, context}.Build();

  DisableBreak disable_break_scope(
      isolate->debug(),
      mode == debug::EvaluateGlobalMode::kDisableBreaks ||
          mode ==
              debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect);

  if (mode == debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect) {
    isolate->debug()->StartSideEffectCheckMode();
  }
  // TODO(cbruni, 1244145): Use host-defined options from script context.
  Handle<FixedArray> host_defined_options(
      Script::cast(function->shared()->script())->host_defined_options(),
      isolate);
  MaybeHandle<Object> result = Execution::CallScript(
      isolate, function, Handle<JSObject>(context->global_proxy(), isolate),
      host_defined_options);
  if (mode == debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect) {
    isolate->debug()->StopSideEffectCheckMode();
  }
  return result;
}

MaybeHandle<Object> DebugEvaluate::Local(Isolate* isolate,
                                         StackFrameId frame_id,
                                         int inlined_jsframe_index,
                                         Handle<String> source,
                                         bool throw_on_side_effect) {
  // Handle the processing of break.
  DisableBreak disable_break_scope(isolate->debug());

  // Get the frame where the debugging is performed.
  DebuggableStackFrameIterator it(isolate, frame_id);
#if V8_ENABLE_WEBASSEMBLY
  if (it.is_wasm()) {
    WasmFrame* frame = WasmFrame::cast(it.frame());
    Handle<SharedFunctionInfo> outer_info(
        isolate->native_context()->empty_function()->shared(), isolate);
    Handle<JSObject> context_extension = GetWasmDebugProxy(frame);
    Handle<ScopeInfo> scope_info =
        ScopeInfo::CreateForWithScope(isolate, Handle<ScopeInfo>::null());
    Handle<Context> context = isolate->factory()->NewWithContext(
        isolate->native_context(), scope_info, context_extension);
    return Evaluate(isolate, outer_info, context, context_extension, source,
                    throw_on_side_effect);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  CHECK(it.is_javascript());
  JavaScriptFrame* frame = it.javascript_frame();
  // This is not a lot different than DebugEvaluate::Global, except that
  // variables accessible by the function we are evaluating from are
  // materialized and included on top of the native context. Changes to
  // the materialized object are written back afterwards.
  // Note that the native context is taken from the original context chain,
  // which may not be the current native context of the isolate.
  ContextBuilder context_builder(isolate, frame, inlined_jsframe_index);
  if (isolate->has_exception()) return {};

  Handle<Context> context = context_builder.evaluation_context();
  Handle<JSObject> receiver(context->global_proxy(), isolate);
  MaybeHandle<Object> maybe_result =
      Evaluate(isolate, context_builder.outer_info(), context, receiver, source,
               throw_on_side_effect);
  if (!maybe_result.is_null()) context_builder.UpdateValues();
  return maybe_result;
}

MaybeHandle<Object> DebugEvaluate::WithTopmostArguments(Isolate* isolate,
                                                        Handle<String> source) {
  // Handle the processing of break.
  DisableBreak disable_break_scope(isolate->debug());
  Factory* factory = isolate->factory();
  JavaScriptStackFrameIterator it(isolate);

  // Get context and receiver.
  Handle<Context> native_context(
      Context::cast(it.frame()->context())->native_context(), isolate);

  // Materialize arguments as property on an extension object.
  Handle<JSObject> materialized = factory->NewSlowJSObjectWithNullProto();
  Handle<String> arguments_str = factory->arguments_string();
  JSObject::SetOwnPropertyIgnoreAttributes(
      materialized, arguments_str,
      Accessors::FunctionGetArguments(it.frame(), 0), NONE)
      .Check();

  // Materialize receiver.
  Handle<Object> this_value(it.frame()->receiver(), isolate);
  DCHECK_EQ(it.frame()->IsConstructor(), IsTheHole(*this_value, isolate));
  if (!IsTheHole(*this_value, isolate)) {
    Handle<String> this_str = factory->this_string();
    JSObject::SetOwnPropertyIgnoreAttributes(materialized, this_str, this_value,
                                             NONE)
        .Check();
  }

  // Use extension object in a debug-evaluate scope.
  Handle<ScopeInfo> scope_info =
      ScopeInfo::CreateForWithScope(isolate, Handle<ScopeInfo>::null());
  scope_info->SetIsDebugEvaluateScope();
  Handle<Context> evaluation_context = factory->NewDebugEvaluateContext(
      native_context, scope_info, materialized, Handle<Context>());
  Handle<SharedFunctionInfo> outer_info(
      native_context->empty_function()->shared(), isolate);
  Handle<JSObject> receiver(native_context->global_proxy(), isolate);
  const bool throw_on_side_effect = false;
  MaybeHandle<Object> maybe_result =
      Evaluate(isolate, outer_info, evaluation_context, receiver, source,
               throw_on_side_effect);
  return maybe_result;
}

// Compile and evaluate source for the given context.
MaybeHandle<Object> DebugEvaluate::Evaluate(
    Isolate* isolate, Handle<SharedFunctionInfo> outer_info,
    Handle<Context> context, Handle<Object> receiver, Handle<String> source,
    bool throw_on_side_effect) {
  Handle<JSFunction> eval_fun;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, eval_fun,
      Compiler::GetFunctionFromEval(
          source, outer_info, context, LanguageMode::kSloppy,
          NO_PARSE_RESTRICTION, kNoSourcePosition, kNoSourcePosition,
          kNoSourcePosition, ParsingWhileDebugging::kYes),
      Object);

  Handle<Object> result;
  bool success = false;
  if (throw_on_side_effect) isolate->debug()->StartSideEffectCheckMode();
  success = Execution::Call(isolate, eval_fun, receiver, 0, nullptr)
                .ToHandle(&result);
  if (throw_on_side_effect) isolate->debug()->StopSideEffectCheckMode();
  if (!success) DCHECK(isolate->has_exception());
  return success ? result : MaybeHandle<Object>();
}

Handle<SharedFunctionInfo> DebugEvaluate::ContextBuilder::outer_info() const {
  return handle(frame_inspector_.GetFunction()->shared(), isolate_);
}

DebugEvaluate::ContextBuilder::ContextBuilder(Isolate* isolate,
                                              JavaScriptFrame* frame,
                                              int inlined_jsframe_index)
    : isolate_(isolate),
      frame_inspector_(frame, inlined_jsframe_index, isolate),
      scope_iterator_(isolate, &frame_inspector_,
                      ScopeIterator::ReparseStrategy::kScriptIfNeeded) {
  Handle<Context> outer_context(frame_inspector_.GetFunction()->context(),
                                isolate);
  evaluation_context_ = outer_context;
  Factory* factory = isolate->factory();

  if (scope_iterator_.Done()) return;

  // To evaluate as if we were running eval at the point of the debug break,
  // we reconstruct the context chain as follows:
  //  - To make stack-allocated variables visible, we materialize them and
  //    use a debug-evaluate context to wrap both the materialized object and
  //    the original context.
  //  - Each scope from the break position up to the function scope is wrapped
  //    in a debug-evaluate context.
  //  - Between the function scope and the native context, we only resolve
  //    variable names that are guaranteed to not be shadowed by stack-allocated
  //    variables. ScopeInfos between the function scope and the native
  //    context have a blocklist attached to implement that.
  //  - The various block lists are calculated by the ScopeIterator during
  //    iteration.
  // Context::Lookup has special handling for debug-evaluate contexts:
  //  - Look up in the materialized stack variables.
  //  - Look up in the original context.
  //  - Once we have seen a debug-evaluate context we start to take the
  //    block lists into account before moving up the context chain.
  for (; scope_iterator_.InInnerScope(); scope_iterator_.Next()) {
    ScopeIterator::ScopeType scope_type = scope_iterator_.Type();
    if (scope_type == ScopeIterator::ScopeTypeScript) break;
    ContextChainElement context_chain_element;
    if (scope_type == ScopeIterator::ScopeTypeLocal ||
        scope_iterator_.DeclaresLocals(ScopeIterator::Mode::STACK)) {
      context_chain_element.materialized_object =
          scope_iterator_.ScopeObject(ScopeIterator::Mode::STACK);
    }
    if (scope_iterator_.HasContext()) {
      context_chain_element.wrapped_context = scope_iterator_.CurrentContext();
    }
    context_chain_.push_back(context_chain_element);
  }

  Handle<ScopeInfo> scope_info =
      IsNativeContext(*evaluation_context_)
          ? Handle<ScopeInfo>::null()
          : handle(evaluation_context_->scope_info(), isolate);
  for (auto rit = context_chain_.rbegin(); rit != context_chain_.rend();
       rit++) {
    ContextChainElement element = *rit;
    scope_info = ScopeInfo::CreateForWithScope(isolate, scope_info);
    scope_info->SetIsDebugEvaluateScope();

    // In the case where the "paused function scope" is the script scope
    // itself, we don't need (and don't have) a blocklist.
    const bool paused_scope_is_script_scope =
        scope_iterator_.Done() || scope_iterator_.InInnerScope();
    if (rit == context_chain_.rbegin() && !paused_scope_is_script_scope) {
      // The DebugEvaluateContext we create for the closure scope is the only
      // DebugEvaluateContext with a block list. This means we'll retrieve
      // the existing block list from the paused function scope
      // and also associate the temporary scope_info we create here with that
      // blocklist.
      Handle<ScopeInfo> function_scope_info = handle(
          frame_inspector_.GetFunction()->shared()->scope_info(), isolate_);
      Handle<Object> block_list = handle(
          isolate_->LocalsBlockListCacheGet(function_scope_info), isolate_);
      CHECK(IsStringSet(*block_list));
      isolate_->LocalsBlockListCacheSet(scope_info, Handle<ScopeInfo>::null(),
                                        Handle<StringSet>::cast(block_list));
    }

    evaluation_context_ = factory->NewDebugEvaluateContext(
        evaluation_context_, scope_info, element.materialized_object,
        element.wrapped_context);
  }
}

void DebugEvaluate::ContextBuilder::UpdateValues() {
  scope_iterator_.Restart();
  for (ContextChainElement& element : context_chain_) {
    if (!element.materialized_object.is_null()) {
      Handle<FixedArray> keys =
          KeyAccumulator::GetKeys(isolate_, element.materialized_object,
                                  KeyCollectionMode::kOwnOnly,
                                  ENUMERABLE_STRINGS)
              .ToHandleChecked();

      for (int i = 0; i < keys->length(); i++) {
        DCHECK(IsString(keys->get(i)));
        Handle<String> key(String::cast(keys->get(i)), isolate_);
        Handle<Object> value = JSReceiver::GetDataProperty(
            isolate_, element.materialized_object, key);
        scope_iterator_.SetVariableValue(key, value);
      }
    }
    scope_iterator_.Next();
  }
}

// static
bool DebugEvaluate::IsSideEffectFreeIntrinsic(Runtime::FunctionId id) {
// Use macro to include only the non-inlined version of an intrinsic.
#define INTRINSIC_ALLOWLIST(V)           \
  /* Conversions */                      \
  V(NumberToStringSlow)                  \
  V(ToBigInt)                            \
  V(ToLength)                            \
  V(ToNumber)                            \
  V(ToObject)                            \
  V(ToString)                            \
  /* Type checks */                      \
  V(IsArray)                             \
  V(IsJSProxy)                           \
  V(IsJSReceiver)                        \
  V(IsRegExp)                            \
  V(IsSmi)                               \
  /* Loads */                            \
  V(LoadLookupSlotForCall)               \
  V(GetPrivateMember)                    \
  V(GetProperty)                         \
  /* Arrays */                           \
  V(ArraySpeciesConstructor)             \
  V(HasFastPackedElements)               \
  V(NewArray)                            \
  V(NormalizeElements)                   \
  V(TypedArrayGetBuffer)                 \
  /* Errors */                           \
  V(NewTypeError)                        \
  V(ReThrow)                             \
  V(ThrowCalledNonCallable)              \
  V(ThrowInvalidStringLength)            \
  V(ThrowIteratorError)                  \
  V(ThrowIteratorResultNotAnObject)      \
  V(ThrowPatternAssignmentNonCoercible)  \
  V(ThrowReferenceError)                 \
  V(ThrowSymbolIteratorInvalid)          \
  /* Strings */                          \
  V(StringReplaceOneCharWithString)      \
  V(StringSubstring)                     \
  V(StringToNumber)                      \
  /* BigInts */                          \
  V(BigIntEqualToBigInt)                 \
  V(BigIntToBoolean)                     \
  V(BigIntToNumber)                      \
  /* Literals */                         \
  V(CreateArrayLiteral)                  \
  V(CreateObjectLiteral)                 \
  V(CreateRegExpLiteral)                 \
  V(DefineClass)                         \
  /* Called from builtins */             \
  V(AllocateInYoungGeneration)           \
  V(AllocateInOldGeneration)             \
  V(AllocateSeqOneByteString)            \
  V(AllocateSeqTwoByteString)            \
  V(ArrayIncludes_Slow)                  \
  V(ArrayIndexOf)                        \
  V(ArrayIsArray)                        \
  V(GetFunctionName)                     \
  V(GetOwnPropertyDescriptor)            \
  V(GlobalPrint)                         \
  V(HasProperty)                         \
  V(ObjectCreate)                        \
  V(ObjectEntries)                       \
  V(ObjectEntriesSkipFastPath)           \
  V(ObjectHasOwnProperty)                \
  V(ObjectKeys)                          \
  V(ObjectValues)                        \
  V(ObjectValuesSkipFastPath)            \
  V(ObjectGetOwnPropertyNames)           \
  V(ObjectGetOwnPropertyNamesTryFast)    \
  V(ObjectIsExtensible)                  \
  V(RegExpInitializeAndCompile)          \
  V(StackGuard)                          \
  V(HandleNoHeapWritesInterrupts)        \
  V(StringAdd)                           \
  V(StringCharCodeAt)                    \
  V(StringEqual)                         \
  V(StringParseFloat)                    \
  V(StringParseInt)                      \
  V(SymbolDescriptiveString)             \
  V(ThrowRangeError)                     \
  V(ThrowTypeError)                      \
  V(ToName)                              \
  V(TransitionElementsKind)              \
  /* Misc. */                            \
  V(Call)                                \
  V(CompleteInobjectSlackTrackingForMap) \
  V(HasInPrototypeChain)                 \
  V(IncrementUseCounter)                 \
  V(MaxSmi)                              \
  V(NewObject)                           \
  V(StringMaxLength)                     \
  V(StringToArray)                       \
  V(AsyncFunctionEnter)                  \
  V(AsyncFunctionResolve)                \
  /* Test */                             \
  V(GetOptimizationStatus)               \
  V(OptimizeFunctionOnNextCall)          \
  V(OptimizeOsr)

// Intrinsics with inline versions have to be allowlisted here a second time.
#define INLINE_INTRINSIC_ALLOWLIST(V) \
  V(AsyncFunctionEnter)               \
  V(AsyncFunctionResolve)

#define CASE(Name) case Runtime::k##Name:
#define INLINE_CASE(Name) case Runtime::kInline##Name:
  switch (id) {
    INTRINSIC_ALLOWLIST(CASE)
    INLINE_INTRINSIC_ALLOWLIST(INLINE_CASE)
    return true;
    default:
      if (v8_flags.trace_side_effect_free_debug_evaluate) {
        PrintF("[debug-evaluate] intrinsic %s may cause side effect.\n",
               Runtime::FunctionForId(id)->name);
      }
      return false;
  }

#undef CASE
#undef INLINE_CASE
#undef INTRINSIC_ALLOWLIST
#undef INLINE_INTRINSIC_ALLOWLIST
}

namespace {

bool BytecodeHasNoSideEffect(interpreter::Bytecode bytecode) {
  using interpreter::Bytecode;
  using interpreter::Bytecodes;
  if (Bytecodes::IsWithoutExternalSideEffects(bytecode)) return true;
  if (Bytecodes::IsCallOrConstruct(bytecode)) return true;
  if (Bytecodes::IsJumpIfToBoolean(bytecode)) return true;
  if (Bytecodes::IsPrefixScalingBytecode(bytecode)) return true;
  switch (bytecode) {
    // Allowlist for bytecodes.
    // Loads.
    case Bytecode::kLdaLookupSlot:
    case Bytecode::kLdaGlobal:
    case Bytecode::kGetNamedProperty:
    case Bytecode::kGetKeyedProperty:
    case Bytecode::kLdaGlobalInsideTypeof:
    case Bytecode::kLdaLookupSlotInsideTypeof:
    case Bytecode::kGetIterator:
    // Arithmetics.
    case Bytecode::kAdd:
    case Bytecode::kAddSmi:
    case Bytecode::kSub:
    case Bytecode::kSubSmi:
    case Bytecode::kMul:
    case Bytecode::kMulSmi:
    case Bytecode::kDiv:
    case Bytecode::kDivSmi:
    case Bytecode::kMod:
    case Bytecode::kModSmi:
    case Bytecode::kExp:
    case Bytecode::kExpSmi:
    case Bytecode::kNegate:
    case Bytecode::kBitwiseAnd:
    case Bytecode::kBitwiseAndSmi:
    case Bytecode::kBitwiseNot:
    case Bytecode::kBitwiseOr:
    case Bytecode::kBitwiseOrSmi:
    case Bytecode::kBitwiseXor:
    case Bytecode::kBitwiseXorSmi:
    case Bytecode::kShiftLeft:
    case Bytecode::kShiftLeftSmi:
    case Bytecode::kShiftRight:
    case Bytecode::kShiftRightSmi:
    case Bytecode::kShiftRightLogical:
    case Bytecode::kShiftRightLogicalSmi:
    case Bytecode::kInc:
    case Bytecode::kDec:
    case Bytecode::kLogicalNot:
    case Bytecode::kToBooleanLogicalNot:
    case Bytecode::kTypeOf:
    // Contexts.
    case Bytecode::kCreateBlockContext:
    case Bytecode::kCreateCatchContext:
    case Bytecode::kCreateFunctionContext:
    case Bytecode::kCreateEvalContext:
    case Bytecode::kCreateWithContext:
    // Literals.
    case Bytecode::kCreateArrayLiteral:
    case Bytecode::kCreateEmptyArrayLiteral:
    case Bytecode::kCreateArrayFromIterable:
    case Bytecode::kCreateObjectLiteral:
    case Bytecode::kCreateEmptyObjectLiteral:
    case Bytecode::kCreateRegExpLiteral:
    // Allocations.
    case Bytecode::kCreateClosure:
    case Bytecode::kCreateUnmappedArguments:
    case Bytecode::kCreateRestParameter:
    // Comparisons.
    case Bytecode::kTestEqual:
    case Bytecode::kTestEqualStrict:
    case Bytecode::kTestLessThan:
    case Bytecode::kTestLessThanOrEqual:
    case Bytecode::kTestGreaterThan:
    case Bytecode::kTestGreaterThanOrEqual:
    case Bytecode::kTestInstanceOf:
    case Bytecode::kTestIn:
    case Bytecode::kTestReferenceEqual:
    case Bytecode::kTestUndetectable:
    case Bytecode::kTestTypeOf:
    case Bytecode::kTestUndefined:
    case Bytecode::kTestNull:
    // Conversions.
    case Bytecode::kToObject:
    case Bytecode::kToName:
    case Bytecode::kToNumber:
    case Bytecode::kToNumeric:
    case Bytecode::kToString:
    case Bytecode::kToBoolean:
    // Misc.
    case Bytecode::kIncBlockCounter:  // Coverage counters.
    case Bytecode::kForInEnumerate:
    case Bytecode::kForInPrepare:
    case Bytecode::kForInContinue:
    case Bytecode::kForInNext:
    case Bytecode::kForInStep:
    case Bytecode::kJumpLoop:
    case Bytecode::kThrow:
    case Bytecode::kReThrow:
    case Bytecode::kThrowReferenceErrorIfHole:
    case Bytecode::kThrowSuperNotCalledIfHole:
    case Bytecode::kThrowSuperAlreadyCalledIfNotHole:
    case Bytecode::kIllegal:
    case Bytecode::kCallJSRuntime:
    case Bytecode::kReturn:
    case Bytecode::kSetPendingMessage:
      return true;
    default:
      return false;
  }
}

DebugInfo::SideEffectState BuiltinGetSideEffectState(Builtin id) {
  switch (id) {
    // Allowlist for builtins.
    // Object builtins.
    case Builtin::kObjectConstructor:
    case Builtin::kObjectCreate:
    case Builtin::kObjectEntries:
    case Builtin::kObjectGetOwnPropertyDescriptor:
    case Builtin::kObjectGetOwnPropertyDescriptors:
    case Builtin::kObjectGetOwnPropertyNames:
    case Builtin::kObjectGetOwnPropertySymbols:
    case Builtin::kObjectGetPrototypeOf:
    case Builtin::kObjectGroupBy:
    case Builtin::kObjectHasOwn:
    case Builtin::kObjectIs:
    case Builtin::kObjectIsExtensible:
    case Builtin::kObjectIsFrozen:
    case Builtin::kObjectIsSealed:
    case Builtin::kObjectKeys:
    case Builtin::kObjectPrototypeValueOf:
    case Builtin::kObjectValues:
    case Builtin::kObjectPrototypeHasOwnProperty:
    case Builtin::kObjectPrototypeIsPrototypeOf:
    case Builtin::kObjectPrototypePropertyIsEnumerable:
    case Builtin::kObjectPrototypeToString:
    case Builtin::kObjectPrototypeToLocaleString:
    // Array builtins.
    case Builtin::kArrayIsArray:
    case Builtin::kArrayConstructor:
    case Builtin::kArrayIndexOf:
    case Builtin::kArrayPrototypeValues:
    case Builtin::kArrayIncludes:
    case Builtin::kArrayPrototypeAt:
    case Builtin::kArrayPrototypeConcat:
    case Builtin::kArrayPrototypeEntries:
    case Builtin::kArrayPrototypeFind:
    case Builtin::kArrayPrototypeFindIndex:
    case Builtin::kArrayPrototypeFindLast:
    case Builtin::kArrayPrototypeFindLastIndex:
    case Builtin::kArrayPrototypeFlat:
    case Builtin::kArrayPrototypeFlatMap:
    case Builtin::kArrayPrototypeJoin:
    case Builtin::kArrayPrototypeKeys:
    case Builtin::kArrayPrototypeLastIndexOf:
    case Builtin::kArrayPrototypeSlice:
    case Builtin::kArrayPrototypeToLocaleString:
    case Builtin::kArrayPrototypeToReversed:
    case Builtin::kArrayPrototypeToSorted:
    case Builtin::kArrayPrototypeToSpliced:
    case Builtin::kArrayPrototypeToString:
    case Builtin::kArrayPrototypeWith:
    case Builtin::kArrayForEach:
    case Builtin::kArrayEvery:
    case Builtin::kArraySome:
    case Builtin::kArrayConcat:
    case Builtin::kArrayFilter:
    case Builtin::kArrayMap:
    case Builtin::kArrayReduce:
    case Builtin::kArrayReduceRight:
    // Trace builtins.
    case Builtin::kIsTraceCategoryEnabled:
    case Builtin::kTrace:
    // TypedArray builtins.
    case Builtin::kTypedArrayConstructor:
    case Builtin::kTypedArrayOf:
    case Builtin::kTypedArrayPrototypeAt:
    case Builtin::kTypedArrayPrototypeBuffer:
    case Builtin::kTypedArrayPrototypeByteLength:
    case Builtin::kTypedArrayPrototypeByteOffset:
    case Builtin::kTypedArrayPrototypeLength:
    case Builtin::kTypedArrayPrototypeEntries:
    case Builtin::kTypedArrayPrototypeKeys:
    case Builtin::kTypedArrayPrototypeValues:
    case Builtin::kTypedArrayPrototypeFind:
    case Builtin::kTypedArrayPrototypeFindIndex:
    case Builtin::kTypedArrayPrototypeFindLast:
    case Builtin::kTypedArrayPrototypeFindLastIndex:
    case Builtin::kTypedArrayPrototypeIncludes:
    case Builtin::kTypedArrayPrototypeJoin:
    case Builtin::kTypedArrayPrototypeIndexOf:
    case Builtin::kTypedArrayPrototypeLastIndexOf:
    case Builtin::kTypedArrayPrototypeSlice:
    case Builtin::kTypedArrayPrototypeSubArray:
    case Builtin::kTypedArrayPrototypeEvery:
    case Builtin::kTypedArrayPrototypeSome:
    case Builtin::kTypedArrayPrototypeToLocaleString:
    case Builtin::kTypedArrayPrototypeFilter:
    case Builtin::kTypedArrayPrototypeMap:
    case Builtin::kTypedArrayPrototypeReduce:
    case Builtin::kTypedArrayPrototypeReduceRight:
    case Builtin::kTypedArrayPrototypeForEach:
    case Builtin::kTypedArrayPrototypeToReversed:
    case Builtin::kTypedArrayPrototypeToSorted:
    case Builtin::kTypedArrayPrototypeWith:
    // ArrayBuffer builtins.
    case Builtin::kArrayBufferConstructor:
    case Builtin::kArrayBufferPrototypeGetByteLength:
    case Builtin::kArrayBufferIsView:
    case Builtin::kArrayBufferPrototypeSlice:
    case Builtin::kReturnReceiver:
    // DataView builtins.
    case Builtin::kDataViewConstructor:
    case Builtin::kDataViewPrototypeGetBuffer:
    case Builtin::kDataViewPrototypeGetByteLength:
    case Builtin::kDataViewPrototypeGetByteOffset:
    case Builtin::kDataViewPrototypeGetInt8:
    case Builtin::kDataViewPrototypeGetUint8:
    case Builtin::kDataViewPrototypeGetInt16:
    case Builtin::kDataViewPrototypeGetUint16:
    case Builtin::kDataViewPrototypeGetInt32:
    case Builtin::kDataViewPrototypeGetUint32:
    case Builtin::kDataViewPrototypeGetFloat16:
    case Builtin::kDataViewPrototypeGetFloat32:
    case Builtin::kDataViewPrototypeGetFloat64:
    case Builtin::kDataViewPrototypeGetBigInt64:
    case Builtin::kDataViewPrototypeGetBigUint64:
    // Boolean bulitins.
    case Builtin::kBooleanConstructor:
    case Builtin::kBooleanPrototypeToString:
    case Builtin::kBooleanPrototypeValueOf:
    // Date builtins.
    case Builtin::kDateConstructor:
    case Builtin::kDateNow:
    case Builtin::kDateParse:
    case Builtin::kDatePrototypeGetDate:
    case Builtin::kDatePrototypeGetDay:
    case Builtin::kDatePrototypeGetFullYear:
    case Builtin::kDatePrototypeGetHours:
    case Builtin::kDatePrototypeGetMilliseconds:
    case Builtin::kDatePrototypeGetMinutes:
    case Builtin::kDatePrototypeGetMonth:
    case Builtin::kDatePrototypeGetSeconds:
    case Builtin::kDatePrototypeGetTime:
    case Builtin::kDatePrototypeGetTimezoneOffset:
    case Builtin::kDatePrototypeGetUTCDate:
    case Builtin::kDatePrototypeGetUTCDay:
    case Builtin::kDatePrototypeGetUTCFullYear:
    case Builtin::kDatePrototypeGetUTCHours:
    case Builtin::kDatePrototypeGetUTCMilliseconds:
    case Builtin::kDatePrototypeGetUTCMinutes:
    case Builtin::kDatePrototypeGetUTCMonth:
    case Builtin::kDatePrototypeGetUTCSeconds:
    case Builtin::kDatePrototypeGetYear:
    case Builtin::kDatePrototypeToDateString:
    case Builtin::kDatePrototypeToISOString:
    case Builtin::kDatePrototypeToUTCString:
    case Builtin::kDatePrototypeToString:
#ifdef V8_INTL_SUPPORT
    case Builtin::kDatePrototypeToLocaleString:
    case Builtin::kDatePrototypeToLocaleDateString:
    case Builtin::kDatePrototypeToLocaleTimeString:
#endif
    case Builtin::kDatePrototypeToTimeString:
    case Builtin::kDatePrototypeToJson:
    case Builtin::kDatePrototypeToPrimitive:
    case Builtin::kDatePrototypeValueOf:
    // Map builtins.
    case Builtin::kMapConstructor:
    case Builtin::kMapGroupBy:
    case Builtin::kMapPrototypeForEach:
    case Builtin::kMapPrototypeGet:
    case Builtin::kMapPrototypeHas:
    case Builtin::kMapPrototypeEntries:
    case Builtin::kMapPrototypeGetSize:
    case Builtin::kMapPrototypeKeys:
    case Builtin::kMapPrototypeValues:
    // WeakMap builtins.
    case Builtin::kWeakMapConstructor:
    case Builtin::kWeakMapGet:
    case Builtin::kWeakMapPrototypeHas:
    // Math builtins.
    case Builtin::kMathAbs:
    case Builtin::kMathAcos:
    case Builtin::kMathAcosh:
    case Builtin::kMathAsin:
    case Builtin::kMathAsinh:
    case Builtin::kMathAtan:
    case Builtin::kMathAtanh:
    case Builtin::kMathAtan2:
    case Builtin::kMathCeil:
    case Builtin::kMathCbrt:
    case Builtin::kMathExpm1:
    case Builtin::kMathClz32:
    case Builtin::kMathCos:
    case Builtin::kMathCosh:
    case Builtin::kMathExp:
    case Builtin::kMathFloor:
    case Builtin::kMathF16round:
    case Builtin::kMathFround:
    case Builtin::kMathHypot:
    case Builtin::kMathImul:
    case Builtin::kMathLog:
    case Builtin::kMathLog1p:
    case Builtin::kMathLog2:
    case Builtin::kMathLog10:
    case Builtin::kMathMax:
    case Builtin::kMathMin:
    case Builtin::kMathPow:
    case Builtin::kMathRound:
    case Builtin::kMathSign:
    case Builtin::kMathSin:
    case Builtin::kMathSinh:
    case Builtin::kMathSqrt:
    case Builtin::kMathTan:
    case Builtin::kMathTanh:
    case Builtin::kMathTrunc:
    // Number builtins.
    case Builtin::kNumberConstructor:
    case Builtin::kNumberIsFinite:
    case Builtin::kNumberIsInteger:
    case Builtin::kNumberIsNaN:
    case Builtin::kNumberIsSafeInteger:
    case Builtin::kNumberParseFloat:
    case Builtin::kNumberParseInt:
    case Builtin::kNumberPrototypeToExponential:
    case Builtin::kNumberPrototypeToFixed:
    case Builtin::kNumberPrototypeToPrecision:
    case Builtin::kNumberPrototypeToString:
    case Builtin::kNumberPrototypeToLocaleString:
    case Builtin::kNumberPrototypeValueOf:
    // BigInt builtins.
    case Builtin::kBigIntConstructor:
    case Builtin::kBigIntAsIntN:
    case Builtin::kBigIntAsUintN:
    case Builtin::kBigIntPrototypeToString:
    case Builtin::kBigIntPrototypeValueOf:
    // Set builtins.
    case Builtin::kSetConstructor:
    case Builtin::kSetPrototypeEntries:
    case Builtin::kSetPrototypeForEach:
    case Builtin::kSetPrototypeGetSize:
    case Builtin::kSetPrototypeHas:
    case Builtin::kSetPrototypeValues:
    // WeakSet builtins.
    case Builtin::kWeakSetConstructor:
    case Builtin::kWeakSetPrototypeHas:
    // String builtins. Strings are immutable.
    case Builtin::kStringFromCharCode:
    case Builtin::kStringFromCodePoint:
    case Builtin::kStringConstructor:
    case Builtin::kStringListFromIterable:
    case Builtin::kStringPrototypeAnchor:
    case Builtin::kStringPrototypeAt:
    case Builtin::kStringPrototypeBig:
    case Builtin::kStringPrototypeBlink:
    case Builtin::kStringPrototypeBold:
    case Builtin::kStringPrototypeCharAt:
    case Builtin::kStringPrototypeCharCodeAt:
    case Builtin::kStringPrototypeCodePointAt:
    case Builtin::kStringPrototypeConcat:
    case Builtin::kStringPrototypeEndsWith:
    case Builtin::kStringPrototypeFixed:
    case Builtin::kStringPrototypeFontcolor:
    case Builtin::kStringPrototypeFontsize:
    case Builtin::kStringPrototypeIncludes:
    case Builtin::kStringPrototypeIndexOf:
    case Builtin::kStringPrototypeIsWellFormed:
    case Builtin::kStringPrototypeItalics:
    case Builtin::kStringPrototypeLastIndexOf:
    case Builtin::kStringPrototypeLink:
    case Builtin::kStringPrototypeMatchAll:
    case Builtin::kStringPrototypePadEnd:
    case Builtin::kStringPrototypePadStart:
    case Builtin::kStringPrototypeRepeat:
    case Builtin::kStringPrototypeSlice:
    case Builtin::kStringPrototypeSmall:
    case Builtin::kStringPrototypeStartsWith:
    case Builtin::kStringSlowFlatten:
    case Builtin::kStringPrototypeStrike:
    case Builtin::kStringPrototypeSub:
    case Builtin::kStringPrototypeSubstr:
    case Builtin::kStringPrototypeSubstring:
    case Builtin::kStringPrototypeSup:
    case Builtin::kStringPrototypeToString:
#ifndef V8_INTL_SUPPORT
    case Builtin::kStringPrototypeToLowerCase:
    case Builtin::kStringPrototypeToUpperCase:
#endif
    case Builtin::kStringPrototypeToWellFormed:
    case Builtin::kStringPrototypeTrim:
    case Builtin::kStringPrototypeTrimEnd:
    case Builtin::kStringPrototypeTrimStart:
    case Builtin::kStringPrototypeValueOf:
    case Builtin::kStringToNumber:
    case Builtin::kStringSubstring:
    // Symbol builtins.
    case Builtin::kSymbolConstructor:
    case Builtin::kSymbolKeyFor:
    case Builtin::kSymbolPrototypeToString:
    case Builtin::kSymbolPrototypeValueOf:
    case Builtin::kSymbolPrototypeToPrimitive:
    // JSON builtins.
    case Builtin::kJsonParse:
    case Builtin::kJsonStringify:
    // Global function builtins.
    case Builtin::kGlobalDecodeURI:
    case Builtin::kGlobalDecodeURIComponent:
    case Builtin::kGlobalEncodeURI:
    case Builtin::kGlobalEncodeURIComponent:
    case Builtin::kGlobalEscape:
    case Builtin::kGlobalUnescape:
    case Builtin::kGlobalIsFinite:
    case Builtin::kGlobalIsNaN:
    // Function builtins.
    case Builtin::kFunctionPrototypeToString:
    case Builtin::kFunctionPrototypeBind:
    case Builtin::kFastFunctionPrototypeBind:
    case Builtin::kFunctionPrototypeCall:
    case Builtin::kFunctionPrototypeApply:
    // Error builtins.
    case Builtin::kErrorConstructor:
    // RegExp builtins.
    case Builtin::kRegExpConstructor:
    // Reflect builtins.
    case Builtin::kReflectApply:
    case Builtin::kReflectConstruct:
    case Builtin::kReflectGetOwnPropertyDescriptor:
    case Builtin::kReflectGetPrototypeOf:
    case Builtin::kReflectHas:
    case Builtin::kReflectIsExtensible:
    case Builtin::kReflectOwnKeys:
    // Internal.
    case Builtin::kStrictPoisonPillThrower:
    case Builtin::kAllocateInYoungGeneration:
    case Builtin::kAllocateInOldGeneration:
    case Builtin::kConstructVarargs:
    case Builtin::kConstructWithArrayLike:
    case Builtin::kGetOwnPropertyDescriptor:
    case Builtin::kOrdinaryGetOwnPropertyDescriptor:
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    case Builtin::kGetContinuationPreservedEmbedderData:
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
      return DebugInfo::kHasNoSideEffect;

#ifdef V8_INTL_SUPPORT
    // Intl builtins.
    case Builtin::kIntlGetCanonicalLocales:
    // Intl.Collator builtins.
    case Builtin::kCollatorConstructor:
    case Builtin::kCollatorInternalCompare:
    case Builtin::kCollatorPrototypeCompare:
    case Builtin::kCollatorPrototypeResolvedOptions:
    case Builtin::kCollatorSupportedLocalesOf:
    // Intl.DateTimeFormat builtins.
    case Builtin::kDateTimeFormatConstructor:
    case Builtin::kDateTimeFormatInternalFormat:
    case Builtin::kDateTimeFormatPrototypeFormat:
    case Builtin::kDateTimeFormatPrototypeFormatRange:
    case Builtin::kDateTimeFormatPrototypeFormatRangeToParts:
    case Builtin::kDateTimeFormatPrototypeFormatToParts:
    case Builtin::kDateTimeFormatPrototypeResolvedOptions:
    case Builtin::kDateTimeFormatSupportedLocalesOf:
    // Intl.DisplayNames builtins.
    case Builtin::kDisplayNamesConstructor:
    case Builtin::kDisplayNamesPrototypeOf:
    case Builtin::kDisplayNamesPrototypeResolvedOptions:
    case Builtin::kDisplayNamesSupportedLocalesOf:
    // Intl.ListFormat builtins.
    case Builtin::kListFormatConstructor:
    case Builtin::kListFormatPrototypeFormat:
    case Builtin::kListFormatPrototypeFormatToParts:
    case Builtin::kListFormatPrototypeResolvedOptions:
    case Builtin::kListFormatSupportedLocalesOf:
    // Intl.Locale builtins.
    case Builtin::kLocaleConstructor:
    case Builtin::kLocalePrototypeBaseName:
    case Builtin::kLocalePrototypeCalendar:
    case Builtin::kLocalePrototypeCalendars:
    case Builtin::kLocalePrototypeCaseFirst:
    case Builtin::kLocalePrototypeCollation:
    case Builtin::kLocalePrototypeCollations:
    case Builtin::kLocalePrototypeGetCalendars:
    case Builtin::kLocalePrototypeGetCollations:
    case Builtin::kLocalePrototypeGetHourCycles:
    case Builtin::kLocalePrototypeGetNumberingSystems:
    case Builtin::kLocalePrototypeGetTextInfo:
    case Builtin::kLocalePrototypeGetTimeZones:
    case Builtin::kLocalePrototypeGetWeekInfo:
    case Builtin::kLocalePrototypeHourCycle:
    case Builtin::kLocalePrototypeHourCycles:
    case Builtin::kLocalePrototypeLanguage:
    case Builtin::kLocalePrototypeMaximize:
    case Builtin::kLocalePrototypeMinimize:
    case Builtin::kLocalePrototypeNumeric:
    case Builtin::kLocalePrototypeNumberingSystem:
    case Builtin::kLocalePrototypeNumberingSystems:
    case Builtin::kLocalePrototypeRegion:
    case Builtin::kLocalePrototypeScript:
    case Builtin::kLocalePrototypeTextInfo:
    case Builtin::kLocalePrototypeTimeZones:
    case Builtin::kLocalePrototypeToString:
    case Builtin::kLocalePrototypeWeekInfo:
    // Intl.NumberFormat builtins.
    case Builtin::kNumberFormatConstructor:
    case Builtin::kNumberFormatInternalFormatNumber:
    case Builtin::kNumberFormatPrototypeFormatNumber:
    case Builtin::kNumberFormatPrototypeFormatToParts:
    case Builtin::kNumberFormatPrototypeResolvedOptions:
    case Builtin::kNumberFormatSupportedLocalesOf:
    // Intl.PluralRules builtins.
    case Builtin::kPluralRulesConstructor:
    case Builtin::kPluralRulesPrototypeResolvedOptions:
    case Builtin::kPluralRulesPrototypeSelect:
    case Builtin::kPluralRulesSupportedLocalesOf:
    // Intl.RelativeTimeFormat builtins.
    case Builtin::kRelativeTimeFormatConstructor:
    case Builtin::kRelativeTimeFormatPrototypeFormat:
    case Builtin::kRelativeTimeFormatPrototypeFormatToParts:
    case Builtin::kRelativeTimeFormatPrototypeResolvedOptions:
    case Builtin::kRelativeTimeFormatSupportedLocalesOf:
      return DebugInfo::kHasNoSideEffect;
#endif  // V8_INTL_SUPPORT

    // Set builtins.
    case Builtin::kSetIteratorPrototypeNext:
    case Builtin::kSetPrototypeAdd:
    case Builtin::kSetPrototypeClear:
    case Builtin::kSetPrototypeDelete:
    // Array builtins.
    case Builtin::kArrayIteratorPrototypeNext:
    case Builtin::kArrayPrototypeFill:
    case Builtin::kArrayPrototypePop:
    case Builtin::kArrayPrototypePush:
    case Builtin::kArrayPrototypeReverse:
    case Builtin::kArrayPrototypeShift:
    case Builtin::kArrayPrototypeUnshift:
    case Builtin::kArrayPrototypeSort:
    case Builtin::kArrayPrototypeSplice:
    case Builtin::kArrayUnshift:
    // Map builtins.
    case Builtin::kMapIteratorPrototypeNext:
    case Builtin::kMapPrototypeClear:
    case Builtin::kMapPrototypeDelete:
    case Builtin::kMapPrototypeSet:
    // Date builtins.
    case Builtin::kDatePrototypeSetDate:
    case Builtin::kDatePrototypeSetFullYear:
    case Builtin::kDatePrototypeSetHours:
    case Builtin::kDatePrototypeSetMilliseconds:
    case Builtin::kDatePrototypeSetMinutes:
    case Builtin::kDatePrototypeSetMonth:
    case Builtin::kDatePrototypeSetSeconds:
    case Builtin::kDatePrototypeSetTime:
    case Builtin::kDatePrototypeSetUTCDate:
    case Builtin::kDatePrototypeSetUTCFullYear:
    case Builtin::kDatePrototypeSetUTCHours:
    case Builtin::kDatePrototypeSetUTCMilliseconds:
    case Builtin::kDatePrototypeSetUTCMinutes:
    case Builtin::kDatePrototypeSetUTCMonth:
    case Builtin::kDatePrototypeSetUTCSeconds:
    case Builtin::kDatePrototypeSetYear:
    // RegExp builtins.
    case Builtin::kRegExpPrototypeTest:
    case Builtin::kRegExpPrototypeExec:
    case Builtin::kRegExpPrototypeSplit:
    case Builtin::kRegExpPrototypeFlagsGetter:
    case Builtin::kRegExpPrototypeGlobalGetter:
    case Builtin::kRegExpPrototypeHasIndicesGetter:
    case Builtin::kRegExpPrototypeIgnoreCaseGetter:
    case Builtin::kRegExpPrototypeMatchAll:
    case Builtin::kRegExpPrototypeMultilineGetter:
    case Builtin::kRegExpPrototypeDotAllGetter:
    case Builtin::kRegExpPrototypeUnicodeGetter:
    case Builtin::kRegExpPrototypeStickyGetter:
      return DebugInfo::kRequiresRuntimeChecks;

    default:
      if (v8_flags.trace_side_effect_free_debug_evaluate) {
        PrintF("[debug-evaluate] built-in %s may cause side effect.\n",
               Builtins::name(id));
      }
      return DebugInfo::kHasSideEffects;
  }
}

bool BytecodeRequiresRuntimeCheck(interpreter::Bytecode bytecode) {
  using interpreter::Bytecode;
  switch (bytecode) {
    case Bytecode::kSetNamedProperty:
    case Bytecode::kDefineNamedOwnProperty:
    case Bytecode::kSetKeyedProperty:
    case Bytecode::kStaInArrayLiteral:
    case Bytecode::kDefineKeyedOwnPropertyInLiteral:
    case Bytecode::kStaCurrentContextSlot:
      return true;
    default:
      return interpreter::Bytecodes::IsCallRuntime(bytecode);
  }
}

}  // anonymous namespace

// static
DebugInfo::SideEffectState DebugEvaluate::FunctionGetSideEffectState(
    Isolate* isolate, Handle<SharedFunctionInfo> info) {
  if (v8_flags.trace_side_effect_free_debug_evaluate) {
    PrintF("[debug-evaluate] Checking function %s for side effect.\n",
           info->DebugNameCStr().get());
  }

  DCHECK(info->is_compiled());
  DCHECK(!info->needs_script_context());
  if (info->HasBytecodeArray()) {
    // Check bytecodes against allowlist.
    Handle<BytecodeArray> bytecode_array(info->GetBytecodeArray(isolate),
                                         isolate);
    if (v8_flags.trace_side_effect_free_debug_evaluate) {
      Print(*bytecode_array);
    }
    bool requires_runtime_checks = false;
    for (interpreter::BytecodeArrayIterator it(bytecode_array); !it.done();
         it.Advance()) {
      interpreter::Bytecode bytecode = it.current_bytecode();
      if (BytecodeHasNoSideEffect(bytecode)) continue;
      if (BytecodeRequiresRuntimeCheck(bytecode)) {
        requires_runtime_checks = true;
        continue;
      }

      if (v8_flags.trace_side_effect_free_debug_evaluate) {
        PrintF("[debug-evaluate] bytecode %s may cause side effect.\n",
               interpreter::Bytecodes::ToString(bytecode));
      }

      // Did not match allowlist.
      return DebugInfo::kHasSideEffects;
    }
    return requires_runtime_checks ? DebugInfo::kRequiresRuntimeChecks
                                   : DebugInfo::kHasNoSideEffect;
  } else if (info->IsApiFunction()) {
    Tagged<Code> code = info->GetCode(isolate);
    if (code->is_builtin()) {
      return code->builtin_id() == Builtin::kHandleApiCallOrConstruct
                 ? DebugInfo::kHasNoSideEffect
                 : DebugInfo::kHasSideEffects;
    }
  } else {
    // Check built-ins against allowlist.
    Builtin builtin =
        info->HasBuiltinId() ? info->builtin_id() : Builtin::kNoBuiltinId;
    if (!Builtins::IsBuiltinId(builtin)) return DebugInfo::kHasSideEffects;
    DebugInfo::SideEffectState state = BuiltinGetSideEffectState(builtin);
    return state;
  }

  return DebugInfo::kHasSideEffects;
}

#ifdef DEBUG
static bool TransitivelyCalledBuiltinHasNoSideEffect(Builtin caller,
                                                     Builtin callee) {
  switch (callee) {
      // Transitively called Builtins:
    case Builtin::kAbort:
    case Builtin::kAbortCSADcheck:
    case Builtin::kAdaptorWithBuiltinExitFrame:
    case Builtin::kArrayConstructorImpl:
    case Builtin::kArrayEveryLoopContinuation:
    case Builtin::kArrayFilterLoopContinuation:
    case Builtin::kArrayFindIndexLoopContinuation:
    case Builtin::kArrayFindLoopContinuation:
    case Builtin::kArrayFindLastIndexLoopContinuation:
    case Builtin::kArrayFindLastLoopContinuation:
    case Builtin::kArrayForEachLoopContinuation:
    case Builtin::kArrayIncludesHoleyDoubles:
    case Builtin::kArrayIncludesPackedDoubles:
    case Builtin::kArrayIncludesSmi:
    case Builtin::kArrayIncludesSmiOrObject:
    case Builtin::kArrayIndexOfHoleyDoubles:
    case Builtin::kArrayIndexOfPackedDoubles:
    case Builtin::kArrayIndexOfSmi:
    case Builtin::kArrayIndexOfSmiOrObject:
    case Builtin::kArrayMapLoopContinuation:
    case Builtin::kArrayReduceLoopContinuation:
    case Builtin::kArrayReduceRightLoopContinuation:
    case Builtin::kArraySomeLoopContinuation:
    case Builtin::kArrayTimSort:
    case Builtin::kArrayTimSortIntoCopy:
    case Builtin::kCall_ReceiverIsAny:
    case Builtin::kCall_ReceiverIsNotNullOrUndefined:
    case Builtin::kCall_ReceiverIsNullOrUndefined:
    case Builtin::kCallWithArrayLike:
    case Builtin::kCEntry_Return1_ArgvOnStack_NoBuiltinExit:
    case Builtin::kCEntry_Return1_ArgvOnStack_BuiltinExit:
    case Builtin::kCEntry_Return1_ArgvInRegister_NoBuiltinExit:
    case Builtin::kCEntry_Return2_ArgvOnStack_NoBuiltinExit:
    case Builtin::kCEntry_Return2_ArgvOnStack_BuiltinExit:
    case Builtin::kCEntry_Return2_ArgvInRegister_NoBuiltinExit:
    case Builtin::kWasmCEntry:
    case Builtin::kCloneFastJSArray:
    case Builtin::kConstruct:
    case Builtin::kConvertToLocaleString:
    case Builtin::kCreateTypedArray:
    case Builtin::kDirectCEntry:
    case Builtin::kDoubleToI:
    case Builtin::kExtractFastJSArray:
    case Builtin::kFastNewObject:
    case Builtin::kFindOrderedHashMapEntry:
    case Builtin::kFindOrderedHashSetEntry:
    case Builtin::kFlattenIntoArrayWithMapFn:
    case Builtin::kFlattenIntoArrayWithoutMapFn:
    case Builtin::kGenericArrayToReversed:
    case Builtin::kGenericArrayWith:
    case Builtin::kGetProperty:
    case Builtin::kGetPropertyWithReceiver:
    case Builtin::kGroupByGeneric:
    case Builtin::kHasProperty:
    case Builtin::kCreateHTML:
    case Builtin::kNonNumberToNumber:
    case Builtin::kNonPrimitiveToPrimitive_Number:
    case Builtin::kNumberToString:
    case Builtin::kObjectToString:
    case Builtin::kOrderedHashTableHealIndex:
    case Builtin::kOrdinaryToPrimitive_Number:
    case Builtin::kOrdinaryToPrimitive_String:
    case Builtin::kParseInt:
    case Builtin::kProxyHasProperty:
    case Builtin::kProxyIsExtensible:
    case Builtin::kProxyGetPrototypeOf:
    case Builtin::kRecordWriteSaveFP:
    case Builtin::kRecordWriteIgnoreFP:
    case Builtin::kStringAdd_CheckNone:
    case Builtin::kStringEqual:
    case Builtin::kStringIndexOf:
    case Builtin::kStringRepeat:
    case Builtin::kBigIntEqual:
    case Builtin::kToInteger:
    case Builtin::kToLength:
    case Builtin::kToName:
    case Builtin::kToObject:
    case Builtin::kToString:
    case Builtin::kTypedArrayMergeSort:
#ifdef V8_IS_TSAN
    case Builtin::kTSANRelaxedStore8IgnoreFP:
    case Builtin::kTSANRelaxedStore8SaveFP:
    case Builtin::kTSANRelaxedStore16IgnoreFP:
    case Builtin::kTSANRelaxedStore16SaveFP:
    case Builtin::kTSANRelaxedStore32IgnoreFP:
    case Builtin::kTSANRelaxedStore32SaveFP:
    case Builtin::kTSANRelaxedStore64IgnoreFP:
    case Builtin::kTSANRelaxedStore64SaveFP:
    case Builtin::kTSANSeqCstStore8IgnoreFP:
    case Builtin::kTSANSeqCstStore8SaveFP:
    case Builtin::kTSANSeqCstStore16IgnoreFP:
    case Builtin::kTSANSeqCstStore16SaveFP:
    case Builtin::kTSANSeqCstStore32IgnoreFP:
    case Builtin::kTSANSeqCstStore32SaveFP:
    case Builtin::kTSANSeqCstStore64IgnoreFP:
    case Builtin::kTSANSeqCstStore64SaveFP:
    case Builtin::kTSANRelaxedLoad32IgnoreFP:
    case Builtin::kTSANRelaxedLoad32SaveFP:
    case Builtin::kTSANRelaxedLoad64IgnoreFP:
    case Builtin::kTSANRelaxedLoad64SaveFP:
#endif  // V8_IS_TSAN
    case Builtin::kWeakMapLookupHashIndex:
      return true;
    case Builtin::kJoinStackPop:
    case Builtin::kJoinStackPush:
      switch (caller) {
        case Builtin::kArrayPrototypeJoin:
        case Builtin::kArrayPrototypeToLocaleString:
        case Builtin::kTypedArrayPrototypeJoin:
        case Builtin::kTypedArrayPrototypeToLocaleString:
          return true;
        default:
          return false;
      }
    case Builtin::kFastCreateDataProperty:
      switch (caller) {
        case Builtin::kArrayPrototypeSlice:
        case Builtin::kArrayPrototypeToSpliced:
        case Builtin::kArrayPrototypeWith:
        case Builtin::kArrayFilter:
          return true;
        default:
          return false;
      }
    case Builtin::kSetProperty:
      switch (caller) {
        case Builtin::kArrayPrototypeSlice:
        case Builtin::kArrayPrototypeToSorted:
        case Builtin::kTypedArrayPrototypeMap:
        case Builtin::kStringPrototypeMatchAll:
          return true;
        default:
          return false;
      }
    default:
      return false;
  }
}

// static
void DebugEvaluate::VerifyTransitiveBuiltins(Isolate* isolate) {
  // TODO(yangguo): also check runtime calls.
  bool failed = false;
  bool sanity_check = false;
  for (Builtin caller = Builtins::kFirst; caller <= Builtins::kLast; ++caller) {
    DebugInfo::SideEffectState state = BuiltinGetSideEffectState(caller);
    if (state != DebugInfo::kHasNoSideEffect) continue;
    Tagged<Code> code = isolate->builtins()->code(caller);
    int mode = RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
               RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET);

    for (RelocIterator it(code, mode); !it.done(); it.next()) {
      RelocInfo* rinfo = it.rinfo();
      DCHECK(RelocInfo::IsCodeTargetMode(rinfo->rmode()));
      Tagged<Code> lookup_result =
          isolate->heap()->FindCodeForInnerPointer(rinfo->target_address());
      Builtin callee = lookup_result->builtin_id();
      if (BuiltinGetSideEffectState(callee) == DebugInfo::kHasNoSideEffect) {
        continue;
      }
      if (TransitivelyCalledBuiltinHasNoSideEffect(caller, callee)) {
        sanity_check = true;
        continue;
      }
      PrintF("Allowlisted builtin %s calls non-allowlisted builtin %s\n",
             Builtins::name(caller), Builtins::name(callee));
      failed = true;
    }
  }
  CHECK(!failed);
#if defined(V8_TARGET_ARCH_PPC) || defined(V8_TARGET_ARCH_PPC64) ||      \
    defined(V8_TARGET_ARCH_MIPS64) || defined(V8_TARGET_ARCH_RISCV32) || \
    defined(V8_TARGET_ARCH_RISCV64)
  // Isolate-independent builtin calls and jumps do not emit reloc infos
  // on PPC. We try to avoid using PC relative code due to performance
  // issue with especially older hardwares.
  // MIPS64 doesn't have PC relative code currently.
  // TODO(mips): Add PC relative code to MIPS64.
  USE(sanity_check);
#else
  CHECK(sanity_check);
#endif
}
#endif  // DEBUG

// static
void DebugEvaluate::ApplySideEffectChecks(
    Handle<BytecodeArray> bytecode_array) {
  for (interpreter::BytecodeArrayIterator it(bytecode_array); !it.done();
       it.Advance()) {
    interpreter::Bytecode bytecode = it.current_bytecode();
    if (BytecodeRequiresRuntimeCheck(bytecode)) it.ApplyDebugBreak();
  }
}

}  // namespace internal
}  // namespace v8
