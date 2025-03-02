// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/allocation-tracker.h"

#include "src/api/api-inl.h"
#include "src/api/api.h"
#include "src/execution/frames-inl.h"
#include "src/handles/global-handles-inl.h"
#include "src/objects/objects-inl.h"
#include "src/profiler/heap-snapshot-generator-inl.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

AllocationTraceNode::AllocationTraceNode(
    AllocationTraceTree* tree, unsigned function_info_index)
    : tree_(tree),
      function_info_index_(function_info_index),
      total_size_(0),
      allocation_count_(0),
      id_(tree->next_node_id()) {
}


AllocationTraceNode::~AllocationTraceNode() {
  for (AllocationTraceNode* node : children_) delete node;
}


AllocationTraceNode* AllocationTraceNode::FindChild(
    unsigned function_info_index) {
  for (AllocationTraceNode* node : children_) {
    if (node->function_info_index() == function_info_index) return node;
  }
  return nullptr;
}


AllocationTraceNode* AllocationTraceNode::FindOrAddChild(
    unsigned function_info_index) {
  AllocationTraceNode* child = FindChild(function_info_index);
  if (child == nullptr) {
    child = new AllocationTraceNode(tree_, function_info_index);
    children_.push_back(child);
  }
  return child;
}


void AllocationTraceNode::AddAllocation(unsigned size) {
  total_size_ += size;
  ++allocation_count_;
}


void AllocationTraceNode::Print(int indent, AllocationTracker* tracker) {
  base::OS::Print("%10u %10u %*c", total_size_, allocation_count_, indent, ' ');
  if (tracker != nullptr) {
    AllocationTracker::FunctionInfo* info =
        tracker->function_info_list()[function_info_index_];
    base::OS::Print("%s #%u", info->name, id_);
  } else {
    base::OS::Print("%u #%u", function_info_index_, id_);
  }
  base::OS::Print("\n");
  indent += 2;
  for (AllocationTraceNode* node : children_) {
    node->Print(indent, tracker);
  }
}


AllocationTraceTree::AllocationTraceTree()
    : next_node_id_(1),
      root_(this, 0) {
}

AllocationTraceNode* AllocationTraceTree::AddPathFromEnd(
    base::Vector<const unsigned> path) {
  AllocationTraceNode* node = root();
  for (const unsigned* entry = path.begin() + path.length() - 1;
       entry != path.begin() - 1; --entry) {
    node = node->FindOrAddChild(*entry);
  }
  return node;
}

void AllocationTraceTree::Print(AllocationTracker* tracker) {
  base::OS::Print("[AllocationTraceTree:]\n");
  base::OS::Print("Total size | Allocation count | Function id | id\n");
  root()->Print(0, tracker);
}

AllocationTracker::FunctionInfo::FunctionInfo()
    : name(""),
      function_id(0),
      script_name(""),
      script_id(0),
      start_position(-1),
      line(-1),
      column(-1) {}

void AddressToTraceMap::AddRange(Address start, int size,
                                 unsigned trace_node_id) {
  Address end = start + size;
  RemoveRange(start, end);

  RangeStack new_range(start, trace_node_id);
  ranges_.insert(RangeMap::value_type(end, new_range));
}


unsigned AddressToTraceMap::GetTraceNodeId(Address addr) {
  RangeMap::const_iterator it = ranges_.upper_bound(addr);
  if (it == ranges_.end()) return 0;
  if (it->second.start <= addr) {
    return it->second.trace_node_id;
  }
  return 0;
}


void AddressToTraceMap::MoveObject(Address from, Address to, int size) {
  unsigned trace_node_id = GetTraceNodeId(from);
  if (trace_node_id == 0) return;
  RemoveRange(from, from + size);
  AddRange(to, size, trace_node_id);
}


void AddressToTraceMap::Clear() {
  ranges_.clear();
}


void AddressToTraceMap::Print() {
  PrintF("[AddressToTraceMap (%zu): \n", ranges_.size());
  for (RangeMap::iterator it = ranges_.begin(); it != ranges_.end(); ++it) {
    PrintF("[%p - %p] => %u\n", reinterpret_cast<void*>(it->second.start),
           reinterpret_cast<void*>(it->first), it->second.trace_node_id);
  }
  PrintF("]\n");
}


void AddressToTraceMap::RemoveRange(Address start, Address end) {
  RangeMap::iterator it = ranges_.upper_bound(start);
  if (it == ranges_.end()) return;

  RangeStack prev_range(0, 0);

  RangeMap::iterator to_remove_begin = it;
  if (it->second.start < start) {
    prev_range = it->second;
  }
  do {
    if (it->first > end) {
      if (it->second.start < end) {
        it->second.start = end;
      }
      break;
    }
    ++it;
  } while (it != ranges_.end());

  ranges_.erase(to_remove_begin, it);

  if (prev_range.start != 0) {
    ranges_.insert(RangeMap::value_type(start, prev_range));
  }
}

AllocationTracker::AllocationTracker(HeapObjectsMap* ids, StringsStorage* names)
    : ids_(ids),
      names_(names),
      id_to_function_info_index_(),
      info_index_for_other_state_(0) {
  FunctionInfo* info = new FunctionInfo();
  info->name = "(root)";
  function_info_list_.push_back(info);
}

AllocationTracker::~AllocationTracker() {
  for (FunctionInfo* info : function_info_list_) delete info;
}

