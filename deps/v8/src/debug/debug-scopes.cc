// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-scopes.h"

#include <memory>

#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/common/globals.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/source-text-module.h"
#include "src/objects/string-set.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

ScopeIterator::ScopeIterator(Isolate* isolate, FrameInspector* frame_inspector,
                             ReparseStrategy strategy)
    : isolate_(isolate),
      frame_inspector_(frame_inspector),
      function_(frame_inspector_->GetFunction()),
      script_(frame_inspector_->GetScript()),
      locals_(StringSet::New(isolate)) {
  if (!IsContext(*frame_inspector->GetContext())) {
    // Optimized frame, context or function cannot be materialized. Give up.
    return;
  }
  context_ = Cast<Context>(frame_inspector->GetContext());

#if V8_ENABLE_WEBASSEMBLY
  // We should not instantiate a ScopeIterator for wasm frames.
  DCHECK_NE(Script::Type::kWasm, frame_inspector->GetScript()->type());
#endif  // V8_ENABLE_WEBASSEMBLY

  TryParseAndRetrieveScopes(strategy);
}

ScopeIterator::~ScopeIterator() = default;

Handle<Object> ScopeIterator::GetFunctionDebugName() const {
  if (!function_.is_null()) return JSFunction::GetDebugName(function_);

  if (!IsNativeContext(*context_)) {
    DisallowGarbageCollection no_gc;
    Tagged<ScopeInfo> closure_info = context_->closure_context()->scope_info();
    Handle<String> debug_name(closure_info->FunctionDebugName(), isolate_);
    if (debug_name->length() > 0) return debug_name;
  }
  return isolate_->factory()->undefined_value();
}

ScopeIterator::ScopeIterator(Isolate* isolate,
                             DirectHandle<JSFunction> function)
    : isolate_(isolate),
      context_(function->context(), isolate),
      locals_(StringSet::New(isolate)) {
  if (!function->shared()->IsSubjectToDebugging()) {
    context_ = Handle<Context>();
    return;
  }
  script_ = handle(Cast<Script>(function->shared()->script()), isolate);
  UnwrapEvaluationContext();
}

ScopeIterator::ScopeIterator(Isolate* isolate,
                             Handle<JSGeneratorObject> generator)
    : isolate_(isolate),
      generator_(generator),
      function_(generator->function(), isolate),
      context_(generator->context(), isolate),
      script_(Cast<Script>(function_->shared()->script()), isolate),
      locals_(StringSet::New(isolate)) {
  CHECK(function_->shared()->IsSubjectToDebugging());
  TryParseAndRetrieveScopes(ReparseStrategy::kFunctionLiteral);
}

void ScopeIterator::Restart() {
  DCHECK_NOT_NULL(frame_inspector_);
  function_ = frame_inspector_->GetFunction();
  context_ = Cast<Context>(frame_inspector_->GetContext());
  current_scope_ = start_scope_;
  DCHECK_NOT_NULL(current_scope_);
  UnwrapEvaluationContext();
  seen_script_scope_ = false;
  calculate_blocklists_ = false;
}

namespace {

// Takes the scope of a parsed script, a function and a break location
// inside the function. The result is the innermost lexical scope around
// the break point, which serves as the starting point of the ScopeIterator.
// And the scope of the function that was passed in (called closure scope).
//
// The start scope is guaranteed to be either the closure scope itself,
// or a child of the closure scope.
class ScopeChainRetriever {
 public:
  ScopeChainRetriever(DeclarationScope* scope,
                      DirectHandle<JSFunction> function, int position)
      : scope_(scope),
        break_scope_start_(function->shared()->StartPosition()),
        break_scope_end_(function->shared()->EndPosition()),
        break_scope_type_(function->shared()->scope_info()->scope_type()),
        position_(position) {
    DCHECK_NOT_NULL(scope);
    RetrieveScopes();
  }

  DeclarationScope* ClosureScope() { return closure_scope_; }
  Scope* StartScope() { return start_scope_; }

 private:
  DeclarationScope* scope_;
  const int break_scope_start_;
  const int break_scope_end_;
  const ScopeType break_scope_type_;
  const int position_;

  DeclarationScope* closure_scope_ = nullptr;
  Scope* start_scope_ = nullptr;

  void RetrieveScopes() {
    // 1. Find the closure scope with a DFS.
    RetrieveClosureScope(scope_);
    DCHECK_NOT_NULL(closure_scope_);

    // 2. Starting from the closure scope search inwards. Given that V8's scope
    //    tree doesn't guarantee that siblings don't overlap, we look at all
    //    scopes and pick the one with the tightest bounds around `position_`.
    start_scope_ = closure_scope_;
    RetrieveStartScope(closure_scope_);
    DCHECK_NOT_NULL(start_scope_);
  }

  bool RetrieveClosureScope(Scope* scope) {
    // The closure scope is the scope that matches exactly the function we
    // paused in.
    // Note that comparing the position alone is not enough and we also need to
    // match the scope type. E.g. class member initializer have the exact same
    // scope positions as their class scope.
    if (break_scope_type_ == scope->scope_type() &&
        break_scope_start_ == scope->start_position() &&
        break_scope_end_ == scope->end_position()) {
      closure_scope_ = scope->AsDeclarationScope();
      return true;
    }

    for (Scope* inner_scope = scope->inner_scope(); inner_scope != nullptr;
         inner_scope = inner_scope->sibling()) {
      if (RetrieveClosureScope(inner_scope)) return true;
    }
    return false;
  }

  void RetrieveStartScope(Scope* scope) {
    const int start = scope->start_position();
    const int end = scope->end_position();

    // Update start_scope_ if scope contains `position_` and scope is a tighter
    // fit than the currently set start_scope_.
    // Generators have the same source position so we also check for equality.
    if (ContainsPosition(scope) && start >= start_scope_->start_position() &&
        end <= start_scope_->end_position()) {
      start_scope_ = scope;
    }

    for (Scope* inner_scope = scope->inner_scope(); inner_scope != nullptr;
         inner_scope = inner_scope->sibling()) {
      RetrieveStartScope(inner_scope);
    }
  }

