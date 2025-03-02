// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/parsing.h"

#include <memory>

#include "src/ast/ast.h"
#include "src/execution/vm-state-inl.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/rewriter.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/zone/zone-list-inl.h"  // crbug.com/v8/8816

namespace v8 {
namespace internal {
namespace parsing {

namespace {

void MaybeReportStatistics(ParseInfo* info, DirectHandle<Script> script,
                           Isolate* isolate, Parser* parser,
                           ReportStatisticsMode mode) {
  switch (mode) {
    case ReportStatisticsMode::kYes:
      parser->UpdateStatistics(isolate, script);
      break;
    case ReportStatisticsMode::kNo:
      break;
  }
}

}  // namespace

bool ParseProgram(ParseInfo* info, DirectHandle<Script> script,
                  MaybeDirectHandle<ScopeInfo> maybe_outer_scope_info,
                  Isolate* isolate, ReportStatisticsMode mode) {
  DCHECK(info->flags().is_toplevel());
  DCHECK_NULL(info->literal());

  VMState<PARSER> state(isolate);

  // Create a character stream for the parser.
  Handle<String> source(Cast<String>(script->source()), isolate);
  std::unique_ptr<Utf16CharacterStream> stream(
      ScannerStream::For(isolate, source));
  info->set_character_stream(std::move(stream));

  Parser parser(isolate->main_thread_local_isolate(), info);

  // Ok to use Isolate here; this function is only called in the main thread.
  DCHECK(parser.parsing_on_main_thread_);
  parser.ParseProgram(isolate, script, info, maybe_outer_scope_info);
  MaybeReportStatistics(info, script, isolate, &parser, mode);
  return info->literal() != nullptr;
}

bool ParseProgram(ParseInfo* info, DirectHandle<Script> script,
                  Isolate* isolate, ReportStatisticsMode mode) {
  return ParseProgram(info, script, kNullMaybeHandle, isolate, mode);
}

bool ParseFunction(ParseInfo* info,
                   DirectHandle<SharedFunctionInfo> shared_info,
                   Isolate* isolate, ReportStatisticsMode mode) {
  DCHECK(!info->flags().is_toplevel());
  DCHECK(!shared_info.is_null());
  DCHECK_NULL(info->literal());

  VMState<PARSER> state(isolate);

  // Create a character stream for the parser.
  DirectHandle<Script> script(Cast<Script>(shared_info->script()), isolate);
  Handle<String> source(Cast<String>(script->source()), isolate);
  uint32_t start_pos = shared_info->StartPosition();
  uint32_t end_pos = shared_info->EndPosition();
  if (end_pos > source->length()) {
    isolate->PushStackTraceAndDie(reinterpret_cast<void*>(script->ptr()),
                                  reinterpret_cast<void*>(source->ptr()));
  }
  std::unique_ptr<Utf16CharacterStream> stream(
      ScannerStream::For(isolate, source, start_pos, end_pos));
  info->set_character_stream(std::move(stream));

  Parser parser(isolate->main_thread_local_isolate(), info);

  // Ok to use Isolate here; this function is only called in the main thread.
  DCHECK(parser.parsing_on_main_thread_);
  parser.ParseFunction(isolate, info, shared_info);
  MaybeReportStatistics(info, script, isolate, &parser, mode);
  return info->literal() != nullptr;
}

bool ParseAny(ParseInfo* info, DirectHandle<SharedFunctionInfo> shared_info,
              Isolate* isolate, ReportStatisticsMode mode) {
  DCHECK(!shared_info.is_null());
  if (info->flags().is_toplevel()) {
    MaybeDirectHandle<ScopeInfo> maybe_outer_scope_info;
    if (shared_info->HasOuterScopeInfo()) {
      maybe_outer_scope_info =
          direct_handle(shared_info->GetOuterScopeInfo(), isolate);
    }
    return ParseProgram(
        info, direct_handle(Cast<Script>(shared_info->script()), isolate),
        maybe_outer_scope_info, isolate, mode);
  }
  return ParseFunction(info, shared_info, isolate, mode);
}

}  // namespace parsing
}  // namespace internal
}  // namespace v8