void AllocationTracker::AllocationEvent(Address addr, int size) {
  DisallowGarbageCollection no_gc;
  Heap* heap = ids_->heap();

  // Mark the new block as FreeSpace to make sure the heap is iterable
  // while we are capturing stack trace.
  heap->CreateFillerObjectAt(addr, size);

  Isolate* isolate = Isolate::FromHeap(heap);
  int length = 0;
  JavaScriptStackFrameIterator it(isolate);
  while (!it.done() && length < kMaxAllocationTraceLength) {
    JavaScriptFrame* frame = it.frame();
    Tagged<SharedFunctionInfo> shared = frame->function()->shared();
    SnapshotObjectId id =
        ids_->FindOrAddEntry(shared.address(), shared->Size(),
                             HeapObjectsMap::MarkEntryAccessed::kNo);
    allocation_trace_buffer_[length++] = AddFunctionInfo(shared, id, isolate);
    it.Advance();
  }
  if (length == 0) {
    unsigned index = functionInfoIndexForVMState(isolate->current_vm_state());
    if (index != 0) {
      allocation_trace_buffer_[length++] = index;
    }
  }
  AllocationTraceNode* top_node = trace_tree_.AddPathFromEnd(
      base::Vector<unsigned>(allocation_trace_buffer_, length));
  top_node->AddAllocation(size);

  address_to_trace_.AddRange(addr, size, top_node->id());
}


static uint32_t SnapshotObjectIdHash(SnapshotObjectId id) {
  return ComputeUnseededHash(static_cast<uint32_t>(id));
}

AllocationTracker::ScriptData::ScriptData(Tagged<Script> script,
                                          Isolate* isolate,
                                          AllocationTracker* tracker)
    : script_id_(script->id()),
      line_ends_(Script::GetLineEnds(isolate, direct_handle(script, isolate))),
      tracker_(tracker) {
  DirectHandle<Script> script_direct_handle(script, isolate);
  auto local_script = ToApiHandle<debug::Script>(script_direct_handle);
  script_.Reset(local_script->GetIsolate(), local_script);
  script_.SetWeak(this, &HandleWeakScript, v8::WeakCallbackType::kParameter);
}

AllocationTracker::ScriptData::~ScriptData() {
  if (!script_.IsEmpty()) {
    script_.ClearWeak();
  }
}

void AllocationTracker::ScriptData::HandleWeakScript(
    const v8::WeakCallbackInfo<ScriptData>& data) {
  ScriptData* script_data = reinterpret_cast<ScriptData*>(data.GetParameter());
  script_data->script_.ClearWeak();
  script_data->script_.Reset();
  script_data->tracker_->scripts_data_map_.erase(script_data->script_id_);
}

String::LineEndsVector& AllocationTracker::GetOrCreateLineEnds(
    Tagged<Script> script, Isolate* isolate) {
  auto it = scripts_data_map_.find(script->id());
  if (it == scripts_data_map_.end()) {
    auto inserted =
        scripts_data_map_.try_emplace(script->id(), script, isolate, this);
    CHECK(inserted.second);
    return inserted.first->second.line_ends();
  } else {
    return it->second.line_ends();
  }
}

Script::PositionInfo AllocationTracker::GetScriptPositionInfo(
    Tagged<Script> script, Isolate* isolate, int start) {
  Script::PositionInfo position_info;
  if (script->has_line_ends()) {
    script->GetPositionInfo(start, &position_info);
  } else {
    script->GetPositionInfoWithLineEnds(start, &position_info,
                                        GetOrCreateLineEnds(script, isolate));
  }
  return position_info;
}

unsigned AllocationTracker::AddFunctionInfo(Tagged<SharedFunctionInfo> shared,
                                            SnapshotObjectId id,
                                            Isolate* isolate) {
  base::HashMap::Entry* entry = id_to_function_info_index_.LookupOrInsert(
      reinterpret_cast<void*>(id), SnapshotObjectIdHash(id));
  if (entry->value == nullptr) {
    FunctionInfo* info = new FunctionInfo();
    info->name = names_->GetCopy(shared->DebugNameCStr().get());
    info->function_id = id;
    if (IsScript(shared->script())) {
      Tagged<Script> script = Cast<Script>(shared->script());
      if (IsName(script->name())) {
        Tagged<Name> name = Cast<Name>(script->name());
        info->script_name = names_->GetName(name);
      }
      info->script_id = script->id();
      info->start_position = shared->StartPosition();
      Script::PositionInfo position_info =
          GetScriptPositionInfo(script, isolate, info->start_position);
      info->line = position_info.line;
      info->column = position_info.column;
    }
    entry->value = reinterpret_cast<void*>(function_info_list_.size());
    function_info_list_.push_back(info);
  }
  return static_cast<unsigned>(reinterpret_cast<intptr_t>((entry->value)));
}

unsigned AllocationTracker::functionInfoIndexForVMState(StateTag state) {
  if (state != OTHER) return 0;
  if (info_index_for_other_state_ == 0) {
    FunctionInfo* info = new FunctionInfo();
    info->name = "(V8 API)";
    info_index_for_other_state_ =
        static_cast<unsigned>(function_info_list_.size());
    function_info_list_.push_back(info);
  }
  return info_index_for_other_state_;
}

}  // namespace internal
}  // namespace v8
