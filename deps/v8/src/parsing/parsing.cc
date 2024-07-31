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

void MaybeReportStatistics(ParseInfo* info, Handle<Script> script,
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

bool ParseProgram(ParseInfo* info, Handle<Script> script,
                  MaybeHandle<ScopeInfo> maybe_outer_scope_info,
                  Isolate* isolate, ReportStatisticsMode mode) {
  DCHECK(info->flags().is_toplevel());
  DCHECK_NULL(info->literal());

  VMState<PARSER> state(isolate);

  // Create a character stream for the parser.
  Handle<String> source(String::cast(script->source()), isolate);
  std::unique_ptr<Utf16CharacterStream> stream(
      ScannerStream::For(isolate, source));
  info->set_character_stream(std::move(stream));

  Parser parser(isolate->main_thread_local_isolate(), info, script);

  // Ok to use Isolate here; this function is only called in the main thread.
  DCHECK(parser.parsing_on_main_thread_);
  parser.ParseProgram(isolate, script, info, maybe_outer_scope_info);
  MaybeReportStatistics(info, script, isolate, &parser, mode);
  return info->literal() != nullptr;
}

bool ParseProgram(ParseInfo* info, Handle<Script> script, Isolate* isolate,
                  ReportStatisticsMode mode) {
  return ParseProgram(info, script, kNullMaybeHandle, isolate, mode);
}

bool ParseFunction(ParseInfo* info, Handle<SharedFunctionInfo> shared_info,
                   Isolate* isolate, ReportStatisticsMode mode) {
  DCHECK(!info->flags().is_toplevel());
  DCHECK(!shared_info.is_null());
  DCHECK_NULL(info->literal());

  VMState<PARSER> state(isolate);

  // Create a character stream for the parser.
  Handle<Script> script(Script::cast(shared_info->script()), isolate);
  Handle<String> source(String::cast(script->source()), isolate);
  std::unique_ptr<Utf16CharacterStream> stream(
      ScannerStream::For(isolate, source, shared_info->StartPosition(),
                         shared_info->EndPosition()));
  info->set_character_stream(std::move(stream));

  Parser parser(isolate->main_thread_local_isolate(), info, script);

  // Ok to use Isolate here; this function is only called in the main thread.
  DCHECK(parser.parsing_on_main_thread_);
  parser.ParseFunction(isolate, info, shared_info);
  MaybeReportStatistics(info, script, isolate, &parser, mode);
  return info->literal() != nullptr;
}

bool ParseAny(ParseInfo* info, Handle<SharedFunctionInfo> shared_info,
              Isolate* isolate, ReportStatisticsMode mode) {
  DCHECK(!shared_info.is_null());
  if (info->flags().is_toplevel()) {
    MaybeHandle<ScopeInfo> maybe_outer_scope_info;
    if (shared_info->HasOuterScopeInfo()) {
      maybe_outer_scope_info =
          handle(shared_info->GetOuterScopeInfo(), isolate);
    }
    return ParseProgram(info,
                        handle(Script::cast(shared_info->script()), isolate),
                        maybe_outer_scope_info, isolate, mode);
  }
  return ParseFunction(info, shared_info, isolate, mode);
}

}  // namespace parsing
}  // namespace internal
}  // namespace v8
