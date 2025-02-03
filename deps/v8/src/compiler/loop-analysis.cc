// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/loop-analysis.h"

#include "src/codegen/tick-counter.h"
#include "src/compiler/all-nodes.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

#define OFFSET(x) ((x)&0x1F)
#define BIT(x) (1u << OFFSET(x))
#define INDEX(x) ((x) >> 5)

// Temporary information for each node during marking.
struct NodeInfo {
  Node* node;
  NodeInfo* next;  // link in chaining loop members
  bool backwards_visited;
};


// Temporary loop info needed during traversal and building the loop tree.
struct TempLoopInfo {
  Node* header;
  NodeInfo* header_list;
  NodeInfo* exit_list;
  NodeInfo* body_list;
  LoopTree::Loop* loop;
};

// Encapsulation of the loop finding algorithm.
// -----------------------------------------------------------------------------
// Conceptually, the contents of a loop are those nodes that are "between" the
// loop header and the backedges of the loop. Graphs in the soup of nodes can
// form improper cycles, so standard loop finding algorithms that work on CFGs
// aren't sufficient. However, in valid TurboFan graphs, all cycles involve
// either a {Loop} node or a phi. The {Loop} node itself and its accompanying
// phis are treated together as a set referred to here as the loop header.
// This loop finding algorithm works by traversing the graph in two directions,
// first from nodes to their inputs, starting at {end}, then in the reverse
// direction, from nodes to their uses, starting at loop headers.
// 1 bit per loop per node per direction are required during the marking phase.
// To handle nested loops correctly, the algorithm must filter some reachability
// marks on edges into/out-of the loop header nodes.
// Note: this algorithm assumes there are no unreachable loop header nodes
// (including loop phis).
class LoopFinderImpl {
 public:
  LoopFinderImpl(Graph* graph, LoopTree* loop_tree, TickCounter* tick_counter,
                 Zone* zone)
      : zone_(zone),
        end_(graph->end()),
        queue_(zone),
        queued_(graph, 2),
        info_(graph->NodeCount(), {nullptr, nullptr, false}, zone),
        loops_(zone),
        loop_num_(graph->NodeCount(), -1, zone),
        loop_tree_(loop_tree),
        loops_found_(0),
        width_(0),
        backward_(nullptr),
        forward_(nullptr),
        tick_counter_(tick_counter) {}

  void Run() {
    PropagateBackward();
    PropagateForward();
    FinishLoopTree();
  }

  void Print() {
    // Print out the results.
    for (NodeInfo& ni : info_) {
      if (ni.node == nullptr) continue;
      for (int i = 1; i <= loops_found_; i++) {
        int index = ni.node->id() * width_ + INDEX(i);
        bool marked_forward = forward_[index] & BIT(i);
        bool marked_backward = backward_[index] & BIT(i);
        if (marked_forward && marked_backward) {
          PrintF("X");
        } else if (marked_forward) {
          PrintF(">");
        } else if (marked_backward) {
          PrintF("<");
        } else {
          PrintF(" ");
        }
      }
      PrintF(" #%d:%s\n", ni.node->id(), ni.node->op()->mnemonic());
    }

    int i = 0;
    for (TempLoopInfo& li : loops_) {
      PrintF("Loop %d headed at #%d\n", i, li.header->id());
      i++;
    }

    for (LoopTree::Loop* loop : loop_tree_->outer_loops_) {
      PrintLoop(loop);
    }
  }

 private:
  Zone* zone_;
  Node* end_;
  NodeDeque queue_;
  NodeMarker<bool> queued_;
  ZoneVector<NodeInfo> info_;
  ZoneVector<TempLoopInfo> loops_;
  ZoneVector<int> loop_num_;
  LoopTree* loop_tree_;
  int loops_found_;
  int width_;
  uint32_t* backward_;
  uint32_t* forward_;
  TickCounter* const tick_counter_;

  int num_nodes() {
    return static_cast<int>(loop_tree_->node_to_loop_num_.size());
  }