  bool ContainsPosition(Scope* scope) {
    const int start = scope->start_position();
    const int end = scope->end_position();
    // In case the closure_scope_ hasn't been found yet, we are less strict
    // about recursing downwards. This might be the case for nested arrow
    // functions that have the same end position.
    const bool position_fits_end =
        closure_scope_ ? position_ < end : position_ <= end;
    // While we're evaluating a class, the calling function will have a class
    // context on the stack with a range that starts at Token::kClass, and the
    // source position will also point to Token::kClass.  To identify the
    // matching scope we include start in the accepted range for class scopes.
    //
    // Similarly "with" scopes can already have bytecodes where the source
    // position points to the closing parenthesis with the "with" context
    // already pushed.
    const bool position_fits_start =
        scope->is_class_scope() || scope->is_with_scope() ? start <= position_
                                                          : start < position_;
    return position_fits_start && position_fits_end;
  }
};

// Walks a ScopeInfo outwards until it finds a EVAL scope.
MaybeHandle<ScopeInfo> FindEvalScope(Isolate* isolate,
                                     Tagged<ScopeInfo> start_scope) {
  Tagged<ScopeInfo> scope = start_scope;
  while (scope->scope_type() != ScopeType::EVAL_SCOPE &&
         scope->HasOuterScopeInfo()) {
    scope = scope->OuterScopeInfo();
  }

  return scope->scope_type() == ScopeType::EVAL_SCOPE
             ? MaybeHandle<ScopeInfo>(scope, isolate)
             : kNullMaybeHandle;
}

}  // namespace

void ScopeIterator::TryParseAndRetrieveScopes(ReparseStrategy strategy) {
  // Catch the case when the debugger stops in an internal function.
  Handle<SharedFunctionInfo> shared_info(function_->shared(), isolate_);
  Handle<ScopeInfo> scope_info(shared_info->scope_info(), isolate_);
  if (IsUndefined(shared_info->script(), isolate_)) {
    current_scope_ = closure_scope_ = nullptr;
    context_ = handle(function_->context(), isolate_);
    function_ = Handle<JSFunction>();
    return;
  }

  bool ignore_nested_scopes = false;
  if (shared_info->HasBreakInfo(isolate_) && frame_inspector_ != nullptr) {
    // The source position at return is always the end of the function,
    // which is not consistent with the current scope chain. Therefore all
    // nested with, catch and block contexts are skipped, and we can only
    // inspect the function scope.
    // This can only happen if we set a break point inside right before the
    // return, which requires a debug info to be available.
    Handle<DebugInfo> debug_info(shared_info->GetDebugInfo(isolate_), isolate_);

    // Find the break point where execution has stopped.
    BreakLocation location = BreakLocation::FromFrame(debug_info, GetFrame());

    ignore_nested_scopes = location.IsReturn();
  }

  if (strategy == ReparseStrategy::kScriptIfNeeded) {
    Tagged<Object> maybe_block_list =
        isolate_->LocalsBlockListCacheGet(scope_info);
    calculate_blocklists_ = IsTheHole(maybe_block_list);
    strategy = calculate_blocklists_ ? ReparseStrategy::kScriptIfNeeded
                                     : ReparseStrategy::kFunctionLiteral;
  }

  // Reparse the code and analyze the scopes.
  // Depending on the choosen strategy, the whole script or just
  // the closure is re-parsed for function scopes.
  DirectHandle<Script> script(Cast<Script>(shared_info->script()), isolate_);

  // Pick between flags for a single function compilation, or an eager
  // compilation of the whole script.
  UnoptimizedCompileFlags flags =
      (scope_info->scope_type() == FUNCTION_SCOPE &&
       strategy == ReparseStrategy::kFunctionLiteral)
          ? UnoptimizedCompileFlags::ForFunctionCompile(isolate_, *shared_info)
          : UnoptimizedCompileFlags::ForScriptCompile(isolate_, *script)
                .set_is_eager(true);
  flags.set_is_reparse(true);

  MaybeHandle<ScopeInfo> maybe_outer_scope;
  if (flags.is_toplevel() &&
      script->compilation_type() == Script::CompilationType::kEval) {
    // Re-parsing a full eval script requires us to correctly set the outer
    // language mode and potentially an outer scope info.
    //
    // We walk the runtime scope chain and look for an EVAL scope. If we don't
    // find one, we assume sloppy mode and no outer scope info.

    DCHECK(flags.is_eval());

    Handle<ScopeInfo> eval_scope;
    if (FindEvalScope(isolate_, *scope_info).ToHandle(&eval_scope)) {
      flags.set_outer_language_mode(eval_scope->language_mode());
      if (eval_scope->HasOuterScopeInfo()) {
        maybe_outer_scope = handle(eval_scope->OuterScopeInfo(), isolate_);
      }
    } else {
      DCHECK_EQ(flags.outer_language_mode(), LanguageMode::kSloppy);
      DCHECK(maybe_outer_scope.is_null());
    }
  } else if (scope_info->scope_type() == EVAL_SCOPE || script->is_wrapped()) {
    flags.set_is_eval(true);
    if (!IsNativeContext(*context_)) {
      maybe_outer_scope = handle(context_->scope_info(), isolate_);
    }
    // Language mode may be inherited from the eval caller.
    // Retrieve it from shared function info.
    flags.set_outer_language_mode(shared_info->language_mode());
  } else if (scope_info->scope_type() == MODULE_SCOPE) {
    DCHECK(script->origin_options().IsModule());
    DCHECK(flags.is_module());
  } else {
    DCHECK(scope_info->is_script_scope() ||
           scope_info->scope_type() == FUNCTION_SCOPE);
  }

  UnoptimizedCompileState compile_state;

  reusable_compile_state_ =
      std::make_unique<ReusableUnoptimizedCompileState>(isolate_);
  info_ = std::make_unique<ParseInfo>(isolate_, flags, &compile_state,
                                      reusable_compile_state_.get());

  const bool parse_result =
      flags.is_toplevel()
          ? parsing::ParseProgram(info_.get(), script, maybe_outer_scope,
                                  isolate_, parsing::ReportStatisticsMode::kNo)
          : parsing::ParseFunction(info_.get(), shared_info, isolate_,
                                   parsing::ReportStatisticsMode::kNo);

  if (parse_result) {
    DeclarationScope* literal_scope = info_->literal()->scope();

    ScopeChainRetriever scope_chain_retriever(literal_scope, function_,
                                              GetSourcePosition());
    start_scope_ = scope_chain_retriever.StartScope();
    current_scope_ = start_scope_;

    // In case of a FUNCTION_SCOPE, the ScopeIterator expects
    // {closure_scope_} to be set to the scope of the function.
    closure_scope_ = scope_info->scope_type() == FUNCTION_SCOPE
                         ? scope_chain_retriever.ClosureScope()
                         : literal_scope;

    if (ignore_nested_scopes) {
      current_scope_ = closure_scope_;
      start_scope_ = current_scope_;
      // ignore_nested_scopes is only used for the return-position breakpoint,
      // so we can safely assume that the closure context for the current
      // function exists if it needs one.
      if (closure_scope_->NeedsContext()) {
        context_ = handle(context_->closure_context(), isolate_);
      }
    }

    MaybeCollectAndStoreLocalBlocklists();
    UnwrapEvaluationContext();
  } else {
    // A failed reparse indicates that the preparser has diverged from the
    // parser, that the preparse data given to the initial parse was faulty, or
    // a stack overflow.
    // TODO(leszeks): This error is pretty unexpected, so we could report the
    // error in debug mode. Better to not fail in release though, in case it's
    // just a stack overflow.

    // Silently fail by presenting an empty context chain.
    context_ = Handle<Context>();
  }
}

