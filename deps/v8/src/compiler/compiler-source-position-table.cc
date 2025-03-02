// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/compiler-source-position-table.h"

#include "src/compiler/node-aux-data.h"
#include "src/compiler/turbofan-graph.h"

namespace v8 {
namespace internal {
namespace compiler {

class SourcePositionTable::Decorator final : public GraphDecorator {
 public:
  explicit Decorator(SourcePositionTable* source_positions)
      : source_positions_(source_positions) {}

  void Decorate(Node* node) final {
    source_positions_->SetSourcePosition(node,
                                         source_positions_->current_position_);
  }

 private:
  SourcePositionTable* source_positions_;
};

SourcePositionTable::SourcePositionTable(Graph* graph)
    : graph_(graph),
      decorator_(nullptr),
      current_position_(SourcePosition::Unknown()),
      table_(graph->zone()) {}

void SourcePositionTable::AddDecorator() {
  DCHECK_NULL(decorator_);
  if (!enabled_) return;
  decorator_ = graph_->zone()->New<Decorator>(this);
  graph_->AddDecorator(decorator_);
}

void SourcePositionTable::RemoveDecorator() {
  if (!enabled_) {
    DCHECK_NULL(decorator_);
    return;
  }
  DCHECK_NOT_NULL(decorator_);
  graph_->RemoveDecorator(decorator_);
  decorator_ = nullptr;
}

SourcePosition SourcePositionTable::GetSourcePosition(Node* node) const {
  return table_.Get(node);
}
SourcePosition SourcePositionTable::GetSourcePosition(NodeId id) const {
  return table_.Get(id);
}

void SourcePositionTable::SetSourcePosition(Node* node,
                                            SourcePosition position) {
  DCHECK(IsEnabled());
  table_.Set(node, position);
}

void SourcePositionTable::PrintJson(std::ostream& os) const {
  os << "{";
  bool needs_comma = false;
  for (auto i : table_) {
    SourcePosition pos = i.second;
    if (pos.IsKnown()) {
      if (needs_comma) {
        os << ",";
      }
      os << "\"" << i.first << "\" : ";
      pos.PrintJson(os);
      needs_comma = true;
    }
  }
  os << "}";
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
