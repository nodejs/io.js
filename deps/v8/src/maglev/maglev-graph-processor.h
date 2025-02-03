// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_PROCESSOR_H_
#define V8_MAGLEV_MAGLEV_GRAPH_PROCESSOR_H_

#include "src/base/macros.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

// The GraphProcessor takes a NodeProcessor, and applies it to each Node in the
// Graph by calling NodeProcessor::Process on each Node.
//
// The GraphProcessor also keeps track of the current ProcessingState, and
// passes this to the Process method.
//
// It expects a NodeProcessor class with:
//
//   // A function that processes the graph before the nodes are walked.
//   void PreProcessGraph(Graph* graph);
//
//   // A function that processes the graph after the nodes are walked.
//   void PostProcessGraph(Graph* graph);
//
//   // A function that processes each basic block before its nodes are walked.
//   BlockProcessResult PreProcessBasicBlock(BasicBlock* block);
//
//   // Process methods for each Node type. The GraphProcessor switches over
//   // the Node's opcode, casts it to the appropriate FooNode, and dispatches
//   // to NodeProcessor::Process. It's then up to the NodeProcessor to provide
//   // either distinct Process methods per Node type, or using templates or
//   // overloading as appropriate to group node processing.
//   void Process(FooNode* node, const ProcessingState& state) {}
//
template <typename NodeProcessor, bool visit_identity_nodes = false>
class GraphProcessor;

enum class BlockProcessResult {
  kContinue,  // Process exited normally.
  kSkip,      // Skip processing this block (no MultiProcessor support).
};

enum class ProcessResult {
  kContinue,   // Process exited normally, and the following processors will be
               // called on the node.
  kRemove,     // Remove the current node from the graph (and do not call the
               // following processors).
  kHoist,      // Hoist the current instruction to the parent basic block
               // and reset the current instruction to the beginning of the
               // block. Parent block must be dominating.
  kAbort,      // Stop processing now, do not process subsequent nodes/blocks.
               // Should not be used when processing Constants.
  kSkipBlock,  // Stop processing this block and skip the remaining nodes (no
               // MultiProcessor support).
};

class ProcessingState {
 public:
  explicit ProcessingState(BlockConstIterator block_it,
                           NodeIterator* node_it = nullptr)
      : block_it_(block_it), node_it_(node_it) {}

  // Disallow copies, since the underlying frame states stay mutable.
  ProcessingState(const ProcessingState&) = delete;
  ProcessingState& operator=(const ProcessingState&) = delete;

  BasicBlock* block() const { return *block_it_; }
  BasicBlock* next_block() const { return *(block_it_ + 1); }

  NodeIterator* node_it() const {
    DCHECK_NOT_NULL(node_it_);
    return node_it_;
  }

 private:
  BlockConstIterator block_it_;
  NodeIterator* node_it_;
};

template <typename NodeProcessor, bool visit_identity_nodes>
class GraphProcessor {
 public:
  template <typename... Args>
  explicit GraphProcessor(Args&&... args)
      : node_processor_(std::forward<Args>(args)...) {}

  void ProcessGraph(Graph* graph) {
    graph_ = graph;

    node_processor_.PreProcessGraph(graph);

    auto process_constants = [&](auto& map) {
      for (auto it = map.begin(); it != map.end();) {
        ProcessResult result =
            node_processor_.Process(it->second, GetCurrentState());
        switch (result) {
          [[likely]] case ProcessResult::kContinue:
            ++it;
            break;
          case ProcessResult::kRemove:
            it = map.erase(it);
            break;
          case ProcessResult::kHoist:
          case ProcessResult::kAbort:
          case ProcessResult::kSkipBlock:
            UNREACHABLE();
        }
      }
    };
    process_constants(graph->constants());
    process_constants(graph->root());
    process_constants(graph->smi());
    process_constants(graph->tagged_index());
    process_constants(graph->int32());
    process_constants(graph->uint32());
    process_constants(graph->float64());
    process_constants(graph->external_references());
    process_constants(graph->trusted_constants());

    for (block_it_ = graph->begin(); block_it_ != graph->end(); ++block_it_) {
      BasicBlock* block = *block_it_;

      BlockProcessResult preprocess_result =
          node_processor_.PreProcessBasicBlock(block);
      switch (preprocess_result) {
        [[likely]] case BlockProcessResult::kContinue:
          break;
        case BlockProcessResult::kSkip:
          continue;
      }

      if (block->has_phi()) {
        auto& phis = *block->phis();
        for (auto it = phis.begin(); it != phis.end();) {
          Phi* phi = *it;
          ProcessResult result =
              node_processor_.Process(phi, GetCurrentState());
          switch (result) {
            [[likely]] case ProcessResult::kContinue:
              ++it;
              break;
            case ProcessResult::kRemove:
              it = phis.RemoveAt(it);
              break;
            case ProcessResult::kAbort:
              return;
            case ProcessResult::kSkipBlock:
              goto skip_block;
            case ProcessResult::kHoist:
              UNREACHABLE();
          }
        }
      }

      node_processor_.PostPhiProcessing();

      for (node_it_ = block->nodes().begin();
           node_it_ != block->nodes().end();) {
        Node* node = *node_it_;
        ProcessResult result = ProcessNodeBase(node, GetCurrentState());
        switch (result) {
          [[likely]] case ProcessResult::kContinue:
            ++node_it_;
            break;
          case ProcessResult::kRemove:
            node_it_ = block->nodes().RemoveAt(node_it_);
            break;
          case ProcessResult::kHoist: {
            DCHECK(block->predecessor_count() == 1 ||
                   (block->predecessor_count() == 2 && block->is_loop()));
            BasicBlock* target = block->predecessor_at(0);
            DCHECK(target->successors().size() == 1);
            Node* cur = *node_it_;
            cur->set_owner(target);
            block->nodes().RemoveAt(node_it_);
            target->nodes().Add(cur);
            node_it_ = block->nodes().begin();
            break;
          }
          case ProcessResult::kAbort:
            return;
          case ProcessResult::kSkipBlock:
            goto skip_block;
        }
      }

      {
        ProcessResult control_result =
            ProcessNodeBase(block->control_node(), GetCurrentState());
        switch (control_result) {
          [[likely]] case ProcessResult::kContinue:
          case ProcessResult::kSkipBlock:
            break;
          case ProcessResult::kAbort:
            return;
          case ProcessResult::kRemove:
          case ProcessResult::kHoist:
            UNREACHABLE();
        }
      }
    skip_block:
      continue;
    }

    node_processor_.PostProcessGraph(graph);
  }

