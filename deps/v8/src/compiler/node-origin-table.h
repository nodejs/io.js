// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_ORIGIN_TABLE_H_
#define V8_COMPILER_NODE_ORIGIN_TABLE_H_

#include <limits>

#include "src/base/compiler-specific.h"
#include "src/compiler/node-aux-data.h"

namespace v8 {
namespace internal {
namespace compiler {

class NodeOrigin {
 public:
  enum OriginKind { kWasmBytecode, kGraphNode, kJSBytecode };
  NodeOrigin(const char* phase_name, const char* reducer_name,
             NodeId created_from)
      : phase_name_(phase_name),
        reducer_name_(reducer_name),
        origin_kind_(kGraphNode),
        created_from_(created_from) {}

  NodeOrigin(const char* phase_name, const char* reducer_name,
             OriginKind origin_kind, uint64_t created_from)
      : phase_name_(phase_name),
        reducer_name_(reducer_name),
        origin_kind_(origin_kind),
        created_from_(created_from) {}

  NodeOrigin(const NodeOrigin& other) V8_NOEXCEPT = default;
  NodeOrigin& operator=(const NodeOrigin& other) V8_NOEXCEPT = default;
  static NodeOrigin Unknown() { return NodeOrigin(); }

  bool IsKnown() { return created_from_ >= 0; }
  int64_t created_from() const { return created_from_; }
  const char* reducer_name() const { return reducer_name_; }
  const char* phase_name() const { return phase_name_; }

  OriginKind origin_kind() const { return origin_kind_; }

  bool operator==(const NodeOrigin& o) const {
    return reducer_name_ == o.reducer_name_ && created_from_ == o.created_from_;
  }

  void PrintJson(std::ostream& out) const;

 private:
  NodeOrigin()
      : phase_name_(""),
        reducer_name_(""),
        created_from_(std::numeric_limits<int64_t>::min()) {}
  const char* phase_name_;
  const char* reducer_name_;
  OriginKind origin_kind_;
  int64_t created_from_;
};

inline bool operator!=(const NodeOrigin& lhs, const NodeOrigin& rhs) {
  return !(lhs == rhs);
}

class V8_EXPORT_PRIVATE NodeOriginTable final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  class V8_NODISCARD Scope final {
   public:
    Scope(NodeOriginTable* origins, const char* reducer_name, Node* node)
        : origins_(origins), prev_origin_(NodeOrigin::Unknown()) {
      if (origins) {
        prev_origin_ = origins->current_origin_;
        origins->current_origin_ =
            NodeOrigin(origins->current_phase_name_, reducer_name, node->id());
      }
    }

    ~Scope() {
      if (origins_) origins_->current_origin_ = prev_origin_;
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    NodeOriginTable* const origins_;
    NodeOrigin prev_origin_;
  };

  class V8_NODISCARD PhaseScope final {
   public:
    PhaseScope(NodeOriginTable* origins, const char* phase_name)
        : origins_(origins) {
      if (origins != nullptr) {
        prev_phase_name_ = origins->current_phase_name_;
        origins->current_phase_name_ =
            phase_name == nullptr ? "unnamed" : phase_name;
      }
    }

    ~PhaseScope() {
      if (origins_) origins_->current_phase_name_ = prev_phase_name_;
    }

    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;

   private:
    NodeOriginTable* const origins_;
    const char* prev_phase_name_;
  };

  explicit NodeOriginTable(Graph* graph);
  explicit NodeOriginTable(Zone* zone);
  NodeOriginTable(const NodeOriginTable&) = delete;
  NodeOriginTable& operator=(const NodeOriginTable&) = delete;

  void AddDecorator();
  void RemoveDecorator();

  NodeOrigin GetNodeOrigin(Node* node) const;
  NodeOrigin GetNodeOrigin(NodeId id) const;
  void SetNodeOrigin(Node* node, const NodeOrigin& no);
  void SetNodeOrigin(NodeId id, NodeId origin);
  void SetNodeOrigin(NodeId id, NodeOrigin::OriginKind kind, NodeId origin);

  void SetCurrentPosition(const NodeOrigin& no) { current_origin_ = no; }

  void SetCurrentBytecodePosition(int offset) {
    current_bytecode_position_ = offset;
  }

  int GetCurrentBytecodePosition() { return current_bytecode_position_; }

  void PrintJson(std::ostream& os) const;

 private:
  class Decorator;

  Graph* const graph_;
  Decorator* decorator_;
  NodeOrigin current_origin_;
  int current_bytecode_position_;

  const char* current_phase_name_;
  static NodeOrigin UnknownNodeOrigin(Zone* zone) {
    return NodeOrigin::Unknown();
  }
  NodeAuxData<NodeOrigin, UnknownNodeOrigin> table_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_ORIGIN_TABLE_H_