  // Tb = Tb | (Fb - loop_filter)
  bool PropagateBackwardMarks(Node* from, Node* to, int loop_filter) {
    if (from == to) return false;
    uint32_t* fp = &backward_[from->id() * width_];
    uint32_t* tp = &backward_[to->id() * width_];
    bool change = false;
    for (int i = 0; i < width_; i++) {
      uint32_t mask = i == INDEX(loop_filter) ? ~BIT(loop_filter) : 0xFFFFFFFF;
      uint32_t prev = tp[i];
      uint32_t next = prev | (fp[i] & mask);
      tp[i] = next;
      if (!change && (prev != next)) change = true;
    }
    return change;
  }

  // Tb = Tb | B
  bool SetBackwardMark(Node* to, int loop_num) {
    uint32_t* tp = &backward_[to->id() * width_ + INDEX(loop_num)];
    uint32_t prev = tp[0];
    uint32_t next = prev | BIT(loop_num);
    tp[0] = next;
    return next != prev;
  }

  // Tf = Tf | B
  bool SetForwardMark(Node* to, int loop_num) {
    uint32_t* tp = &forward_[to->id() * width_ + INDEX(loop_num)];
    uint32_t prev = tp[0];
    uint32_t next = prev | BIT(loop_num);
    tp[0] = next;
    return next != prev;
  }

  // Tf = Tf | (Ff & Tb)
  bool PropagateForwardMarks(Node* from, Node* to) {
    if (from == to) return false;
    bool change = false;
    int findex = from->id() * width_;
    int tindex = to->id() * width_;
    for (int i = 0; i < width_; i++) {
      uint32_t marks = backward_[tindex + i] & forward_[findex + i];
      uint32_t prev = forward_[tindex + i];
      uint32_t next = prev | marks;
      forward_[tindex + i] = next;
      if (!change && (prev != next)) change = true;
    }
    return change;
  }

  bool IsInLoop(Node* node, int loop_num) {
    int offset = node->id() * width_ + INDEX(loop_num);
    return backward_[offset] & forward_[offset] & BIT(loop_num);
  }

  // Propagate marks backward from loop headers.
  void PropagateBackward() {
    ResizeBackwardMarks();
    SetBackwardMark(end_, 0);
    Queue(end_);

    while (!queue_.empty()) {
      tick_counter_->TickAndMaybeEnterSafepoint();
      Node* node = queue_.front();
      info(node).backwards_visited = true;
      queue_.pop_front();
      queued_.Set(node, false);

      int loop_num = -1;
      // Setup loop headers first.
      if (node->opcode() == IrOpcode::kLoop) {
        // found the loop node first.
        loop_num = CreateLoopInfo(node);
      } else if (NodeProperties::IsPhi(node)) {
        // found a phi first.
        Node* merge = node->InputAt(node->InputCount() - 1);
        if (merge->opcode() == IrOpcode::kLoop) {
          loop_num = CreateLoopInfo(merge);
        }
      } else if (node->opcode() == IrOpcode::kLoopExit) {
        // Intentionally ignore return value. Loop exit node marks
        // are propagated normally.
        CreateLoopInfo(node->InputAt(1));
      } else if (node->opcode() == IrOpcode::kLoopExitValue ||
                 node->opcode() == IrOpcode::kLoopExitEffect) {
        Node* loop_exit = NodeProperties::GetControlInput(node);
        // Intentionally ignore return value. Loop exit node marks
        // are propagated normally.
        CreateLoopInfo(loop_exit->InputAt(1));
      }

      // Propagate marks backwards from this node.
      for (int i = 0; i < node->InputCount(); i++) {
        Node* input = node->InputAt(i);
        if (IsBackedge(node, i)) {
          // Only propagate the loop mark on backedges.
          if (SetBackwardMark(input, loop_num) ||
              !info(input).backwards_visited) {
            Queue(input);
          }
        } else {
          // Entry or normal edge. Propagate all marks except loop_num.
          // TODO(manoskouk): Add test that needs backwards_visited to function
          // correctly, probably using wasm loop unrolling when it is available.
          if (PropagateBackwardMarks(node, input, loop_num) ||
              !info(input).backwards_visited) {
            Queue(input);
          }
        }
      }
    }
  }

