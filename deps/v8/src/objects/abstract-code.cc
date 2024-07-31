// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/abstract-code.h"

#include "src/objects/abstract-code-inl.h"

namespace v8 {
namespace internal {

// TODO(cbruni): Move to BytecodeArray
int AbstractCode::SourcePosition(Isolate* isolate, int offset) {
  CHECK_NE(kind(isolate), CodeKind::BASELINE);
  Tagged<TrustedByteArray> source_position_table =
      SourcePositionTableInternal(isolate);

  // Subtract one because the current PC is one instruction after the call site.
  if (IsCode(*this)) offset--;
  int position = 0;
  for (SourcePositionTableIterator iterator(
           source_position_table, SourcePositionTableIterator::kJavaScriptOnly,
           SourcePositionTableIterator::kDontSkipFunctionEntry);
       !iterator.done() && iterator.code_offset() <= offset;
       iterator.Advance()) {
    position = iterator.source_position().ScriptOffset();
  }
  return position;
}

// TODO(cbruni): Move to BytecodeArray
int AbstractCode::SourceStatementPosition(Isolate* isolate, int offset) {
  CHECK_NE(kind(isolate), CodeKind::BASELINE);
  // First find the closest position.
  int position = SourcePosition(isolate, offset);
  // Now find the closest statement position before the position.
  int statement_position = 0;
  for (SourcePositionTableIterator it(SourcePositionTableInternal(isolate));
       !it.done(); it.Advance()) {
    if (it.is_statement()) {
      int p = it.source_position().ScriptOffset();
      if (statement_position < p && p <= position) {
        statement_position = p;
      }
    }
  }
  return statement_position;
}

}  // namespace internal
}  // namespace v8