void ScopeIterator::UnwrapEvaluationContext() {
  if (!context_->IsDebugEvaluateContext()) return;
  Tagged<Context> current = *context_;
  do {
    Tagged<Object> wrapped = current->get(Context::WRAPPED_CONTEXT_INDEX);
    if (IsContext(wrapped)) {
      current = Cast<Context>(wrapped);
    } else {
      DCHECK(!current->previous().is_null());
      current = current->previous();
    }
  } while (current->IsDebugEvaluateContext());
  context_ = handle(current, isolate_);
}

Handle<JSObject> ScopeIterator::MaterializeScopeDetails() {
  // Calculate the size of the result.
  DirectHandle<FixedArray> details =
      isolate_->factory()->NewFixedArray(kScopeDetailsSize);
  // Fill in scope details.
  details->set(kScopeDetailsTypeIndex, Smi::FromInt(Type()));
  DirectHandle<JSObject> scope_object = ScopeObject(Mode::ALL);
  details->set(kScopeDetailsObjectIndex, *scope_object);
  if (Type() == ScopeTypeGlobal || Type() == ScopeTypeScript) {
    return isolate_->factory()->NewJSArrayWithElements(details);
  } else if (HasContext()) {
    DirectHandle<Object> closure_name = GetFunctionDebugName();
    details->set(kScopeDetailsNameIndex, *closure_name);
    details->set(kScopeDetailsStartPositionIndex,
                 Smi::FromInt(start_position()));
    details->set(kScopeDetailsEndPositionIndex, Smi::FromInt(end_position()));
    if (InInnerScope()) {
      details->set(kScopeDetailsFunctionIndex, *function_);
    }
  }
  return isolate_->factory()->NewJSArrayWithElements(details);
}

bool ScopeIterator::HasPositionInfo() {
  return InInnerScope() || !IsNativeContext(*context_);
}

int ScopeIterator::start_position() {
  if (InInnerScope()) return current_scope_->start_position();
  if (IsNativeContext(*context_)) return 0;
  return context_->closure_context()->scope_info()->StartPosition();
}

int ScopeIterator::end_position() {
  if (InInnerScope()) return current_scope_->end_position();
  if (IsNativeContext(*context_)) return 0;
  return context_->closure_context()->scope_info()->EndPosition();
}

bool ScopeIterator::DeclaresLocals(Mode mode) const {
  ScopeType type = Type();

  if (type == ScopeTypeWith) return mode == Mode::ALL;
  if (type == ScopeTypeGlobal) return mode == Mode::ALL;

  bool declares_local = false;
  auto visitor = [&](DirectHandle<String> name, DirectHandle<Object> value,
                     ScopeType scope_type) {
    declares_local = true;
    return true;
  };
  VisitScope(visitor, mode);
  return declares_local;
}

bool ScopeIterator::HasContext() const {
  return !InInnerScope() || NeedsContext();
}

bool ScopeIterator::NeedsContext() const {
  const bool needs_context = current_scope_->NeedsContext();

  // We try very hard to ensure that a function's context is already
  // available when we pause right at the beginning of that function.
  // This can be tricky when we pause via stack check or via
  // `BreakOnNextFunctionCall`, which happens normally in the middle of frame
  // construction and we have to "step into" the function first.
  //
  // We check this by ensuring that the current context is not the closure
  // context should the function need one. In that case the function has already
  // pushed the context and we are good.
  CHECK_IMPLIES(needs_context && current_scope_ == closure_scope_ &&
                    current_scope_->is_function_scope() && !function_.is_null(),
                function_->context() != *context_);

  return needs_context;
}

bool ScopeIterator::AdvanceOneScope() {
  if (!current_scope_ || !current_scope_->outer_scope()) return false;

  current_scope_ = current_scope_->outer_scope();
  CollectLocalsFromCurrentScope();
  return true;
}

void ScopeIterator::AdvanceOneContext() {
  DCHECK(!IsNativeContext(*context_));
  DCHECK(!context_->previous().is_null());
  context_ = handle(context_->previous(), isolate_);

  // The locals blocklist is always associated with a context. So when we
  // move one context up, we also reset the locals_ blocklist.
  locals_ = StringSet::New(isolate_);
}

void ScopeIterator::AdvanceScope() {
  DCHECK(InInnerScope());

  do {
    if (NeedsContext()) {
      // current_scope_ needs a context so moving one scope up requires us to
      // also move up one context.
      AdvanceOneContext();
    }

    CHECK(AdvanceOneScope());
  } while (current_scope_->is_hidden());
}

void ScopeIterator::AdvanceContext() {
  AdvanceOneContext();

  // While advancing one context, we need to advance at least one
  // scope, but until we hit the next scope that actually requires
  // a context. All the locals collected along the way build the
  // blocklist for debug-evaluate for this context.
  while (AdvanceOneScope() && !NeedsContext()) {
  }
}