  // Make a new loop if necessary for the given node.
  int CreateLoopInfo(Node* node) {
    DCHECK_EQ(IrOpcode::kLoop, node->opcode());
    int loop_num = LoopNum(node);
    if (loop_num > 0) return loop_num;

    loop_num = ++loops_found_;
    if (INDEX(loop_num) >= width_) ResizeBackwardMarks();

    // Create a new loop.
    loops_.push_back({node, nullptr, nullptr, nullptr, nullptr});
    loop_tree_->NewLoop();
    SetLoopMarkForLoopHeader(node, loop_num);
    return loop_num;
  }

  void SetLoopMark(Node* node, int loop_num) {
    info(node);  // create the NodeInfo
    SetBackwardMark(node, loop_num);
    loop_tree_->node_to_loop_num_[node->id()] = loop_num;
  }

  void SetLoopMarkForLoopHeader(Node* node, int loop_num) {
    DCHECK_EQ(IrOpcode::kLoop, node->opcode());
    SetLoopMark(node, loop_num);
    for (Node* use : node->uses()) {
      if (NodeProperties::IsPhi(use)) {
        SetLoopMark(use, loop_num);
      }

      // Do not keep the loop alive if it does not have any backedges.
      if (node->InputCount() <= 1) continue;

      if (use->opcode() == IrOpcode::kLoopExit) {
        SetLoopMark(use, loop_num);
        for (Node* exit_use : use->uses()) {
          if (exit_use->opcode() == IrOpcode::kLoopExitValue ||
              exit_use->opcode() == IrOpcode::kLoopExitEffect) {
            SetLoopMark(exit_use, loop_num);
          }
        }
      }
    }
  }

  void ResizeBackwardMarks() {
    int new_width = width_ + 1;
    int max = num_nodes();
    uint32_t* new_backward = zone_->AllocateArray<uint32_t>(new_width * max);
    memset(new_backward, 0, new_width * max * sizeof(uint32_t));
    if (width_ > 0) {  // copy old matrix data.
      for (int i = 0; i < max; i++) {
        uint32_t* np = &new_backward[i * new_width];
        uint32_t* op = &backward_[i * width_];
        for (int j = 0; j < width_; j++) np[j] = op[j];
      }
    }
    width_ = new_width;
    backward_ = new_backward;
  }

  void ResizeForwardMarks() {
    int max = num_nodes();
    forward_ = zone_->AllocateArray<uint32_t>(width_ * max);
    memset(forward_, 0, width_ * max * sizeof(uint32_t));
  }

  // Propagate marks forward from loops.
  void PropagateForward() {
    ResizeForwardMarks();
    for (TempLoopInfo& li : loops_) {
      SetForwardMark(li.header, LoopNum(li.header));
      Queue(li.header);
    }
    // Propagate forward on paths that were backward reachable from backedges.
    while (!queue_.empty()) {
      tick_counter_->TickAndMaybeEnterSafepoint();
      Node* node = queue_.front();
      queue_.pop_front();
      queued_.Set(node, false);
      for (Edge edge : node->use_edges()) {
        Node* use = edge.from();
        if (!IsBackedge(use, edge.index())) {
          if (PropagateForwardMarks(node, use)) Queue(use);
        }
      }
    }
  }

  bool IsLoopHeaderNode(Node* node) {
    return node->opcode() == IrOpcode::kLoop || NodeProperties::IsPhi(node);
  }

  bool IsLoopExitNode(Node* node) {
    return node->opcode() == IrOpcode::kLoopExit ||
           node->opcode() == IrOpcode::kLoopExitValue ||
           node->opcode() == IrOpcode::kLoopExitEffect;
  }

  bool IsBackedge(Node* use, int index) {
    if (LoopNum(use) <= 0) return false;
    if (NodeProperties::IsPhi(use)) {
      return index != NodeProperties::FirstControlIndex(use) &&
             index != kAssumedLoopEntryIndex;
    } else if (use->opcode() == IrOpcode::kLoop) {
      return index != kAssumedLoopEntryIndex;
    }
    DCHECK(IsLoopExitNode(use));
    return false;
  }

