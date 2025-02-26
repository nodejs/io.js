// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/handler-table-builder.h"

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/interpreter/bytecode-register.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

HandlerTableBuilder::HandlerTableBuilder(Zone* zone) : entries_(zone) {}

template <typename IsolateT>
DirectHandle<TrustedByteArray> HandlerTableBuilder::ToHandlerTable(
    IsolateT* isolate) {
  int handler_table_size = static_cast<int>(entries_.size());
  DirectHandle<TrustedByteArray> table_byte_array =
      isolate->factory()->NewTrustedByteArray(
          HandlerTable::LengthForRange(handler_table_size));
  HandlerTable table(*table_byte_array);
  for (int i = 0; i < handler_table_size; ++i) {
    Entry& entry = entries_[i];
    HandlerTable::CatchPrediction pred = entry.catch_prediction_;
    table.SetRangeStart(i, static_cast<int>(entry.offset_start));
    table.SetRangeEnd(i, static_cast<int>(entry.offset_end));
    table.SetRangeHandler(i, static_cast<int>(entry.offset_target), pred);
    table.SetRangeData(i, entry.context.index());
  }
  return table_byte_array;
}

template DirectHandle<TrustedByteArray> HandlerTableBuilder::ToHandlerTable(
    Isolate* isolate);
template DirectHandle<TrustedByteArray> HandlerTableBuilder::ToHandlerTable(
    LocalIsolate* isolate);

int HandlerTableBuilder::NewHandlerEntry() {
  int handler_id = static_cast<int>(entries_.size());
  Entry entry = {0, 0, 0, Register::invalid_value(), HandlerTable::UNCAUGHT};
  entries_.push_back(entry);
  return handler_id;
}


void HandlerTableBuilder::SetTryRegionStart(int handler_id, size_t offset) {
  DCHECK(Smi::IsValid(offset));  // Encoding of handler table requires this.
  entries_[handler_id].offset_start = offset;
}


void HandlerTableBuilder::SetTryRegionEnd(int handler_id, size_t offset) {
  DCHECK(Smi::IsValid(offset));  // Encoding of handler table requires this.
  entries_[handler_id].offset_end = offset;
}


void HandlerTableBuilder::SetHandlerTarget(int handler_id, size_t offset) {
  DCHECK(Smi::IsValid(offset));  // Encoding of handler table requires this.
  entries_[handler_id].offset_target = offset;
}

void HandlerTableBuilder::SetPrediction(
    int handler_id, HandlerTable::CatchPrediction prediction) {
  entries_[handler_id].catch_prediction_ = prediction;
}


void HandlerTableBuilder::SetContextRegister(int handler_id, Register reg) {
  entries_[handler_id].context = reg;
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