void ScopeIterator::Next() {
  DCHECK(!Done());

  ScopeType scope_type = Type();

  if (scope_type == ScopeTypeGlobal) {
    // The global scope is always the last in the chain.
    DCHECK(IsNativeContext(*context_));
    context_ = Handle<Context>();
    DCHECK(Done());
    return;
  }

  bool leaving_closure = current_scope_ == closure_scope_;

  if (scope_type == ScopeTypeScript) {
    DCHECK_IMPLIES(InInnerScope() && !leaving_closure,
                   current_scope_->is_script_scope());
    seen_script_scope_ = true;
    if (context_->IsScriptContext()) {
      context_ = handle(context_->previous(), isolate_);
    }
  } else if (!InInnerScope()) {
    AdvanceContext();
  } else {
    DCHECK_NOT_NULL(current_scope_);
    AdvanceScope();

    if (leaving_closure) {
      DCHECK(current_scope_ != closure_scope_);
      // If the current_scope_ doesn't need a context, we advance the scopes
      // and collect the blocklist along the way until we find the scope
      // that should match `context_`.
      // But only do this if we have complete scope information.
      while (!NeedsContext() && AdvanceOneScope()) {
      }
    }
  }

  MaybeCollectAndStoreLocalBlocklists();
  UnwrapEvaluationContext();

  if (leaving_closure) function_ = Handle<JSFunction>();
}

// Return the type of the current scope.
ScopeIterator::ScopeType ScopeIterator::Type() const {
  DCHECK(!Done());
  if (InInnerScope()) {
    switch (current_scope_->scope_type()) {
      case FUNCTION_SCOPE:
        DCHECK_IMPLIES(NeedsContext(), context_->IsFunctionContext() ||
                                           context_->IsDebugEvaluateContext());
        return ScopeTypeLocal;
      case MODULE_SCOPE:
        DCHECK_IMPLIES(NeedsContext(), context_->IsModuleContext());
        return ScopeTypeModule;
      case SCRIPT_SCOPE:
      case REPL_MODE_SCOPE:
        DCHECK_IMPLIES(NeedsContext(), context_->IsScriptContext() ||
                                           IsNativeContext(*context_));
        return ScopeTypeScript;
      case WITH_SCOPE:
        DCHECK_IMPLIES(NeedsContext(), context_->IsWithContext());
        return ScopeTypeWith;
      case CATCH_SCOPE:
        DCHECK(context_->IsCatchContext());
        return ScopeTypeCatch;
      case BLOCK_SCOPE:
      case CLASS_SCOPE:
        DCHECK_IMPLIES(NeedsContext(), context_->IsBlockContext());
        return ScopeTypeBlock;
      case EVAL_SCOPE:
        DCHECK_IMPLIES(NeedsContext(), context_->IsEvalContext());
        return ScopeTypeEval;
      case SHADOW_REALM_SCOPE:
        DCHECK_IMPLIES(NeedsContext(), IsNativeContext(*context_));
        // TODO(v8:11989): New ScopeType for ShadowRealms?
        return ScopeTypeScript;
    }
    UNREACHABLE();
  }
  if (IsNativeContext(*context_)) {
    DCHECK(IsJSGlobalObject(context_->global_object()));
    // If we are at the native context and have not yet seen script scope,
    // fake it.
    return seen_script_scope_ ? ScopeTypeGlobal : ScopeTypeScript;
  }
  if (context_->IsFunctionContext() || context_->IsEvalContext() ||
      context_->IsDebugEvaluateContext()) {
    return ScopeTypeClosure;
  }
  if (context_->IsCatchContext()) {
    return ScopeTypeCatch;
  }
  if (context_->IsBlockContext()) {
    return ScopeTypeBlock;
  }
  if (context_->IsModuleContext()) {
    return ScopeTypeModule;
  }
  if (context_->IsScriptContext()) {
    return ScopeTypeScript;
  }
  DCHECK(context_->IsWithContext());
  return ScopeTypeWith;
}

Handle<JSObject> ScopeIterator::ScopeObject(Mode mode) {
  DCHECK(!Done());

  ScopeType type = Type();
  if (type == ScopeTypeGlobal) {
    DCHECK_EQ(Mode::ALL, mode);
    return handle(context_->global_proxy(), isolate_);
  }
  if (type == ScopeTypeWith) {
    DCHECK_EQ(Mode::ALL, mode);
    return WithContextExtension();
  }

  Handle<JSObject> scope = isolate_->factory()->NewSlowJSObjectWithNullProto();
  auto visitor = [=, this](Handle<String> name, Handle<Object> value,
                           ScopeType scope_type) {
    if (IsOptimizedOut(*value, isolate_)) {
      JSObject::SetAccessor(
          scope, name, isolate_->factory()->value_unavailable_accessor(), NONE)
          .Check();
    } else if (IsTheHole(*value, isolate_)) {
      const bool is_overriden_repl_let =
          scope_type == ScopeTypeScript &&
          JSReceiver::HasOwnProperty(isolate_, scope, name).FromMaybe(true);
      if (!is_overriden_repl_let) {
        // We also use the hole to represent overridden let-declarations via
        // REPL mode in a script context. Don't install the unavailable accessor
        // in that case.
        JSObject::SetAccessor(scope, name,
                              isolate_->factory()->value_unavailable_accessor(),
                              NONE)
            .Check();
      }
    } else {
      // Overwrite properties. Sometimes names in the same scope can collide,
      // e.g. with extension objects introduced via local eval.
      Object::SetPropertyOrElement(isolate_, scope, name, value,
                                   Just(ShouldThrow::kDontThrow))
          .Check();
    }
    return false;
  };

  VisitScope(visitor, mode);
  return scope;
}

void ScopeIterator::VisitScope(const Visitor& visitor, Mode mode) const {
  switch (Type()) {
    case ScopeTypeLocal:
    case ScopeTypeClosure:
    case ScopeTypeCatch:
    case ScopeTypeBlock:
    case ScopeTypeEval:
      return VisitLocalScope(visitor, mode, Type());
    case ScopeTypeModule:
      if (InInnerScope()) {
        return VisitLocalScope(visitor, mode, Type());
      }
      DCHECK_EQ(Mode::ALL, mode);
      return VisitModuleScope(visitor);
    case ScopeTypeScript:
      DCHECK_EQ(Mode::ALL, mode);
      return VisitScriptScope(visitor);
    case ScopeTypeWith:
    case ScopeTypeGlobal:
      UNREACHABLE();
  }
}