  int LoopNum(Node* node) { return loop_tree_->node_to_loop_num_[node->id()]; }

  NodeInfo& info(Node* node) {
    NodeInfo& i = info_[node->id()];
    if (i.node == nullptr) i.node = node;
    return i;
  }

  void Queue(Node* node) {
    if (!queued_.Get(node)) {
      queue_.push_back(node);
      queued_.Set(node, true);
    }
  }

  void AddNodeToLoop(NodeInfo* node_info, TempLoopInfo* loop, int loop_num) {
    if (LoopNum(node_info->node) == loop_num) {
      if (IsLoopHeaderNode(node_info->node)) {
        node_info->next = loop->header_list;
        loop->header_list = node_info;
      } else {
        DCHECK(IsLoopExitNode(node_info->node));
        node_info->next = loop->exit_list;
        loop->exit_list = node_info;
      }
    } else {
      node_info->next = loop->body_list;
      loop->body_list = node_info;
    }
  }

  void FinishLoopTree() {
    DCHECK(loops_found_ == static_cast<int>(loops_.size()));
    DCHECK(loops_found_ == static_cast<int>(loop_tree_->all_loops_.size()));

    // Degenerate cases.
    if (loops_found_ == 0) return;
    if (loops_found_ == 1) return FinishSingleLoop();

    for (int i = 1; i <= loops_found_; i++) ConnectLoopTree(i);

    size_t count = 0;
    // Place the node into the innermost nested loop of which it is a member.
    for (NodeInfo& ni : info_) {
      if (ni.node == nullptr) continue;

      TempLoopInfo* innermost = nullptr;
      int innermost_index = 0;
      int pos = ni.node->id() * width_;
      // Search the marks word by word.
      for (int i = 0; i < width_; i++) {
        uint32_t marks = backward_[pos + i] & forward_[pos + i];

        for (int j = 0; j < 32; j++) {
          if (marks & (1u << j)) {
            int loop_num = i * 32 + j;
            if (loop_num == 0) continue;
            TempLoopInfo* loop = &loops_[loop_num - 1];
            if (innermost == nullptr ||
                loop->loop->depth_ > innermost->loop->depth_) {
              innermost = loop;
              innermost_index = loop_num;
            }
          }
        }
      }
      if (innermost == nullptr) continue;

      // Return statements should never be found by forward or backward walk.
      CHECK(ni.node->opcode() != IrOpcode::kReturn);

      AddNodeToLoop(&ni, innermost, innermost_index);
      count++;
    }

    // Serialize the node lists for loops into the loop tree.
    loop_tree_->loop_nodes_.reserve(count);
    for (LoopTree::Loop* loop : loop_tree_->outer_loops_) {
      SerializeLoop(loop);
    }
  }

  // Handle the simpler case of a single loop (no checks for nesting necessary).
  void FinishSingleLoop() {
    // Place nodes into the loop header and body.
    TempLoopInfo* li = &loops_[0];
    li->loop = &loop_tree_->all_loops_[0];
    loop_tree_->SetParent(nullptr, li->loop);
    size_t count = 0;
    for (NodeInfo& ni : info_) {
      if (ni.node == nullptr || !IsInLoop(ni.node, 1)) continue;

      // Return statements should never be found by forward or backward walk.
      CHECK(ni.node->opcode() != IrOpcode::kReturn);

      AddNodeToLoop(&ni, li, 1);
      count++;
    }

    // Serialize the node lists for the loop into the loop tree.
    loop_tree_->loop_nodes_.reserve(count);
    SerializeLoop(li->loop);
  }

