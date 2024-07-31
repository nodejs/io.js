// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_GRAPHCYCLES_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_GRAPHCYCLES_H_

// GraphCycles detects the introduction of a cycle into a directed
// graph that is being built up incrementally.
//
// Nodes are identified by small integers.  It is not possible to
// record multiple edges with the same (source, destination) pair;
// requests to add an edge where one already exists are silently
// ignored.
//
// It is also not possible to introduce a cycle; an attempt to insert
// an edge that would introduce a cycle fails and returns false.
//
// GraphCycles uses no internal locking; calls into it should be
// serialized externally.

// Performance considerations:
//   Works well on sparse graphs, poorly on dense graphs.
//   Extra information is maintained incrementally to detect cycles quickly.
//   InsertEdge() is very fast when the edge already exists, and reasonably fast
//   otherwise.
//   FindPath() is linear in the size of the graph.
// The current implementation uses O(|V|+|E|) space.

#include <cstdint>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

// Opaque identifier for a graph node.
struct GraphId {
  uint64_t handle;

  bool operator==(const GraphId& x) const { return handle == x.handle; }
  bool operator!=(const GraphId& x) const { return handle != x.handle; }
};

// Return an invalid graph id that will never be assigned by GraphCycles.
inline GraphId InvalidGraphId() {
  return GraphId{0};
}

class GraphCycles {
 public:
  GraphCycles();
  ~GraphCycles();

  // Return the id to use for ptr, assigning one if necessary.
  // Subsequent calls with the same ptr value will return the same id
  // until Remove().
  GraphId GetId(void* ptr);

  // Remove "ptr" from the graph.  Its corresponding node and all
  // edges to and from it are removed.
  void RemoveNode(void* ptr);

  // Return the pointer associated with id, or nullptr if id is not
  // currently in the graph.
  void* Ptr(GraphId id);

  // Attempt to insert an edge from source_node to dest_node.  If the
  // edge would introduce a cycle, return false without making any
  // changes. Otherwise add the edge and return true.
  bool InsertEdge(GraphId source_node, GraphId dest_node);

  // Remove any edge that exists from source_node to dest_node.
  void RemoveEdge(GraphId source_node, GraphId dest_node);

  // Return whether node exists in the graph.
  bool HasNode(GraphId node);

  // Return whether there is an edge directly from source_node to dest_node.
  bool HasEdge(GraphId source_node, GraphId dest_node) const;

  // Return whether dest_node is reachable from source_node
  // by following edges.
  bool IsReachable(GraphId source_node, GraphId dest_node) const;

  // Find a path from "source" to "dest".  If such a path exists,
  // place the nodes on the path in the array path[], and return
  // the number of nodes on the path.  If the path is longer than
  // max_path_len nodes, only the first max_path_len nodes are placed
  // in path[].  The client should compare the return value with
  // max_path_len" to see when this occurs.  If no path exists, return
  // 0.  Any valid path stored in path[] will start with "source" and
  // end with "dest".  There is no guarantee that the path is the
  // shortest, but no node will appear twice in the path, except the
  // source and destination node if they are identical; therefore, the
  // return value is at most one greater than the number of nodes in
  // the graph.
  int FindPath(GraphId source, GraphId dest, int max_path_len,
               GraphId path[]) const;

  // Update the stack trace recorded for id with the current stack
  // trace if the last time it was updated had a smaller priority
  // than the priority passed on this call.
  //
  // *get_stack_trace is called to get the stack trace.
  void UpdateStackTrace(GraphId id, int priority,
                        int (*get_stack_trace)(void**, int));

  // Set *ptr to the beginning of the array that holds the recorded
  // stack trace for id and return the depth of the stack trace.
  int GetStackTrace(GraphId id, void*** ptr);

  // Check internal invariants. Crashes on failure, returns true on success.
  // Expensive: should only be called from graphcycles_test.cc.
  bool CheckInvariants() const;

  // ----------------------------------------------------
  struct Rep;
 private:
  Rep *rep_;      // opaque representation
  GraphCycles(const GraphCycles&) = delete;
  GraphCycles& operator=(const GraphCycles&) = delete;
};

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif
