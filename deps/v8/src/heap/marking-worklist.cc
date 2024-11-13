// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/marking-worklist.h"

#include <algorithm>
#include <cstddef>
#include <map>

#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/cppgc-js/cpp-marking-state.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/map.h"
#include "src/objects/objects-definitions.h"

namespace v8 {
namespace internal {

void MarkingWorklists::Clear() {
  shared_.Clear();
  on_hold_.Clear();
  other_.Clear();
  for (auto& cw : context_worklists_) {
    cw.worklist->Clear();
  }
  ReleaseContextWorklists();
}

void MarkingWorklists::Print() {
  PrintWorklist("shared", &shared_);
  PrintWorklist("on_hold", &on_hold_);
}

void MarkingWorklists::CreateContextWorklists(
    const std::vector<Address>& contexts) {
  DCHECK(context_worklists_.empty());
  if (contexts.empty()) return;

  context_worklists_.reserve(contexts.size());
  for (Address context : contexts) {
    context_worklists_.push_back(
        {context, std::make_unique<MarkingWorklist>()});
  }
}

void MarkingWorklists::ReleaseContextWorklists() { context_worklists_.clear(); }

void MarkingWorklists::PrintWorklist(const char* worklist_name,
                                     MarkingWorklist* worklist) {
#ifdef DEBUG
  std::map<InstanceType, int> count;
  int total_count = 0;
  worklist->Iterate([&count, &total_count](Tagged<HeapObject> obj) {
    ++total_count;
    count[obj->map()->instance_type()]++;
  });
  std::vector<std::pair<int, InstanceType>> rank;
  rank.reserve(count.size());
  for (const auto& i : count) {
    rank.emplace_back(i.second, i.first);
  }
  std::map<InstanceType, std::string> instance_type_name;
#define INSTANCE_TYPE_NAME(name) instance_type_name[name] = #name;
  INSTANCE_TYPE_LIST(INSTANCE_TYPE_NAME)
#undef INSTANCE_TYPE_NAME
  std::sort(rank.begin(), rank.end(),
            std::greater<std::pair<int, InstanceType>>());
  PrintF("Worklist %s: %d\n", worklist_name, total_count);
  for (auto i : rank) {
    PrintF("  [%s]: %d\n", instance_type_name[i.second].c_str(), i.first);
  }
#endif
}

constexpr Address MarkingWorklists::Local::kSharedContext;
constexpr Address MarkingWorklists::Local::kOtherContext;
constexpr std::nullptr_t MarkingWorklists::Local::kNoCppMarkingState;

MarkingWorklists::Local::Local(
    MarkingWorklists* global,
    std::unique_ptr<CppMarkingState> cpp_marking_state)
    : active_(&shared_),
      shared_(*global->shared()),
      on_hold_(*global->on_hold()),
      active_context_(kSharedContext),
      is_per_context_mode_(!global->context_worklists().empty()),
      other_(*global->other()),
      cpp_marking_state_(std::move(cpp_marking_state)) {
  if (is_per_context_mode_) {
    context_worklists_.reserve(global->context_worklists().size());
    int index = 0;
    for (auto& cw : global->context_worklists()) {
      context_worklists_.emplace_back(*cw.worklist);
      worklist_by_context_.Set(cw.context, index);
      index++;
    }
  }
}

void MarkingWorklists::Local::Publish() {
  shared_.Publish();
  on_hold_.Publish();
  other_.Publish();
  if (is_per_context_mode_) {
    for (auto* entry = worklist_by_context_.Start(); entry != nullptr;
         entry = worklist_by_context_.Next(entry)) {
      context_worklists_[entry->value].Publish();
    }
  }
  PublishCppHeapObjects();
}

bool MarkingWorklists::Local::IsEmpty() {
  // This function checks the on_hold_ worklist, so it works only for the main
  // thread.
  if (!active_->IsLocalEmpty() || !on_hold_.IsLocalEmpty() ||
      !active_->IsGlobalEmpty() || !on_hold_.IsGlobalEmpty()) {
    return false;
  }
  if (!is_per_context_mode_) {
    return true;
  }
  if (!shared_.IsLocalEmpty() || !other_.IsLocalEmpty() ||
      !shared_.IsGlobalEmpty() || !other_.IsGlobalEmpty()) {
    return false;
  }
  for (auto* entry = worklist_by_context_.Start(); entry != nullptr;
       entry = worklist_by_context_.Next(entry)) {
    auto& worklist = context_worklists_[entry->value];
    if (entry->key != active_context_ &&
        !(worklist.IsLocalEmpty() && worklist.IsGlobalEmpty())) {
      SwitchToContextImpl(entry->key, &worklist);
      return false;
    }
  }
  return true;
}

bool MarkingWorklists::Local::IsWrapperEmpty() const {
  return !cpp_marking_state_ || cpp_marking_state_->IsLocalEmpty();
}

void MarkingWorklists::Local::ShareWork() {
  if (!active_->IsLocalEmpty() && active_->IsGlobalEmpty()) {
    active_->Publish();
  }
  if (is_per_context_mode_ && active_context_ != kSharedContext) {
    if (!shared_.IsLocalEmpty() && shared_.IsGlobalEmpty()) {
      shared_.Publish();
    }
  }
}

void MarkingWorklists::Local::PublishWork() {
  DCHECK(!is_per_context_mode_);
  shared_.Publish();
}

void MarkingWorklists::Local::MergeOnHold() { shared_.Merge(on_hold_); }

bool MarkingWorklists::Local::PopContext(Tagged<HeapObject>* object) {
  DCHECK(is_per_context_mode_);
  // As an optimization we first check only the local segments to avoid locks.
  for (auto* entry = worklist_by_context_.Start(); entry != nullptr;
       entry = worklist_by_context_.Next(entry)) {
    auto& worklist = context_worklists_[entry->value];
    if (entry->key != active_context_ && !worklist.IsLocalEmpty()) {
      SwitchToContextImpl(entry->key, &worklist);
      return active_->Pop(object);
    }
  }
  // All local segments are empty. Check global segments.
  for (auto* entry = worklist_by_context_.Start(); entry != nullptr;
       entry = worklist_by_context_.Next(entry)) {
    auto& worklist = context_worklists_[entry->value];
    if (entry->key != active_context_ && worklist.Pop(object)) {
      SwitchToContextImpl(entry->key, &worklist);
      return true;
    }
  }
  // All worklists are empty. Switch to the default shared worklist.
  SwitchToContext(kSharedContext);
  return false;
}

Address MarkingWorklists::Local::SwitchToContextSlow(Address context) {
  auto maybe_index = worklist_by_context_.Get(context);
  if (V8_UNLIKELY(maybe_index.IsNothing())) {
    // The context passed is not an actual context:
    // - Shared context that should use the explicit worklist.
    // - This context was created during marking and should use the other
    // bucket.
    if (context == kSharedContext) {
      SwitchToContextImpl(kSharedContext, &shared_);
    } else {
      SwitchToContextImpl(kOtherContext, &other_);
    }
  } else {
    SwitchToContextImpl(context, &(context_worklists_[maybe_index.FromJust()]));
  }
  return active_context_;
}

Address MarkingWorklists::Local::SwitchToSharedForTesting() {
  return SwitchToContext(kSharedContext);
}

}  // namespace internal
}  // namespace v8
