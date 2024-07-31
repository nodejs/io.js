// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/compilation-cache.h"

#include "src/common/globals.h"
#include "src/heap/factory.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/compilation-cache-table-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/slots.h"
#include "src/objects/visitors.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

// Initial size of each compilation cache table allocated.
static const int kInitialCacheSize = 64;

CompilationCache::CompilationCache(Isolate* isolate)
    : isolate_(isolate),
      script_(isolate),
      eval_global_(isolate),
      eval_contextual_(isolate),
      reg_exp_(isolate),
      enabled_script_and_eval_(true) {}

Handle<CompilationCacheTable> CompilationCacheEvalOrScript::GetTable() {
  if (IsUndefined(table_, isolate())) {
    return CompilationCacheTable::New(isolate(), kInitialCacheSize);
  }
  return handle(CompilationCacheTable::cast(table_), isolate());
}

Handle<CompilationCacheTable> CompilationCacheRegExp::GetTable(int generation) {
  DCHECK_LT(generation, kGenerations);
  Handle<CompilationCacheTable> result;
  if (IsUndefined(tables_[generation], isolate())) {
    result = CompilationCacheTable::New(isolate(), kInitialCacheSize);
    tables_[generation] = *result;
  } else {
    Tagged<CompilationCacheTable> table =
        CompilationCacheTable::cast(tables_[generation]);
    result = Handle<CompilationCacheTable>(table, isolate());
  }
  return result;
}

void CompilationCacheRegExp::Age() {
  static_assert(kGenerations > 1);

  // Age the generations implicitly killing off the oldest.
  for (int i = kGenerations - 1; i > 0; i--) {
    tables_[i] = tables_[i - 1];
  }

  // Set the first generation as unborn.
  tables_[0] = ReadOnlyRoots(isolate()).undefined_value();
}

void CompilationCacheScript::Age() {
  DisallowGarbageCollection no_gc;
  if (IsUndefined(table_, isolate())) return;
  Tagged<CompilationCacheTable> table = CompilationCacheTable::cast(table_);

  for (InternalIndex entry : table->IterateEntries()) {
    Tagged<Object> key;
    if (!table->ToKey(isolate(), entry, &key)) continue;
    DCHECK(IsWeakFixedArray(key));

    Tagged<Object> value = table->PrimaryValueAt(entry);
    if (!IsUndefined(value, isolate())) {
      Tagged<SharedFunctionInfo> info = SharedFunctionInfo::cast(value);
      // Clear entries after Bytecode was flushed from SFI.
      if (!info->HasBytecodeArray()) {
        table->SetPrimaryValueAt(entry,
                                 ReadOnlyRoots(isolate()).undefined_value(),
                                 SKIP_WRITE_BARRIER);
      }
    }
  }
}

void CompilationCacheEval::Age() {
  DisallowGarbageCollection no_gc;
  if (IsUndefined(table_, isolate())) return;
  Tagged<CompilationCacheTable> table = CompilationCacheTable::cast(table_);

  for (InternalIndex entry : table->IterateEntries()) {
    Tagged<Object> key;
    if (!table->ToKey(isolate(), entry, &key)) continue;

    if (IsNumber(key, isolate())) {
      // The ageing mechanism for the initial dummy entry in the eval cache.
      // The 'key' is the hash represented as a Number. The 'value' is a smi
      // counting down from kHashGenerations. On reaching zero, the entry is
      // cleared.
      // Note: The following static assert only establishes an explicit
      // connection between initialization- and use-sites of the smi value
      // field.
      static_assert(CompilationCacheTable::kHashGenerations);
      const int new_count = Smi::ToInt(table->PrimaryValueAt(entry)) - 1;
      if (new_count == 0) {
        table->RemoveEntry(entry);
      } else {
        DCHECK_GT(new_count, 0);
        table->SetPrimaryValueAt(entry, Smi::FromInt(new_count),
                                 SKIP_WRITE_BARRIER);
      }
    } else {
      DCHECK(IsFixedArray(key));
      // The ageing mechanism for eval caches.
      Tagged<SharedFunctionInfo> info =
          SharedFunctionInfo::cast(table->PrimaryValueAt(entry));
      // Clear entries after Bytecode was flushed from SFI.
      if (!info->HasBytecodeArray()) {
        table->RemoveEntry(entry);
      }
    }
  }
}

void CompilationCacheEvalOrScript::Iterate(RootVisitor* v) {
  v->VisitRootPointer(Root::kCompilationCache, nullptr,
                      FullObjectSlot(&table_));
}