bool ScopeIterator::SetVariableValue(Handle<String> name,
                                     Handle<Object> value) {
  DCHECK(!Done());
  name = isolate_->factory()->InternalizeString(name);
  switch (Type()) {
    case ScopeTypeGlobal:
    case ScopeTypeWith:
      break;

    case ScopeTypeEval:
    case ScopeTypeBlock:
    case ScopeTypeCatch:
    case ScopeTypeModule:
      if (InInnerScope()) return SetLocalVariableValue(name, value);
      if (Type() == ScopeTypeModule && SetModuleVariableValue(name, value)) {
        return true;
      }
      return SetContextVariableValue(name, value);

    case ScopeTypeLocal:
    case ScopeTypeClosure:
      if (InInnerScope()) {
        DCHECK_EQ(ScopeTypeLocal, Type());
        if (SetLocalVariableValue(name, value)) return true;
        // There may not be an associated context since we're InInnerScope().
        if (!NeedsContext()) return false;
      } else {
        DCHECK_EQ(ScopeTypeClosure, Type());
        if (SetContextVariableValue(name, value)) return true;
      }
      // The above functions only set variables statically declared in the
      // function. There may be eval-introduced variables. Check them in
      // SetContextExtensionValue.
      return SetContextExtensionValue(name, value);

    case ScopeTypeScript:
      return SetScriptVariableValue(name, value);
  }
  return false;
}

bool ScopeIterator::ClosureScopeHasThisReference() const {
  // closure_scope_ can be nullptr if parsing failed. See the TODO in
  // TryParseAndRetrieveScopes.
  return closure_scope_ && !closure_scope_->has_this_declaration() &&
         closure_scope_->HasThisReference();
}

void ScopeIterator::CollectLocalsFromCurrentScope() {
  DCHECK(IsStringSet(*locals_));
  for (Variable* var : *current_scope_->locals()) {
    if (var->location() == VariableLocation::PARAMETER ||
        var->location() == VariableLocation::LOCAL) {
      locals_ = StringSet::Add(isolate_, locals_, var->name());
    }
  }
}

#ifdef DEBUG
// Debug print of the content of the current scope.
void ScopeIterator::DebugPrint() {
  StdoutStream os;
  DCHECK(!Done());
  switch (Type()) {
    case ScopeIterator::ScopeTypeGlobal:
      os << "Global:\n";
      Print(*context_, os);
      break;

    case ScopeIterator::ScopeTypeLocal: {
      os << "Local:\n";
      if (NeedsContext()) {
        Print(*context_, os);
        if (context_->has_extension()) {
          DirectHandle<HeapObject> extension(context_->extension(), isolate_);
          DCHECK(IsJSContextExtensionObject(*extension));
          Print(*extension, os);
        }
      }
      break;
    }

    case ScopeIterator::ScopeTypeWith:
      os << "With:\n";
      Print(context_->extension(), os);
      break;

    case ScopeIterator::ScopeTypeCatch:
      os << "Catch:\n";
      Print(context_->extension(), os);
      Print(context_->get(Context::THROWN_OBJECT_INDEX), os);
      break;

    case ScopeIterator::ScopeTypeClosure:
      os << "Closure:\n";
      Print(*context_, os);
      if (context_->has_extension()) {
        DirectHandle<HeapObject> extension(context_->extension(), isolate_);
        DCHECK(IsJSContextExtensionObject(*extension));
        Print(*extension, os);
      }
      break;

    case ScopeIterator::ScopeTypeScript:
      os << "Script:\n";
      Print(context_->native_context()->script_context_table(), os);
      break;

    default:
      UNREACHABLE();
  }
  PrintF("\n");
}
#endif

int ScopeIterator::GetSourcePosition() const {
  if (frame_inspector_) {
    return frame_inspector_->GetSourcePosition();
  } else {
    DCHECK(!generator_.is_null());
    SharedFunctionInfo::EnsureSourcePositionsAvailable(
        isolate_, handle(generator_->function()->shared(), isolate_));
    return generator_->source_position();
  }
}

void ScopeIterator::VisitScriptScope(const Visitor& visitor) const {
  DirectHandle<ScriptContextTable> script_contexts(
      context_->native_context()->script_context_table(), isolate_);

  // Skip the first script since that just declares 'this'.
  for (int i = 1; i < script_contexts->length(kAcquireLoad); i++) {
    DirectHandle<Context> context(script_contexts->get(i), isolate_);
    Handle<ScopeInfo> scope_info(context->scope_info(), isolate_);
    if (VisitContextLocals(visitor, scope_info, context, ScopeTypeScript)) {
      return;
    }
  }
}

void ScopeIterator::VisitModuleScope(const Visitor& visitor) const {
  DCHECK(context_->IsModuleContext());

  Handle<ScopeInfo> scope_info(context_->scope_info(), isolate_);
  if (VisitContextLocals(visitor, scope_info, context_, ScopeTypeModule)) {
    return;
  }

  int module_variable_count = scope_info->ModuleVariableCount();

  DirectHandle<SourceTextModule> module(context_->module(), isolate_);

  for (int i = 0; i < module_variable_count; ++i) {
    int index;
    Handle<String> name;
    {
      Tagged<String> raw_name;
      scope_info->ModuleVariable(i, &raw_name, &index);
      if (ScopeInfo::VariableIsSynthetic(raw_name)) continue;
      name = handle(raw_name, isolate_);
    }
    Handle<Object> value =
        SourceTextModule::LoadVariable(isolate_, module, index);

    if (visitor(name, value, ScopeTypeModule)) return;
  }
}

bool ScopeIterator::VisitContextLocals(const Visitor& visitor,
                                       Handle<ScopeInfo> scope_info,
                                       DirectHandle<Context> context,
                                       ScopeType scope_type) const {
  // Fill all context locals to the context extension.
  for (auto it : ScopeInfo::IterateLocalNames(scope_info)) {
    Handle<String> name(it->name(), isolate_);
    if (ScopeInfo::VariableIsSynthetic(*name)) continue;
    int context_index = scope_info->ContextHeaderLength() + it->index();
    Handle<Object> value(context->get(context_index), isolate_);
    if (visitor(name, value, scope_type)) return true;
  }
  return false;
}

