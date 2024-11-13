// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_ALLOCATION_TRACKER_H_
#define V8_PROFILER_ALLOCATION_TRACKER_H_

#include <map>
#include <unordered_map>
#include <vector>

#include "include/v8-persistent-handle.h"
#include "include/v8-profiler.h"
#include "include/v8-unwinder.h"
#include "src/base/hashmap.h"
#include "src/base/vector.h"
#include "src/debug/debug-interface.h"
#include "src/handles/handles.h"
#include "src/objects/script.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {

// Forward declarations.
class AllocationTraceTree;
class AllocationTracker;
class HeapObjectsMap;
class SharedFunctionInfo;
class StringsStorage;

class AllocationTraceNode {
 public:
  AllocationTraceNode(AllocationTraceTree* tree,
                      unsigned function_info_index);
  ~AllocationTraceNode();
  AllocationTraceNode(const AllocationTraceNode&) = delete;
  AllocationTraceNode& operator=(const AllocationTraceNode&) = delete;
  AllocationTraceNode* FindChild(unsigned function_info_index);
  AllocationTraceNode* FindOrAddChild(unsigned function_info_index);
  void AddAllocation(unsigned size);

  unsigned function_info_index() const { return function_info_index_; }
  unsigned allocation_size() const { return total_size_; }
  unsigned allocation_count() const { return allocation_count_; }
  unsigned id() const { return id_; }
  const std::vector<AllocationTraceNode*>& children() const {
    return children_;
  }

  void Print(int indent, AllocationTracker* tracker);

 private:
  AllocationTraceTree* tree_;
  unsigned function_info_index_;
  unsigned total_size_;
  unsigned allocation_count_;
  unsigned id_;
  std::vector<AllocationTraceNode*> children_;
};


class AllocationTraceTree {
 public:
  AllocationTraceTree();
  ~AllocationTraceTree() = default;
  AllocationTraceTree(const AllocationTraceTree&) = delete;
  AllocationTraceTree& operator=(const AllocationTraceTree&) = delete;
  AllocationTraceNode* AddPathFromEnd(base::Vector<const unsigned> path);
  AllocationTraceNode* root() { return &root_; }
  unsigned next_node_id() { return next_node_id_++; }
  V8_EXPORT_PRIVATE void Print(AllocationTracker* tracker);

 private:
  unsigned next_node_id_;
  AllocationTraceNode root_;
};

class V8_EXPORT_PRIVATE AddressToTraceMap {
 public:
  void AddRange(Address addr, int size, unsigned node_id);
  unsigned GetTraceNodeId(Address addr);
  void MoveObject(Address from, Address to, int size);
  void Clear();
  size_t size() { return ranges_.size(); }
  void Print();

 private:
  struct RangeStack {
    RangeStack(Address start, unsigned node_id)
        : start(start), trace_node_id(node_id) {}
    Address start;
    unsigned trace_node_id;
  };
  // [start, end) -> trace
  using RangeMap = std::map<Address, RangeStack>;

  void RemoveRange(Address start, Address end);

  RangeMap ranges_;
};

class AllocationTracker {
 public:
  struct FunctionInfo {
    FunctionInfo();
    const char* name;
    SnapshotObjectId function_id;
    const char* script_name;
    int script_id;
    int start_position;
    int line;
    int column;
  };

  AllocationTracker(HeapObjectsMap* ids, StringsStorage* names);
  ~AllocationTracker();
  AllocationTracker(const AllocationTracker&) = delete;
  AllocationTracker& operator=(const AllocationTracker&) = delete;

  void AllocationEvent(Address addr, int size);

  AllocationTraceTree* trace_tree() { return &trace_tree_; }
  const std::vector<FunctionInfo*>& function_info_list() const {
    return function_info_list_;
  }
  AddressToTraceMap* address_to_trace() { return &address_to_trace_; }

 private:
  unsigned AddFunctionInfo(Tagged<SharedFunctionInfo> info, SnapshotObjectId id,
                           Isolate* isolate);
  String::LineEndsVector& GetOrCreateLineEnds(Tagged<Script> script,
                                              Isolate* isolate);
  Script::PositionInfo GetScriptPositionInfo(Tagged<Script> script,
                                             Isolate* isolate, int start);
  unsigned functionInfoIndexForVMState(StateTag state);

  static const int kMaxAllocationTraceLength = 64;
  HeapObjectsMap* ids_;
  StringsStorage* names_;
  AllocationTraceTree trace_tree_;
  unsigned allocation_trace_buffer_[kMaxAllocationTraceLength];
  std::vector<FunctionInfo*> function_info_list_;
  base::HashMap id_to_function_info_index_;
  unsigned info_index_for_other_state_;
  AddressToTraceMap address_to_trace_;
  using ScriptId = int;
  class ScriptData {
   public:
    ScriptData(Tagged<Script>, Isolate*, AllocationTracker*);
    ~ScriptData();
    String::LineEndsVector& line_ends() { return line_ends_; }

   private:
    static void HandleWeakScript(const v8::WeakCallbackInfo<ScriptData>&);
    Global<debug::Script> script_;
    ScriptId script_id_;
    String::LineEndsVector line_ends_;
    AllocationTracker* tracker_;
  };
  using ScriptsDataMap = std::unordered_map<ScriptId, ScriptData>;
  ScriptsDataMap scripts_data_map_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_ALLOCATION_TRACKER_H_