  // Recursively serialize the list of header nodes and body nodes
  // so that nested loops occupy nested intervals.
  void SerializeLoop(LoopTree::Loop* loop) {
    int loop_num = loop_tree_->LoopNum(loop);
    TempLoopInfo& li = loops_[loop_num - 1];

    // Serialize the header.
    loop->header_start_ = static_cast<int>(loop_tree_->loop_nodes_.size());
    for (NodeInfo* ni = li.header_list; ni != nullptr; ni = ni->next) {
      loop_tree_->loop_nodes_.push_back(ni->node);
      loop_tree_->node_to_loop_num_[ni->node->id()] = loop_num;
    }

    // Serialize the body.
    loop->body_start_ = static_cast<int>(loop_tree_->loop_nodes_.size());
    for (NodeInfo* ni = li.body_list; ni != nullptr; ni = ni->next) {
      loop_tree_->loop_nodes_.push_back(ni->node);
      loop_tree_->node_to_loop_num_[ni->node->id()] = loop_num;
    }

    // Serialize nested loops.
    for (LoopTree::Loop* child : loop->children_) SerializeLoop(child);

    // Serialize the exits.
    loop->exits_start_ = static_cast<int>(loop_tree_->loop_nodes_.size());
    for (NodeInfo* ni = li.exit_list; ni != nullptr; ni = ni->next) {
      loop_tree_->loop_nodes_.push_back(ni->node);
      loop_tree_->node_to_loop_num_[ni->node->id()] = loop_num;
    }

    loop->exits_end_ = static_cast<int>(loop_tree_->loop_nodes_.size());
  }

  // Connect the LoopTree loops to their parents recursively.
  LoopTree::Loop* ConnectLoopTree(int loop_num) {
    TempLoopInfo& li = loops_[loop_num - 1];
    if (li.loop != nullptr) return li.loop;

    NodeInfo& ni = info(li.header);
    LoopTree::Loop* parent = nullptr;
    for (int i = 1; i <= loops_found_; i++) {
      if (i == loop_num) continue;
      if (IsInLoop(ni.node, i)) {
        // recursively create potential parent loops first.
        LoopTree::Loop* upper = ConnectLoopTree(i);
        if (parent == nullptr || upper->depth_ > parent->depth_) {
          parent = upper;
        }
      }
    }
    li.loop = &loop_tree_->all_loops_[loop_num - 1];
    loop_tree_->SetParent(parent, li.loop);
    return li.loop;
  }

  void PrintLoop(LoopTree::Loop* loop) {
    for (int i = 0; i < loop->depth_; i++) PrintF("  ");
    PrintF("Loop depth = %d ", loop->depth_);
    int i = loop->header_start_;
    while (i < loop->body_start_) {
      PrintF(" H#%d", loop_tree_->loop_nodes_[i++]->id());
    }
    while (i < loop->exits_start_) {
      PrintF(" B#%d", loop_tree_->loop_nodes_[i++]->id());
    }
    while (i < loop->exits_end_) {
      PrintF(" E#%d", loop_tree_->loop_nodes_[i++]->id());
    }
    PrintF("\n");
    for (LoopTree::Loop* child : loop->children_) PrintLoop(child);
  }
};

LoopTree* LoopFinder::BuildLoopTree(Graph* graph, TickCounter* tick_counter,
                                    Zone* zone) {
  LoopTree* loop_tree =
      graph->zone()->New<LoopTree>(graph->NodeCount(), graph->zone());
  LoopFinderImpl finder(graph, loop_tree, tick_counter, zone);
  finder.Run();
  if (v8_flags.trace_turbo_loop) {
    finder.Print();
  }
  return loop_tree;
}

