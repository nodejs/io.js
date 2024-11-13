// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SCHEDULER_H_
#define V8_COMPILER_SCHEDULER_H_

#include <optional>

#include "src/base/flags.h"
#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class ProfileDataFromFile;
class TickCounter;

namespace compiler {

// Forward declarations.
class CFGBuilder;
class ControlEquivalence;
class Graph;
class SpecialRPONumberer;

// Computes a schedule from a graph, placing nodes into basic blocks and
// ordering the basic blocks in the special RPO order.
class V8_EXPORT_PRIVATE Scheduler {
 public:
  // Flags that control the mode of operation.
  enum Flag { kNoFlags = 0u, kSplitNodes = 1u << 1, kTempSchedule = 1u << 2 };
  using Flags = base::Flags<Flag>;

  // The complete scheduling algorithm. Creates a new schedule and places all
  // nodes from the graph into it.
  static Schedule* ComputeSchedule(Zone* temp_zone, Graph* graph, Flags flags,
                                   TickCounter* tick_counter,
                                   const ProfileDataFromFile* profile_data);

  // Compute the RPO of blocks in an existing schedule.
  static BasicBlockVector* ComputeSpecialRPO(Zone* zone, Schedule* schedule);

  // Computes the dominator tree on an existing schedule that has RPO computed.
  static void GenerateDominatorTree(Schedule* schedule);

  const ProfileDataFromFile* profile_data() const { return profile_data_; }

 private:
  // Placement of a node changes during scheduling. The placement state
  // transitions over time while the scheduler is choosing a position:
  //
  //                   +---------------------+-----+----> kFixed
  //                  /                     /     /
  //    kUnknown ----+------> kCoupled ----+     /
  //                  \                         /
  //                   +----> kSchedulable ----+--------> kScheduled
  //
  // 1) InitializePlacement(): kUnknown -> kCoupled|kSchedulable|kFixed
  // 2) UpdatePlacement(): kCoupled|kSchedulable -> kFixed|kScheduled
  //
  // We maintain the invariant that all nodes that are not reachable
  // from the end have kUnknown placement. After the PrepareUses phase runs,
  // also the opposite is true - all nodes with kUnknown placement are not
  // reachable from the end.
  enum Placement { kUnknown, kSchedulable, kFixed, kCoupled, kScheduled };

  // Implements a two-dimensional map: (int, int) -> BasicBlock*.
  using CommonDominatorCache = ZoneMap<int, ZoneMap<int, BasicBlock*>*>;

  // Per-node data tracked during scheduling.
  struct SchedulerData {
    BasicBlock* minimum_block_;  // Minimum legal RPO placement.
    int unscheduled_count_;      // Number of unscheduled uses of this node.
    Placement placement_;        // Whether the node is fixed, schedulable,
                                 // coupled to another node, or not yet known.
  };

  Zone* zone_;
  Graph* graph_;
  Schedule* schedule_;
  Flags flags_;
  ZoneVector<NodeVector*>
      scheduled_nodes_;                  // Per-block list of nodes in reverse.
  NodeVector schedule_root_nodes_;       // Fixed root nodes seed the worklist.
  ZoneQueue<Node*> schedule_queue_;      // Worklist of schedulable nodes.
  ZoneVector<SchedulerData> node_data_;  // Per-node data for all nodes.
  CFGBuilder* control_flow_builder_;     // Builds basic blocks for controls.
  SpecialRPONumberer* special_rpo_;      // Special RPO numbering of blocks.
  ControlEquivalence* equivalence_;      // Control dependence equivalence.
  TickCounter* const tick_counter_;
  const ProfileDataFromFile* profile_data_;
  CommonDominatorCache common_dominator_cache_;

  Scheduler(Zone* zone, Graph* graph, Schedule* schedule, Flags flags,
            size_t node_count_hint_, TickCounter* tick_counter,
            const ProfileDataFromFile* profile_data);

  inline SchedulerData DefaultSchedulerData();
  inline SchedulerData* GetData(Node* node);

  Placement GetPlacement(Node* node);
  Placement InitializePlacement(Node* node);
  void UpdatePlacement(Node* node, Placement placement);
  bool IsLive(Node* node);

  // If the node is coupled, returns the coupled control edge index.
  inline std::optional<int> GetCoupledControlEdge(Node* node);
  void IncrementUnscheduledUseCount(Node* node, Node* from);
  void DecrementUnscheduledUseCount(Node* node, Node* from);

  static void PropagateImmediateDominators(BasicBlock* block);

  // Uses {common_dominator_cache_} to speed up repeated calls.
  BasicBlock* GetCommonDominator(BasicBlock* b1, BasicBlock* b2);
  // Returns the common dominator of {b1} and {b2} if it can be found in
  // {common_dominator_cache_}, or nullptr otherwise.
  // Not meant to be called directly, only from {GetCommonDominator}.
  BasicBlock* GetCommonDominatorIfCached(BasicBlock* b1, BasicBlock* b2);

  // Phase 1: Build control-flow graph.
  friend class CFGBuilder;
  void BuildCFG();

  // Phase 2: Compute special RPO and dominator tree.
  friend class SpecialRPONumberer;
  void ComputeSpecialRPONumbering();
  void GenerateDominatorTree();

  // Phase 3: Prepare use counts for nodes.
  friend class PrepareUsesVisitor;
  void PrepareUses();

  // Phase 4: Schedule nodes early.
  friend class ScheduleEarlyNodeVisitor;
  void ScheduleEarly();

  // Phase 5: Schedule nodes late.
  friend class ScheduleLateNodeVisitor;
  void ScheduleLate();

  // Phase 6: Seal the final schedule.
  void SealFinalSchedule();

  void FuseFloatingControl(BasicBlock* block, Node* node);
  void MovePlannedNodes(BasicBlock* from, BasicBlock* to);
};


DEFINE_OPERATORS_FOR_FLAGS(Scheduler::Flags)

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SCHEDULER_H_