bool ScopeIterator::VisitLocals(const Visitor& visitor, Mode mode,
                                ScopeType scope_type) const {
  if (mode == Mode::STACK && current_scope_->is_declaration_scope() &&
      current_scope_->AsDeclarationScope()->has_this_declaration()) {
    // TODO(bmeurer): We should refactor the general variable lookup
    // around "this", since the current way is rather hacky when the
    // receiver is context-allocated.
    auto this_var = current_scope_->AsDeclarationScope()->receiver();
    Handle<Object> receiver =
        this_var->location() == VariableLocation::CONTEXT
            ? handle(context_->get(this_var->index()), isolate_)
        : frame_inspector_ == nullptr ? handle(generator_->receiver(), isolate_)
                                      : frame_inspector_->GetReceiver();
    if (visitor(isolate_->factory()->this_string(), receiver, scope_type))
      return true;
  }

  if (current_scope_->is_function_scope()) {
    Variable* function_var =
        current_scope_->AsDeclarationScope()->function_var();
    if (function_var != nullptr) {
      Handle<JSFunction> function = frame_inspector_ == nullptr
                                        ? function_
                                        : frame_inspector_->GetFunction();
      Handle<String> name = function_var->name();
      if (visitor(name, function, scope_type)) return true;
    }
  }

  for (Variable* var : *current_scope_->locals()) {
    if (ScopeInfo::VariableIsSynthetic(*var->name())) {
      // We want to materialize "new.target" for debug-evaluate.
      if (mode != Mode::STACK ||
          !var->name()->Equals(*isolate_->factory()->dot_new_target_string())) {
        continue;
      }
    }

    int index = var->index();
    Handle<Object> value;
    switch (var->location()) {
      case VariableLocation::LOOKUP:
        UNREACHABLE();

      case VariableLocation::REPL_GLOBAL:
        // REPL declared variables are ignored for now.
      case VariableLocation::UNALLOCATED:
        continue;

      case VariableLocation::PARAMETER: {
        if (frame_inspector_ == nullptr) {
          // Get the variable from the suspended generator.
          DCHECK(!generator_.is_null());
          Tagged<FixedArray> parameters_and_registers =
              generator_->parameters_and_registers();
          DCHECK_LT(index, parameters_and_registers->length());
          value = handle(parameters_and_registers->get(index), isolate_);
        } else if (var->IsReceiver()) {
          value = frame_inspector_->GetReceiver();
        } else {
          value = frame_inspector_->GetParameter(index);
        }
        break;
      }

      case VariableLocation::LOCAL:
        if (frame_inspector_ == nullptr) {
          // Get the variable from the suspended generator.
          DCHECK(!generator_.is_null());
          Tagged<FixedArray> parameters_and_registers =
              generator_->parameters_and_registers();
          int parameter_count =
              function_->shared()->scope_info()->ParameterCount();
          index += parameter_count;
          DCHECK_LT(index, parameters_and_registers->length());
          value = handle(parameters_and_registers->get(index), isolate_);
        } else {
          value = frame_inspector_->GetExpression(index);
          if (IsOptimizedOut(*value, isolate_)) {
            // We'll rematerialize this later.
            if (current_scope_->is_declaration_scope() &&
                current_scope_->AsDeclarationScope()->arguments() == var) {
              continue;
            }
          } else if (IsLexicalVariableMode(var->mode()) &&
                     IsUndefined(*value, isolate_) &&
                     GetSourcePosition() != kNoSourcePosition &&
                     GetSourcePosition() <= var->initializer_position()) {
            // Variables that are `undefined` could also mean an elided hole
            // write. We explicitly check the static scope information if we
            // are currently stopped before the variable is actually initialized
            // which means we are in the middle of that var's TDZ.
            value = isolate_->factory()->the_hole_value();
          }
        }
        break;

      case VariableLocation::CONTEXT:
        if (mode == Mode::STACK) continue;
        DCHECK(var->IsContextSlot());

        // We know of at least one open bug where the context and scope chain
        // don't match (https://crbug.com/753338).
        // Return `undefined` if the context's ScopeInfo doesn't know anything
        // about this variable.
        if (context_->scope_info()->ContextSlotIndex(var->name()) != index) {
          value = isolate_->factory()->undefined_value();
        } else {
          value = handle(context_->get(index), isolate_);
        }
        break;

      case VariableLocation::MODULE: {
        if (mode == Mode::STACK) continue;
        // if (var->IsExport()) continue;
        DirectHandle<SourceTextModule> module(context_->module(), isolate_);
        value = SourceTextModule::LoadVariable(isolate_, module, var->index());
        break;
      }
    }

    if (visitor(var->name(), value, scope_type)) return true;
  }
  return false;
}

// Retrieve the with-context extension object. If the extension object is
// a proxy, return an empty object.
Handle<JSObject> ScopeIterator::WithContextExtension() {
  DCHECK(context_->IsWithContext());
  if (!IsJSObject(context_->extension_receiver())) {
    DCHECK(IsJSProxy(context_->extension_receiver()) ||
           IsWasmObject(context_->extension_receiver()));
    return isolate_->factory()->NewSlowJSObjectWithNullProto();
  }
  return handle(Cast<JSObject>(context_->extension_receiver()), isolate_);
}

// Create a plain JSObject which materializes the block scope for the specified
// block context.
void ScopeIterator::VisitLocalScope(const Visitor& visitor, Mode mode,
                                    ScopeType scope_type) const {
  if (InInnerScope()) {
    if (VisitLocals(visitor, mode, scope_type)) return;
    if (mode == Mode::STACK && Type() == ScopeTypeLocal) {
      // Hide |this| in arrow functions that may be embedded in other functions
      // but don't force |this| to be context-allocated. Otherwise we'd find the
      // wrong |this| value.
      if (!closure_scope_->has_this_declaration() &&
          !closure_scope_->HasThisReference()) {
        if (visitor(isolate_->factory()->this_string(),
                    isolate_->factory()->undefined_value(), scope_type))
          return;
      }
      // Add |arguments| to the function scope even if it wasn't used.
      // Currently we don't yet support materializing the arguments object of
      // suspended generators. We'd need to read the arguments out from the
      // suspended generator rather than from an activation as
      // FunctionGetArguments does.
      if (frame_inspector_ != nullptr && !closure_scope_->is_arrow_scope() &&
          (closure_scope_->arguments() == nullptr ||
           IsOptimizedOut(*frame_inspector_->GetExpression(
                              closure_scope_->arguments()->index()),
                          isolate_))) {
        JavaScriptFrame* frame = GetFrame();
        Handle<JSObject> arguments = Accessors::FunctionGetArguments(
            frame, frame_inspector_->inlined_frame_index());
        if (visitor(isolate_->factory()->arguments_string(), arguments,
                    scope_type))
          return;
      }
    }
  } else {
    DCHECK_EQ(Mode::ALL, mode);
    Handle<ScopeInfo> scope_info(context_->scope_info(), isolate_);
    if (VisitContextLocals(visitor, scope_info, context_, scope_type)) return;
  }

  if (mode == Mode::ALL && HasContext()) {
    DCHECK(!context_->IsScriptContext());
    DCHECK(!IsNativeContext(*context_));
    DCHECK(!context_->IsWithContext());
    if (!context_->scope_info()->SloppyEvalCanExtendVars()) return;
    if (context_->extension_object().is_null()) return;
    Handle<JSObject> extension(context_->extension_object(), isolate_);
    DirectHandle<FixedArray> keys =
        KeyAccumulator::GetKeys(isolate_, extension,
                                KeyCollectionMode::kOwnOnly, ENUMERABLE_STRINGS)
            .ToHandleChecked();

    for (int i = 0; i < keys->length(); i++) {
      // Names of variables introduced by eval are strings.
      DCHECK(IsString(keys->get(i)));
      Handle<String> key(Cast<String>(keys->get(i)), isolate_);
      Handle<Object> value =
          JSReceiver::GetDataProperty(isolate_, extension, key);
      if (visitor(key, value, scope_type)) return;
    }
  }
}

bool ScopeIterator::SetLocalVariableValue(Handle<String> variable_name,
                                          DirectHandle<Object> new_value) {
  // TODO(verwaest): Walk parameters backwards, not forwards.
  // TODO(verwaest): Use VariableMap rather than locals() list for lookup.
  for (Variable* var : *current_scope_->locals()) {
    if (String::Equals(isolate_, var->name(), variable_name)) {
      int index = var->index();
      switch (var->location()) {
        case VariableLocation::LOOKUP:
        case VariableLocation::UNALLOCATED:
          // Drop assignments to unallocated locals.
          DCHECK(var->is_this() ||
                 *variable_name == ReadOnlyRoots(isolate_).arguments_string());
          return false;

        case VariableLocation::REPL_GLOBAL:
          // Assignments to REPL declared variables are ignored for now.
          return false;

        case VariableLocation::PARAMETER: {
          if (var->is_this()) return false;
          if (frame_inspector_ == nullptr) {
            // Set the variable in the suspended generator.
            DCHECK(!generator_.is_null());
            DirectHandle<FixedArray> parameters_and_registers(
                generator_->parameters_and_registers(), isolate_);
            DCHECK_LT(index, parameters_and_registers->length());
            parameters_and_registers->set(index, *new_value);
          } else {
            JavaScriptFrame* frame = GetFrame();
            if (!frame->is_unoptimized()) return false;

            frame->SetParameterValue(index, *new_value);
          }
          return true;
        }

        case VariableLocation::LOCAL:
          if (frame_inspector_ == nullptr) {
            // Set the variable in the suspended generator.
            DCHECK(!generator_.is_null());
            int parameter_count =
                function_->shared()->scope_info()->ParameterCount();
            index += parameter_count;
            DirectHandle<FixedArray> parameters_and_registers(
                generator_->parameters_and_registers(), isolate_);
            DCHECK_LT(index, parameters_and_registers->length());
            parameters_and_registers->set(index, *new_value);
          } else {
            // Set the variable on the stack.
            JavaScriptFrame* frame = GetFrame();
            if (!frame->is_unoptimized()) return false;

            frame->SetExpression(index, *new_value);
          }
          return true;

        case VariableLocation::CONTEXT:
          DCHECK(var->IsContextSlot());

          // We know of at least one open bug where the context and scope chain
          // don't match (https://crbug.com/753338).
          // Skip the write if the context's ScopeInfo doesn't know anything
          // about this variable.
          if (context_->scope_info()->ContextSlotIndex(variable_name) !=
              index) {
            return false;
          }
          context_->set(index, *new_value);
          return true;

        case VariableLocation::MODULE:
          if (!var->IsExport()) return false;
          DirectHandle<SourceTextModule> module(context_->module(), isolate_);
          SourceTextModule::StoreVariable(module, var->index(), new_value);
          return true;
      }
      UNREACHABLE();
    }
  }

  return false;
}

bool ScopeIterator::SetContextExtensionValue(Handle<String> variable_name,
                                             Handle<Object> new_value) {
  if (!context_->has_extension()) return false;

  DCHECK(IsJSContextExtensionObject(context_->extension_object()));
  Handle<JSObject> ext(context_->extension_object(), isolate_);
  LookupIterator it(isolate_, ext, variable_name, LookupIterator::OWN);
  Maybe<bool> maybe = JSReceiver::HasProperty(&it);
  DCHECK(maybe.IsJust());
  if (!maybe.FromJust()) return false;

  CHECK(Object::SetDataProperty(&it, new_value).ToChecked());
  return true;
}

bool ScopeIterator::SetContextVariableValue(Handle<String> variable_name,
                                            DirectHandle<Object> new_value) {
  int slot_index = context_->scope_info()->ContextSlotIndex(variable_name);
  if (slot_index < 0) return false;
  context_->set(slot_index, *new_value);
  return true;
}

bool ScopeIterator::SetModuleVariableValue(DirectHandle<String> variable_name,
                                           DirectHandle<Object> new_value) {
  DisallowGarbageCollection no_gc;
  int cell_index;
  VariableMode mode;
  InitializationFlag init_flag;
  MaybeAssignedFlag maybe_assigned_flag;
  cell_index = context_->scope_info()->ModuleIndex(
      *variable_name, &mode, &init_flag, &maybe_assigned_flag);

  // Setting imports is currently not supported.
  if (SourceTextModuleDescriptor::GetCellIndexKind(cell_index) !=
      SourceTextModuleDescriptor::kExport) {
    return false;
  }

  DirectHandle<SourceTextModule> module(context_->module(), isolate_);
  SourceTextModule::StoreVariable(module, cell_index, new_value);
  return true;
}

bool ScopeIterator::SetScriptVariableValue(Handle<String> variable_name,
                                           DirectHandle<Object> new_value) {
  DirectHandle<ScriptContextTable> script_contexts(
      context_->native_context()->script_context_table(), isolate_);
  VariableLookupResult lookup_result;
  if (script_contexts->Lookup(variable_name, &lookup_result)) {
    DirectHandle<Context> script_context(
        script_contexts->get(lookup_result.context_index), isolate_);
    script_context->set(lookup_result.slot_index, *new_value);
    return true;
  }

  return false;
}