#if V8_ENABLE_WEBASSEMBLY
// static
ZoneUnorderedSet<Node*>* LoopFinder::FindSmallInnermostLoopFromHeader(
    Node* loop_header, AllNodes& all_nodes, Zone* zone, size_t max_size,
    Purpose purpose) {
  auto* visited = zone->New<ZoneUnorderedSet<Node*>>(zone);
  std::vector<Node*> queue;

  DCHECK_EQ(loop_header->opcode(), IrOpcode::kLoop);

  queue.push_back(loop_header);
  visited->insert(loop_header);

#define ENQUEUE_USES(use_name, condition)             \
  for (Node * use_name : node->uses()) {              \
    if (condition && visited->count(use_name) == 0) { \
      visited->insert(use_name);                      \
      queue.push_back(use_name);                      \
    }                                                 \
  }
  bool has_instruction_worth_peeling = false;
  while (!queue.empty()) {
    Node* node = queue.back();
    queue.pop_back();
    if (node->opcode() == IrOpcode::kEnd) {
      // We reached the end of the graph. The end node is not part of the loop.
      visited->erase(node);
      continue;
    }
    if (visited->size() > max_size) return nullptr;
    switch (node->opcode()) {
      case IrOpcode::kLoop:
        // Found nested loop.
        if (node != loop_header) return nullptr;
        ENQUEUE_USES(use, true);
        break;
      case IrOpcode::kLoopExit:
        // Found nested loop.
        if (node->InputAt(1) != loop_header) return nullptr;
        // LoopExitValue/Effect uses are inside the loop. The rest are not.
        ENQUEUE_USES(use, (use->opcode() == IrOpcode::kLoopExitEffect ||
                           use->opcode() == IrOpcode::kLoopExitValue))
        break;
      case IrOpcode::kLoopExitEffect:
      case IrOpcode::kLoopExitValue:
        if (NodeProperties::GetControlInput(node)->InputAt(1) != loop_header) {
          // Found nested loop.
          return nullptr;
        }
        // All uses are outside the loop, do nothing.
        break;
      // If unrolling, call nodes are considered to have unbounded size,
      // i.e. >max_size, with the exception of certain wasm builtins.
      case IrOpcode::kTailCall:
      case IrOpcode::kJSWasmCall:
      case IrOpcode::kJSCall:
        if (purpose == Purpose::kLoopUnrolling) return nullptr;
        ENQUEUE_USES(use, true)
        break;
      case IrOpcode::kCall: {
        if (purpose == Purpose::kLoopPeeling) {
          ENQUEUE_USES(use, true);
          break;
        }
        Node* callee = node->InputAt(0);
        if (callee->opcode() != IrOpcode::kRelocatableInt32Constant &&
            callee->opcode() != IrOpcode::kRelocatableInt64Constant) {
          return nullptr;
        }
        Builtin builtin = static_cast<Builtin>(
            OpParameter<RelocatablePtrConstantInfo>(callee->op()).value());
        constexpr Builtin unrollable_builtins[] = {
            // Exists in every stack check.
            Builtin::kWasmStackGuard,
            // Fast table operations.
            Builtin::kWasmTableGet, Builtin::kWasmTableSet,
            Builtin::kWasmTableGetFuncRef, Builtin::kWasmTableSetFuncRef,
            Builtin::kWasmTableGrow,
            // Atomics.
            Builtin::kWasmI32AtomicWait, Builtin::kWasmI64AtomicWait,
            // Exceptions.
            Builtin::kWasmAllocateFixedArray, Builtin::kWasmThrow,
            Builtin::kWasmRethrow, Builtin::kWasmRethrowExplicitContext,
            // Fast wasm-gc operations.
            Builtin::kWasmRefFunc,
            // While a built-in call, this is the slow path, so it should not
            // prevent loop unrolling for stringview_wtf16.get_codeunit.
            Builtin::kWasmStringViewWtf16GetCodeUnit};
        if (std::count(std::begin(unrollable_builtins),
                       std::end(unrollable_builtins), builtin) == 0) {
          return nullptr;
        }
        ENQUEUE_USES(use, true)
        break;
      }
      case IrOpcode::kWasmStructGet: {
        // When a chained load occurs in the loop, assume that peeling might
        // help.
        Node* object = node->InputAt(0);
        if (object->opcode() == IrOpcode::kWasmStructGet &&
            visited->find(object) != visited->end()) {
          has_instruction_worth_peeling = true;
        }
        ENQUEUE_USES(use, true);
        break;
      }
      case IrOpcode::kWasmArrayGet:
        // Rationale for array.get: loops that contain an array.get also
        // contain a bounds check, which needs to load the array's length,
        // which benefits from load elimination after peeling.
      case IrOpcode::kStringPrepareForGetCodeunit:
        // Rationale for PrepareForGetCodeunit: this internal operation is
        // specifically designed for being hoisted out of loops.
        has_instruction_worth_peeling = true;
        [[fallthrough]];
      default:
        ENQUEUE_USES(use, true)
        break;
    }
  }

  // Check that there is no floating control other than direct nodes to start().
  // We do this by checking that all non-start control inputs of loop nodes are
  // also in the loop.
  // TODO(manoskouk): This is a safety check. Consider making it DEBUG-only when
  // we are confident there is no incompatible floating control generated in
  // wasm.
  for (Node* node : *visited) {
    // The loop header is allowed to point outside the loop.
    if (node == loop_header) continue;

    if (!all_nodes.IsLive(node)) continue;

    for (Edge edge : node->input_edges()) {
      Node* input = edge.to();
      if (NodeProperties::IsControlEdge(edge) && visited->count(input) == 0 &&
          input->opcode() != IrOpcode::kStart) {
        FATAL(
            "Floating control detected in wasm turbofan graph: Node #%d:%s is "
            "inside loop headed by #%d, but its control dependency #%d:%s is "
            "outside",
            node->id(), node->op()->mnemonic(), loop_header->id(), input->id(),
            input->op()->mnemonic());
      }
    }
  }

  // Only peel functions containing instructions for which loop peeling is known
  // to be useful. TODO(14034): Add more instructions to get more benefits out
  // of loop peeling.
  if (purpose == Purpose::kLoopPeeling && !has_instruction_worth_peeling) {
    return nullptr;
  }
  return visited;
}
#endif  // V8_ENABLE_WEBASSEMBLY