  NodeProcessor& node_processor() { return node_processor_; }
  const NodeProcessor& node_processor() const { return node_processor_; }

 private:
  ProcessingState GetCurrentState() {
    return ProcessingState(block_it_, &node_it_);
  }

  ProcessResult ProcessNodeBase(NodeBase* node, const ProcessingState& state) {
    switch (node->opcode()) {
#define CASE(OPCODE)                                        \
  case Opcode::k##OPCODE:                                   \
    if constexpr (!visit_identity_nodes &&                  \
                  Opcode::k##OPCODE == Opcode::kIdentity) { \
      return ProcessResult::kContinue;                      \
    }                                                       \
    PreProcess(node->Cast<OPCODE>(), state);                \
    return node_processor_.Process(node->Cast<OPCODE>(), state);

      NODE_BASE_LIST(CASE)
#undef CASE
    }
  }

  void PreProcess(NodeBase* node, const ProcessingState& state) {}

  NodeProcessor node_processor_;
  Graph* graph_;
  BlockConstIterator block_it_;
  NodeIterator node_it_;
};

// A NodeProcessor that wraps multiple NodeProcessors, and forwards to each of
// them iteratively.
template <typename... Processors>
class NodeMultiProcessor;

template <>
class NodeMultiProcessor<> {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  V8_INLINE ProcessResult Process(NodeBase* node,
                                  const ProcessingState& state) {
    return ProcessResult::kContinue;
  }
  void PostPhiProcessing() {}
};

template <typename Processor, typename... Processors>
class NodeMultiProcessor<Processor, Processors...>
    : NodeMultiProcessor<Processors...> {
  using Base = NodeMultiProcessor<Processors...>;

 public:
  template <typename... Args>
  explicit NodeMultiProcessor(Processor&& processor, Args&&... processors)
      : Base(std::forward<Args>(processors)...),
        processor_(std::forward<Processor>(processor)) {}
  template <typename... Args>
  explicit NodeMultiProcessor(Args&&... processors)
      : Base(std::forward<Args>(processors)...) {}

  template <typename Node>
  ProcessResult Process(Node* node, const ProcessingState& state) {
    auto res = processor_.Process(node, state);
    switch (res) {
      [[likely]] case ProcessResult::kContinue:
        return Base::Process(node, state);
      case ProcessResult::kAbort:
      case ProcessResult::kRemove:
        return res;
      case ProcessResult::kHoist:
      case ProcessResult::kSkipBlock:
        // TODO(olivf): How to combine these with multiple processors depends on
        // the needs of the actual processors. Implement once needed.
        UNREACHABLE();
    }
  }
  void PreProcessGraph(Graph* graph) {
    processor_.PreProcessGraph(graph);
    Base::PreProcessGraph(graph);
  }
  void PostProcessGraph(Graph* graph) {
    // Post process in reverse order because that kind of makes sense.
    Base::PostProcessGraph(graph);
    processor_.PostProcessGraph(graph);
  }
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    BlockProcessResult res = processor_.PreProcessBasicBlock(block);
    switch (res) {
      [[likely]] case BlockProcessResult::kContinue:
        return Base::PreProcessBasicBlock(block);
      case BlockProcessResult::kSkip:
        // TODO(olivf): How to combine this with multiple processors depends on
        // the needs of the actual processors. Implement once needed.
        UNREACHABLE();
    }
  }
  void PostPhiProcessing() {
    processor_.PostPhiProcessing();
    Base::PostPhiProcessing();
  }

 private:
  Processor processor_;
};

template <typename... Processors>
using GraphMultiProcessor = GraphProcessor<NodeMultiProcessor<Processors...>>;

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_PROCESSOR_H_
