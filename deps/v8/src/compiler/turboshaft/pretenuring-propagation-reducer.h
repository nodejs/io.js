// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_PRETENURING_PROPAGATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_PRETENURING_PROPAGATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/reducer-traits.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/zone/zone-allocator.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler::turboshaft {

// This reducer propagates pretenuring (= allocations of Old objects rather than
// Young objects) throughout the graph: if a young allocation is stored in an
// old allocation, then we'll make it old instead. The idea being that 1) if an
// object is stored in an old object, it makes sense for it for be considered
// old, and 2) this reduces the size of the remembered sets.
// For instance, if we have:
//
//     a = Allocate(old)
//     b = Allocate(young)
//     c = Allocate(young)
//     b.x = c
//     a.x = b
//
// Then we'll make `b` and `c` allocate Old objects directly, because they'll be
// stored to old objects (transitively for `c`, but it still counts).
// On the other hand, if we have:
//
//     a = Allocate(old)
//     b = Allocate(young)
//     c = Allocate(young)
//     a.x = c
//     b.c = a
//
// Then we'll allocate `c` Old as well (because it is stored in an old pointer),
// but `b` will stay young, since it's not stored in an Old object (it contains
// a pointer to an old object, but that's probably not a good reason to make it
// old).
//
//
// Implementation
//
// In a first phase, we iterate the input graph and create a directed graph
// (called `store_graph_`) where each node points to the nodes stored in it (so,
// an edge between `a` and `b` means that `b` is stored in `a`). We also collect
// all of the old allocations in a separate list (`old_allocs_`). The
// `store_graph_` of the first example above will thus be:
//
//    a -----> b -----> c
//
// And the graph of the second example will be:
//
//   a -----> c        b -----> a
//
// (it contains two unconnected subgraphs)
//
// Then, in a second phase, we iterate the old allocations (`old_allocs_`), and
// for each one, we do a DFS in `store_graph_`, marking all of the nodes we
// encounter as old, and stopping on old nodes (which 1- prevents infinite loops
// easily, and 2- is sound because all of the initially-old pointers are roots
// of this phase). On the 2 examples above, `a` will be the old entry in
// `old_allocs_`, so in both cases will do a single DFS starting from `a`. In
// the 1st case, it's easy to see that this DFS will encounter `b` and `c`
// (which will thus both become Old), while in the 2nd case, this DFS will only
// reach `c` (which will thus become Old, while `b` won't be changed).
//
// To be more precise, we also record Phi inputs in `store_graph_`, so that if
// we have something like:
//
//     a = Allocate(old)
//     b = Allocate(young)
//     c = Allocate(young)
//     p = Phi(b, c)
//     a.x = p
//
// Then we oldify both `b` and `c`. In this case, `store_graph_` would be
//
//                --------> b
//               /
//      a ----> p
//               \
//                --------> c
//
// Which means that in the second phase, we'll start a DFS on `a` (the only old
// allocation), move to `p` (the only node reachable from `a`), and the oldify
// `b` and `c` (which are reachable from `p`).
//
//
// Limitation: when a Phi of old allocations is used as the left-hand side of a
// Store where the value being stored is a young allocation, we don't oldify the
// young allocation. For instance, we won't oldify `a` in this example:
//
//     a = Allocate(young)
//     b = Allocate(old)
//     c = Allocate(old)
//     p = Phi(b, c)
//     p.x = a
//
// The reason being that the store_graph sturcture isn't well suited for this,
// since an edge Phi->Node can mean either that Node is stored (via a StoreOp)
// in Phi, or that Node is an input of Phi. The `store_graph_` for the example
// above will thus look like:
//
//      ------> b
//     /
//    p ------> a
//     \
//      ------> c
//
// In order to oldify `a`, we would need to register `p` in `old_allocs_`,
// except that we should only do this when `p` is actually old, and we discover
// that only in the second phase.
// Consider for instance this more complex example:
//
//     a = Allocate(old)
//     b = Allocate(young)
//     c = Allocate(young)
//     d = Allocate(young)
//     a.x = b
//     a.y = c
//     p = Phi(b, c)
//     p.x = d
//
// The graph will be:
//
//      -----> b <-----
//     /               \
//    a                 p -----> d
//     \               /
//      -----> c <-----
//
// And the only entry in `old_allocs_` will be `a`. During the DFS from `a`,
// allocations `b` and `c` will be oldified. At this point, `p` will point to
// edges to 2 old (`b` and `c`) and 1 young (`d`) nodes.
// We could look at all Phis in `store_graph_` and consider one by one for being
// roots of an oldifying DFS: if all of the inputs of a phi `p` (in the sense
// OpIndex inputs in the input_graph) are Old, then start an oldifying DFS from
// `p`. However, the worst case complexity would be something like O(n^2) where
// `n` is the number of Phis in the graph (since we could end up checking all
// Phis but only finding a single one that is old, but the DFS could make a
// single other phi old, thus repeating the process). This complexity could be
// made linear by maintaining additional datastructures on the side, but there
// isn't much evidence that this optimization would be often useful in practice.