bool LoopFinder::HasMarkedExits(LoopTree* loop_tree,
                                const LoopTree::Loop* loop) {
  // Look for returns and if projections that are outside the loop but whose
  // control input is inside the loop.
  Node* loop_node = loop_tree->GetLoopControl(loop);
  for (Node* node : loop_tree->LoopNodes(loop)) {
    for (Node* use : node->uses()) {
      if (!loop_tree->Contains(loop, use)) {
        bool unmarked_exit;
        switch (node->opcode()) {
          case IrOpcode::kLoopExit:
            unmarked_exit = (node->InputAt(1) != loop_node);
            break;
          case IrOpcode::kLoopExitValue:
          case IrOpcode::kLoopExitEffect:
            unmarked_exit = (node->InputAt(1)->InputAt(1) != loop_node);
            break;
          default:
            unmarked_exit = (use->opcode() != IrOpcode::kTerminate);
        }
        if (unmarked_exit) {
          if (v8_flags.trace_turbo_loop) {
            PrintF(
                "Cannot peel loop %i. Loop exit without explicit mark: Node %i "
                "(%s) is inside loop, but its use %i (%s) is outside.\n",
                loop_node->id(), node->id(), node->op()->mnemonic(), use->id(),
                use->op()->mnemonic());
          }
          return false;
        }
      }
    }
  }
  return true;
}

Node* LoopTree::HeaderNode(const Loop* loop) {
  Node* first = *HeaderNodes(loop).begin();
  if (first->opcode() == IrOpcode::kLoop) return first;
  DCHECK(IrOpcode::IsPhiOpcode(first->opcode()));
  Node* header = NodeProperties::GetControlInput(first);
  DCHECK_EQ(IrOpcode::kLoop, header->opcode());
  return header;
}

Node* NodeCopier::map(Node* node, uint32_t copy_index) {
  DCHECK_LT(copy_index, copy_count_);
  if (node_map_.Get(node) == 0) return node;
  return copies_->at(node_map_.Get(node) + copy_index);
}

void NodeCopier::Insert(Node* original, const NodeVector& new_copies) {
  DCHECK_EQ(new_copies.size(), copy_count_);
  node_map_.Set(original, copies_->size() + 1);
  copies_->push_back(original);
  copies_->insert(copies_->end(), new_copies.begin(), new_copies.end());
}

void NodeCopier::Insert(Node* original, Node* copy) {
  DCHECK_EQ(copy_count_, 1);
  node_map_.Set(original, copies_->size() + 1);
  copies_->push_back(original);
  copies_->push_back(copy);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