void CompilationCacheRegExp::Iterate(RootVisitor* v) {
  v->VisitRootPointers(Root::kCompilationCache, nullptr,
                       FullObjectSlot(&tables_[0]),
                       FullObjectSlot(&tables_[kGenerations]));
}

void CompilationCacheEvalOrScript::Clear() {
  table_ = ReadOnlyRoots(isolate()).undefined_value();
}

void CompilationCacheRegExp::Clear() {
  MemsetPointer(reinterpret_cast<Address*>(tables_),
                ReadOnlyRoots(isolate()).undefined_value().ptr(), kGenerations);
}

void CompilationCacheEvalOrScript::Remove(
    Handle<SharedFunctionInfo> function_info) {
  if (IsUndefined(table_, isolate())) return;
  CompilationCacheTable::cast(table_)->Remove(*function_info);
}

CompilationCacheScript::LookupResult CompilationCacheScript::Lookup(
    Handle<String> source, const ScriptDetails& script_details) {
  LookupResult result;
  LookupResult::RawObjects raw_result_for_escaping_handle_scope;

  // Probe the script table. Make sure not to leak handles
  // into the caller's handle scope.
  {
    HandleScope scope(isolate());
    Handle<CompilationCacheTable> table = GetTable();
    LookupResult probe = CompilationCacheTable::LookupScript(
        table, source, script_details, isolate());
    raw_result_for_escaping_handle_scope = probe.GetRawObjects();
  }
  result = LookupResult::FromRawObjects(raw_result_for_escaping_handle_scope,
                                        isolate());

  // Once outside the manacles of the handle scope, we need to recheck
  // to see if we actually found a cached script. If so, we return a
  // handle created in the caller's handle scope.
  Handle<Script> script;
  if (result.script().ToHandle(&script)) {
    Handle<SharedFunctionInfo> sfi;
    if (result.toplevel_sfi().ToHandle(&sfi)) {
      isolate()->counters()->compilation_cache_hits()->Increment();
      LOG(isolate(), CompilationCacheEvent("hit", "script", *sfi));
    } else {
      isolate()->counters()->compilation_cache_partial_hits()->Increment();
    }
  } else {
    isolate()->counters()->compilation_cache_misses()->Increment();
  }
  return result;
}

void CompilationCacheScript::Put(Handle<String> source,
                                 Handle<SharedFunctionInfo> function_info) {
  HandleScope scope(isolate());
  Handle<CompilationCacheTable> table = GetTable();
  table_ = *CompilationCacheTable::PutScript(table, source, kNullMaybeHandle,
                                             function_info, isolate());
}

InfoCellPair CompilationCacheEval::Lookup(Handle<String> source,
                                          Handle<SharedFunctionInfo> outer_info,
                                          Handle<Context> native_context,
                                          LanguageMode language_mode,
                                          int position) {
  HandleScope scope(isolate());
  // Make sure not to leak the table into the surrounding handle
  // scope. Otherwise, we risk keeping old tables around even after
  // having cleared the cache.
  InfoCellPair result;
  Handle<CompilationCacheTable> table = GetTable();
  result = CompilationCacheTable::LookupEval(
      table, source, outer_info, native_context, language_mode, position);
  if (result.has_shared()) {
    isolate()->counters()->compilation_cache_hits()->Increment();
  } else {
    isolate()->counters()->compilation_cache_misses()->Increment();
  }
  return result;
}

void CompilationCacheEval::Put(Handle<String> source,
                               Handle<SharedFunctionInfo> outer_info,
                               Handle<SharedFunctionInfo> function_info,
                               Handle<Context> native_context,
                               Handle<FeedbackCell> feedback_cell,
                               int position) {
  HandleScope scope(isolate());
  Handle<CompilationCacheTable> table = GetTable();
  table_ =
      *CompilationCacheTable::PutEval(table, source, outer_info, function_info,
                                      native_context, feedback_cell, position);
}

MaybeHandle<FixedArray> CompilationCacheRegExp::Lookup(Handle<String> source,
                                                       JSRegExp::Flags flags) {
  HandleScope scope(isolate());
  // Make sure not to leak the table into the surrounding handle
  // scope. Otherwise, we risk keeping old tables around even after
  // having cleared the cache.
  Handle<Object> result = isolate()->factory()->undefined_value();
  int generation;
  for (generation = 0; generation < kGenerations; generation++) {
    Handle<CompilationCacheTable> table = GetTable(generation);
    result = table->LookupRegExp(source, flags);
    if (IsFixedArray(*result)) break;
  }
  if (IsFixedArray(*result)) {
    Handle<FixedArray> data = Handle<FixedArray>::cast(result);
    if (generation != 0) {
      Put(source, flags, data);
    }
    isolate()->counters()->compilation_cache_hits()->Increment();
    return scope.CloseAndEscape(data);
  } else {
    isolate()->counters()->compilation_cache_misses()->Increment();
    return MaybeHandle<FixedArray>();
  }
}