class PretenuringPropagationAnalyzer {
 public:
  PretenuringPropagationAnalyzer(Zone* phase_zone, Graph& mutable_input_graph)
      : zone_(phase_zone),
        input_graph_(mutable_input_graph),
        old_allocs_(phase_zone),
        store_graph_(phase_zone),
        old_phis_(phase_zone),
        queue_(phase_zone) {}

  void Run();

 private:
  void ProcessStore(const StoreOp& store);
  void ProcessPhi(const PhiOp& phi);
  void ProcessAllocate(const AllocateOp& allocate);

  bool PushContainedValues(OpIndex base);
  void OldifySubgraph(OpIndex old_alloc);

  void BuildStoreInputGraph();
  void PropagateAllocationTypes();

  ZoneVector<OpIndex>* FindOrCreate(OpIndex idx) {
    auto it = store_graph_.find(idx);
    if (it != store_graph_.end()) return it->second;
    return Create(idx);
  }

  ZoneVector<OpIndex>* Create(OpIndex idx) {
    DCHECK_EQ(store_graph_.count(idx), 0);
    ZoneVector<OpIndex>* stored_items = zone_->New<ZoneVector<OpIndex>>(zone_);
    store_graph_.insert({idx, stored_items});
    return stored_items;
  }

  ZoneVector<OpIndex>* TryFind(OpIndex idx) {
    auto it = store_graph_.find(idx);
    if (it != store_graph_.end()) return it->second;
    return nullptr;
  }

  Zone* zone_;
  Graph& input_graph_;
  ZoneVector<OpIndex> old_allocs_;

  // (see main comment at the begining of this file for the role of
  // `store_graph_`)
  // `store_graph_` contains mapping from OpIndex to vector<OpIndex>. If for an
  // entry `a` it contains a vector `v`, it means that `a` has edges to all of
  // the values in `v`.
  ZoneAbslFlatHashMap<OpIndex, ZoneVector<OpIndex>*> store_graph_;

  // AllocateOp have an AllocationType field, which is set to kOld once they've
  // been visited, thus ensuring that recursion ends. However, PhiOp don't have
  // such a field. Thus, once we've visited a Phi, we store it in {old_phis_} to
  // prevent revisiting it.
  ZoneAbslFlatHashSet<OpIndex> old_phis_;

  // Used in the final phase to do DFS in the graph from each old store. It
  // could be a local variable, but we instead use an instance variable to reuse
  // memory.
  ZoneVector<OpIndex> queue_;
};

// Forward delcaration
template <class Next>
class MemoryOptimizationReducer;

template <class Next>
class PretenuringPropagationReducer : public Next {
#if defined(__clang__)
  // PretenuringPropagationReducer should run before MemoryOptimizationReducer
  // (because once young allocations are marked for folding, they can't be
  // oldified anymore). We enforce this by making PretenuringPropagationReducer
  // run in the same phase as MemoryOptimizationReducer, but before.
  static_assert(next_contains_reducer<Next, MemoryOptimizationReducer>::value);
#endif

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(PretenuringPropagation)

  void Analyze() {
    PretenuringPropagationAnalyzer analyzer(Asm().phase_zone(),
                                            Asm().modifiable_input_graph());
    analyzer.Run();
    Next::Analyze();
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_PRETENURING_PROPAGATION_REDUCER_H_