namespace {

// Given the scope and context of a paused function, this class calculates
// all the necessary block lists on the scope chain and stores them in the
// global LocalsBlockListCache ephemeron table.
//
// Doc: bit.ly/chrome-devtools-debug-evaluate-design.
//
// The algorithm works in a single walk of the scope chain from the
// paused function scope outwards to the script scope.
//
// When we step from scope "a" to its outer scope "b", we do:
//
//   1. Add all stack-allocated variables from "b" to the blocklists.
//   2. Does "b" need a context? If yes:
//        - Store all current blocklists in the global table
//        - Start a new blocklist for scope "b"
//   3. Is "b" a function scope without a context? If yes:
//        - Start a new blocklist for scope "b"
//
class LocalBlocklistsCollector {
 public:
  LocalBlocklistsCollector(Isolate* isolate, Handle<Script> script,
                           Handle<Context> context,
                           DeclarationScope* closure_scope);
  void CollectAndStore();

 private:
  void InitializeWithClosureScope();
  void AdvanceToNextNonHiddenScope();
  void CollectCurrentLocalsIntoBlocklists();
  Handle<ScopeInfo> FindScopeInfoForScope(Scope* scope) const;
  void StoreFunctionBlocklists(Handle<ScopeInfo> outer_scope_info);

  Isolate* isolate_;
  Handle<Script> script_;
  Handle<Context> context_;
  Scope* scope_;
  DeclarationScope* closure_scope_;

  Handle<StringSet> context_blocklist_;
  std::map<Scope*, Handle<StringSet>> function_blocklists_;
};

LocalBlocklistsCollector::LocalBlocklistsCollector(
    Isolate* isolate, Handle<Script> script, Handle<Context> context,
    DeclarationScope* closure_scope)
    : isolate_(isolate),
      script_(script),
      context_(context),
      scope_(closure_scope),
      closure_scope_(closure_scope) {}

void LocalBlocklistsCollector::InitializeWithClosureScope() {
  CHECK(scope_->is_declaration_scope());
  function_blocklists_.emplace(scope_, StringSet::New(isolate_));
  if (scope_->NeedsContext()) context_blocklist_ = StringSet::New(isolate_);
}

void LocalBlocklistsCollector::AdvanceToNextNonHiddenScope() {
  DCHECK(scope_ && scope_->outer_scope());
  do {
    scope_ = scope_->outer_scope();
    CHECK(scope_);
  } while (scope_->is_hidden());
}

void LocalBlocklistsCollector::CollectCurrentLocalsIntoBlocklists() {
  for (Variable* var : *scope_->locals()) {
    if (var->location() == VariableLocation::PARAMETER ||
        var->location() == VariableLocation::LOCAL) {
      if (!context_blocklist_.is_null()) {
        context_blocklist_ =
            StringSet::Add(isolate_, context_blocklist_, var->name());
      }
      for (auto& pair : function_blocklists_) {
        pair.second = StringSet::Add(isolate_, pair.second, var->name());
      }
    }
  }
}

Handle<ScopeInfo> LocalBlocklistsCollector::FindScopeInfoForScope(
    Scope* scope) const {
  DisallowGarbageCollection no_gc;
  SharedFunctionInfo::ScriptIterator iterator(isolate_, *script_);
  for (Tagged<SharedFunctionInfo> info = iterator.Next(); !info.is_null();
       info = iterator.Next()) {
    Tagged<ScopeInfo> scope_info = info->scope_info();
    if (info->is_compiled() && !scope_info.is_null() &&
        scope->start_position() == info->StartPosition() &&
        scope->end_position() == info->EndPosition() &&
        scope->scope_type() == scope_info->scope_type()) {
      return handle(scope_info, isolate_);
    }
  }
  return Handle<ScopeInfo>();
}

void LocalBlocklistsCollector::StoreFunctionBlocklists(
    Handle<ScopeInfo> outer_scope_info) {
  for (const auto& pair : function_blocklists_) {
    Handle<ScopeInfo> scope_info = FindScopeInfoForScope(pair.first);
    // If we don't find a ScopeInfo it's not tragic. It means we'll do
    // a full-reparse in case we pause in that function in the future.
    // The only ScopeInfo that MUST be found is for the closure_scope_.
    CHECK_IMPLIES(pair.first == closure_scope_, !scope_info.is_null());
    if (scope_info.is_null()) continue;
    isolate_->LocalsBlockListCacheSet(scope_info, outer_scope_info,
                                      pair.second);
  }
}

void LocalBlocklistsCollector::CollectAndStore() {
  InitializeWithClosureScope();

  while (scope_->outer_scope() && !IsNativeContext(*context_)) {
    AdvanceToNextNonHiddenScope();
    // 1. Add all stack-allocated variables of `scope_` to the various lists.
    CollectCurrentLocalsIntoBlocklists();

    // 2. If the current scope requires a context then all the blocklists "stop"
    //    here and we store them.  Next, advance the current context so
    //    `context_` and `scope_` match again.
    if (scope_->NeedsContext()) {
      if (!context_blocklist_.is_null()) {
        // Only store the block list and advance the context if the
        // context_blocklist is set. This handles the case when we start on
        // a closure scope that doesn't require a context. In that case
        // `context_` is already the right context for `scope_` so we don't
        // need to advance `context_`.
        isolate_->LocalsBlockListCacheSet(
            handle(context_->scope_info(), isolate_),
            handle(context_->previous()->scope_info(), isolate_),
            context_blocklist_);
        context_ = handle(context_->previous(), isolate_);
      }

      StoreFunctionBlocklists(handle(context_->scope_info(), isolate_));

      context_blocklist_ = StringSet::New(isolate_);
      function_blocklists_.clear();
    } else if (scope_->is_function_scope()) {
      // 3. If `scope` is a function scope with an SFI, start recording
      //    locals for its ScopeInfo.
      CHECK(!scope_->NeedsContext());
      function_blocklists_.emplace(scope_, StringSet::New(isolate_));
    }
  }

  // In case we don't have any outer scopes we still need to record the empty
  // block list for the paused function to prevent future re-parses.
  StoreFunctionBlocklists(handle(context_->scope_info(), isolate_));
}

}  // namespace

void ScopeIterator::MaybeCollectAndStoreLocalBlocklists() const {
  if (!calculate_blocklists_ || current_scope_ != closure_scope_ ||
      Type() == ScopeTypeScript) {
    return;
  }

  DCHECK(IsTheHole(isolate_->LocalsBlockListCacheGet(
      handle(function_->shared()->scope_info(), isolate_))));
  LocalBlocklistsCollector collector(isolate_, script_, context_,
                                     closure_scope_);
  collector.CollectAndStore();
}

}  // namespace internal
}  // namespace v8
