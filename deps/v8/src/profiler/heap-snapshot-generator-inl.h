// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_HEAP_SNAPSHOT_GENERATOR_INL_H_
#define V8_PROFILER_HEAP_SNAPSHOT_GENERATOR_INL_H_

#include "src/profiler/heap-snapshot-generator.h"

#include "src/profiler/heap-profiler.h"
#include "src/strings/string-hasher-inl.h"

namespace v8 {
namespace internal {

HeapEntry* HeapGraphEdge::from() const {
  return &snapshot()->entries()[from_index()];
}

Isolate* HeapGraphEdge::isolate() const { return to_entry_->isolate(); }

HeapSnapshot* HeapGraphEdge::snapshot() const {
  return to_entry_->snapshot();
}

int HeapEntry::set_children_index(int index) {
  // Note: children_count_ and children_end_index_ are parts of a union.
  int next_index = index + children_count_;
  children_end_index_ = index;
  return next_index;
}

void HeapEntry::add_child(HeapGraphEdge* edge) {
  snapshot_->children()[children_end_index_++] = edge;
}

HeapGraphEdge* HeapEntry::child(int i) { return children_begin()[i]; }

std::vector<HeapGraphEdge*>::iterator HeapEntry::children_begin() const {
  return index_ == 0 ? snapshot_->children().begin()
                     : snapshot_->entries()[index_ - 1].children_end();
}

std::vector<HeapGraphEdge*>::iterator HeapEntry::children_end() const {
  DCHECK_GE(children_end_index_, 0);
  return snapshot_->children().begin() + children_end_index_;
}

int HeapEntry::children_count() const {
  return static_cast<int>(children_end() - children_begin());
}

Isolate* HeapEntry::isolate() const { return snapshot_->profiler()->isolate(); }

uint32_t HeapSnapshotJSONSerializer::StringHash(const void* string) {
  const char* s = reinterpret_cast<const char*>(string);
  int len = static_cast<int>(strlen(s));
  return StringHasher::HashSequentialString(s, len,
                                            v8::internal::kZeroHashSeed);
}

int HeapSnapshotJSONSerializer::to_node_index(const HeapEntry* e) {
  return to_node_index(e->index());
}

int HeapSnapshotJSONSerializer::to_node_index(int entry_index) {
  return entry_index * (trace_function_count_
                            ? kNodeFieldsCountWithTraceNodeId
                            : kNodeFieldsCountWithoutTraceNodeId);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_HEAP_SNAPSHOT_GENERATOR_INL_H_