void CompilationCacheRegExp::Put(Handle<String> source, JSRegExp::Flags flags,
                                 Handle<FixedArray> data) {
  HandleScope scope(isolate());
  Handle<CompilationCacheTable> table = GetTable(0);
  tables_[0] =
      *CompilationCacheTable::PutRegExp(isolate(), table, source, flags, data);
}

void CompilationCache::Remove(Handle<SharedFunctionInfo> function_info) {
  if (!IsEnabledScriptAndEval()) return;

  eval_global_.Remove(function_info);
  eval_contextual_.Remove(function_info);
  script_.Remove(function_info);
}

CompilationCacheScript::LookupResult CompilationCache::LookupScript(
    Handle<String> source, const ScriptDetails& script_details,
    LanguageMode language_mode) {
  if (!IsEnabledScript(language_mode)) return {};
  return script_.Lookup(source, script_details);
}

InfoCellPair CompilationCache::LookupEval(Handle<String> source,
                                          Handle<SharedFunctionInfo> outer_info,
                                          Handle<Context> context,
                                          LanguageMode language_mode,
                                          int position) {
  InfoCellPair result;
  if (!IsEnabledScriptAndEval()) return result;

  const char* cache_type;

  if (IsNativeContext(*context)) {
    result = eval_global_.Lookup(source, outer_info, context, language_mode,
                                 position);
    cache_type = "eval-global";

  } else {
    DCHECK_NE(position, kNoSourcePosition);
    Handle<Context> native_context(context->native_context(), isolate());
    result = eval_contextual_.Lookup(source, outer_info, native_context,
                                     language_mode, position);
    cache_type = "eval-contextual";
  }

  if (result.has_shared()) {
    LOG(isolate(), CompilationCacheEvent("hit", cache_type, result.shared()));
  }

  return result;
}

MaybeHandle<FixedArray> CompilationCache::LookupRegExp(Handle<String> source,
                                                       JSRegExp::Flags flags) {
  return reg_exp_.Lookup(source, flags);
}

void CompilationCache::PutScript(Handle<String> source,
                                 LanguageMode language_mode,
                                 Handle<SharedFunctionInfo> function_info) {
  if (!IsEnabledScript(language_mode)) return;
  LOG(isolate(), CompilationCacheEvent("put", "script", *function_info));

  script_.Put(source, function_info);
}

void CompilationCache::PutEval(Handle<String> source,
                               Handle<SharedFunctionInfo> outer_info,
                               Handle<Context> context,
                               Handle<SharedFunctionInfo> function_info,
                               Handle<FeedbackCell> feedback_cell,
                               int position) {
  if (!IsEnabledScriptAndEval()) return;

  const char* cache_type;
  HandleScope scope(isolate());
  if (IsNativeContext(*context)) {
    eval_global_.Put(source, outer_info, function_info, context, feedback_cell,
                     position);
    cache_type = "eval-global";
  } else {
    DCHECK_NE(position, kNoSourcePosition);
    Handle<Context> native_context(context->native_context(), isolate());
    eval_contextual_.Put(source, outer_info, function_info, native_context,
                         feedback_cell, position);
    cache_type = "eval-contextual";
  }
  LOG(isolate(), CompilationCacheEvent("put", cache_type, *function_info));
}

void CompilationCache::PutRegExp(Handle<String> source, JSRegExp::Flags flags,
                                 Handle<FixedArray> data) {
  reg_exp_.Put(source, flags, data);
}

void CompilationCache::Clear() {
  script_.Clear();
  eval_global_.Clear();
  eval_contextual_.Clear();
  reg_exp_.Clear();
}

void CompilationCache::Iterate(RootVisitor* v) {
  script_.Iterate(v);
  eval_global_.Iterate(v);
  eval_contextual_.Iterate(v);
  reg_exp_.Iterate(v);
}

void CompilationCache::MarkCompactPrologue() {
  // Drop SFI entries with flushed bytecode.
  script_.Age();
  eval_global_.Age();
  eval_contextual_.Age();

  // Drop entries in oldest generation.
  reg_exp_.Age();
}

void CompilationCache::EnableScriptAndEval() {
  enabled_script_and_eval_ = true;
}

void CompilationCache::DisableScriptAndEval() {
  enabled_script_and_eval_ = false;
  Clear();
}

}  // namespace internal
}  // namespace v8
