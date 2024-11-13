// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug.h"

#include <memory>
#include <optional>

#include "src/api/api-inl.h"
#include "src/base/platform/mutex.h"
#include "src/builtins/builtins.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/compiler.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/debug/debug-evaluate.h"
#include "src/debug/liveedit.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/protectors-inl.h"
#include "src/execution/v8threads.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/heap-inl.h"  // For NextDebuggingId.
#include "src/init/bootstrapper.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/logging/counters.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/api-callbacks-inl.h"
#include "src/objects/debug-objects-inl.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/slots.h"
#include "src/snapshot/embedded/embedded-data.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-objects-inl.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

class Debug::TemporaryObjectsTracker : public HeapObjectAllocationTracker {
 public:
  TemporaryObjectsTracker() = default;
  ~TemporaryObjectsTracker() override = default;
  TemporaryObjectsTracker(const TemporaryObjectsTracker&) = delete;
  TemporaryObjectsTracker& operator=(const TemporaryObjectsTracker&) = delete;

  void AllocationEvent(Address addr, int size) override {
    if (disabled) return;
    AddRegion(addr, addr + size);
  }

  void MoveEvent(Address from, Address to, int size) override {
    if (from == to) return;
    base::MutexGuard guard(&mutex_);
    if (RemoveFromRegions(from, from + size)) {
      // We had the object tracked as temporary, so we will track the
      // new location as temporary, too.
      AddRegion(to, to + size);
    } else {
      // The object we moved is a non-temporary, so the new location is also
      // non-temporary. Thus we remove everything we track there (because it
      // must have become dead).
      RemoveFromRegions(to, to + size);
    }
  }

  bool HasObject(Handle<HeapObject> obj) {
    if (IsJSObject(*obj) && Cast<JSObject>(obj)->GetEmbedderFieldCount()) {
      // Embedder may store any pointers using embedder fields and implements
      // non trivial logic, e.g. create wrappers lazily and store pointer to
      // native object inside embedder field. We should consider all objects
      // with embedder fields as non temporary.
      return false;
    }
    Address addr = obj->address();
    return HasRegionContainingObject(addr, addr + obj->Size());
  }

  bool disabled = false;

 private:
  bool HasRegionContainingObject(Address start, Address end) {
    // Check if there is a region that contains (overlaps) this object's space.
    auto it = FindOverlappingRegion(start, end, false);
    // If there is, we expect the region to contain the entire object.
    DCHECK_IMPLIES(it != regions_.end(),
                   it->second <= start && end <= it->first);
    return it != regions_.end();
  }

  // This function returns any one of the overlapping regions (there might be
  // multiple). If {include_adjacent} is true, it will also consider regions
  // that have no overlap but are directly connected.
  std::map<Address, Address>::iterator FindOverlappingRegion(
      Address start, Address end, bool include_adjacent) {
    // Region A = [start, end) overlaps with an existing region [existing_start,
    // existing_end) iff (start <= existing_end) && (existing_start <= end).
    // Since we index {regions_} by end address, we can find a candidate that
    // satisfies the first condition using lower_bound.
    if (include_adjacent) {
      auto it = regions_.lower_bound(start);
      if (it == regions_.end()) return regions_.end();
      if (it->second <= end) return it;
    } else {
      auto it = regions_.upper_bound(start);
      if (it == regions_.end()) return regions_.end();
      if (it->second < end) return it;
    }
    return regions_.end();
  }

  void AddRegion(Address start, Address end) {
    DCHECK_LT(start, end);

    // Region [start, end) can be combined with an existing region if they
    // overlap.
    while (true) {
      auto it = FindOverlappingRegion(start, end, true);
      // If there is no such region, we don't need to merge anything.
      if (it == regions_.end()) break;

      // Otherwise, we found an overlapping region. We remove the old one and
      // add the new region recursively (to handle cases where the new region
      // overlaps multiple existing ones).
      start = std::min(start, it->second);
      end = std::max(end, it->first);
      regions_.erase(it);
    }

    // Add the new (possibly combined) region.
    regions_.emplace(end, start);
  }

  bool RemoveFromRegions(Address start, Address end) {
    // Check if we have anything that overlaps with [start, end).
    auto it = FindOverlappingRegion(start, end, false);
    if (it == regions_.end()) return false;

    // We need to update all overlapping regions.
    for (; it != regions_.end();
         it = FindOverlappingRegion(start, end, false)) {
      Address existing_start = it->second;
      Address existing_end = it->first;
      // If we remove the region [start, end) from an existing region
      // [existing_start, existing_end), there can be at most 2 regions left:
      regions_.erase(it);
      // The one before {start} is: [existing_start, start)
      if (existing_start < start) AddRegion(existing_start, start);
      // And the one after {end} is: [end, existing_end)
      if (end < existing_end) AddRegion(end, existing_end);
    }
    return true;
  }

  // Tracking addresses is not enough, because a single allocation may combine
  // multiple objects due to allocation folding. We track both start and end
  // (exclusive) address of regions. We index by end address for faster lookup.
  // Map: end address => start address
  std::map<Address, Address> regions_;
  base::Mutex mutex_;
};

Debug::Debug(Isolate* isolate)
    : is_active_(false),
      hook_on_function_call_(false),
      is_suppressed_(false),
      break_disabled_(false),
      break_points_active_(true),
      break_on_caught_exception_(false),
      break_on_uncaught_exception_(false),
      side_effect_check_failed_(false),
      debug_infos_(isolate),
      isolate_(isolate) {
  ThreadInit();
}

Debug::~Debug() { DCHECK_NULL(debug_delegate_); }

BreakLocation BreakLocation::FromFrame(Handle<DebugInfo> debug_info,
                                       JavaScriptFrame* frame) {
  if (debug_info->CanBreakAtEntry()) {
    return BreakLocation(Debug::kBreakAtEntryPosition, DEBUG_BREAK_AT_ENTRY);
  }
  auto summary = FrameSummary::GetTop(frame).AsJavaScript();
  int offset = summary.code_offset();
  DirectHandle<AbstractCode> abstract_code = summary.abstract_code();
  BreakIterator it(debug_info);
  it.SkipTo(BreakIndexFromCodeOffset(debug_info, abstract_code, offset));
  return it.GetBreakLocation();
}

bool BreakLocation::IsPausedInJsFunctionEntry(JavaScriptFrame* frame) {
  auto summary = FrameSummary::GetTop(frame);
  return summary.code_offset() == kFunctionEntryBytecodeOffset;
}

MaybeHandle<FixedArray> Debug::CheckBreakPointsForLocations(
    Handle<DebugInfo> debug_info, std::vector<BreakLocation>& break_locations,
    bool* has_break_points) {
  Handle<FixedArray> break_points_hit = isolate_->factory()->NewFixedArray(
      debug_info->GetBreakPointCount(isolate_));
  int break_points_hit_count = 0;
  bool has_break_points_at_all = false;
  for (size_t i = 0; i < break_locations.size(); i++) {
    bool location_has_break_points;
    MaybeHandle<FixedArray> check_result = CheckBreakPoints(
        debug_info, &break_locations[i], &location_has_break_points);
    has_break_points_at_all |= location_has_break_points;
    if (!check_result.is_null()) {
      DirectHandle<FixedArray> break_points_current_hit =
          check_result.ToHandleChecked();
      int num_objects = break_points_current_hit->length();
      for (int j = 0; j < num_objects; ++j) {
        break_points_hit->set(break_points_hit_count++,
                              break_points_current_hit->get(j));
      }
    }
  }
  *has_break_points = has_break_points_at_all;
  if (break_points_hit_count == 0) return {};

  break_points_hit->RightTrim(isolate_, break_points_hit_count);
  return break_points_hit;
}

void BreakLocation::AllAtCurrentStatement(
    Handle<DebugInfo> debug_info, JavaScriptFrame* frame,
    std::vector<BreakLocation>* result_out) {
  DCHECK(!debug_info->CanBreakAtEntry());
  auto summary = FrameSummary::GetTop(frame).AsJavaScript();
  int offset = summary.code_offset();
  DirectHandle<AbstractCode> abstract_code = summary.abstract_code();
  PtrComprCageBase cage_base = GetPtrComprCageBase(*debug_info);
  if (IsCode(*abstract_code, cage_base)) offset = offset - 1;
  int statement_position;
  {
    BreakIterator it(debug_info);
    it.SkipTo(BreakIndexFromCodeOffset(debug_info, abstract_code, offset));
    statement_position = it.statement_position();
  }
  for (BreakIterator it(debug_info); !it.Done(); it.Next()) {
    if (it.statement_position() == statement_position) {
      result_out->push_back(it.GetBreakLocation());
    }
  }
}

Tagged<JSGeneratorObject> BreakLocation::GetGeneratorObjectForSuspendedFrame(
    JavaScriptFrame* frame) const {
  DCHECK(IsSuspend());
  DCHECK_GE(generator_obj_reg_index_, 0);

  Tagged<Object> generator_obj =
      UnoptimizedFrame::cast(frame)->ReadInterpreterRegister(
          generator_obj_reg_index_);

  return Cast<JSGeneratorObject>(generator_obj);
}

int BreakLocation::BreakIndexFromCodeOffset(
    Handle<DebugInfo> debug_info, DirectHandle<AbstractCode> abstract_code,
    int offset) {
  // Run through all break points to locate the one closest to the address.
  int closest_break = 0;
  int distance = kMaxInt;
  DCHECK(kFunctionEntryBytecodeOffset <= offset &&
         offset < abstract_code->Size());
  for (BreakIterator it(debug_info); !it.Done(); it.Next()) {
    // Check if this break point is closer that what was previously found.
    if (it.code_offset() <= offset && offset - it.code_offset() < distance) {
      closest_break = it.break_index();
      distance = offset - it.code_offset();
      // Check whether we can't get any closer.
      if (distance == 0) break;
    }
  }
  return closest_break;
}

bool BreakLocation::HasBreakPoint(Isolate* isolate,
                                  Handle<DebugInfo> debug_info) const {
  // First check whether there is a break point with the same source position.
  if (!debug_info->HasBreakInfo() ||
      !debug_info->HasBreakPoint(isolate, position_)) {
    return false;
  }
  if (debug_info->CanBreakAtEntry()) {
    DCHECK_EQ(Debug::kBreakAtEntryPosition, position_);
    return debug_info->BreakAtEntry();
  } else {
    // Then check whether a break point at that source position would have
    // the same code offset. Otherwise it's just a break location that we can
    // step to, but not actually a location where we can put a break point.
    DCHECK(IsBytecodeArray(*abstract_code_, isolate));
    BreakIterator it(debug_info);
    it.SkipToPosition(position_);
    return it.code_offset() == code_offset_;
  }
}

debug::BreakLocationType BreakLocation::type() const {
  switch (type_) {
    case DEBUGGER_STATEMENT:
      return debug::kDebuggerStatementBreakLocation;
    case DEBUG_BREAK_SLOT_AT_CALL:
      return debug::kCallBreakLocation;
    case DEBUG_BREAK_SLOT_AT_RETURN:
      return debug::kReturnBreakLocation;

    // Externally, suspend breaks should look like normal breaks.
    case DEBUG_BREAK_SLOT_AT_SUSPEND:
    default:
      return debug::kCommonBreakLocation;
  }
}

BreakIterator::BreakIterator(Handle<DebugInfo> debug_info)
    : debug_info_(debug_info),
      break_index_(-1),
      source_position_iterator_(
          debug_info->DebugBytecodeArray(isolate())->SourcePositionTable()) {
  position_ = debug_info->shared()->StartPosition();
  statement_position_ = position_;
  // There is at least one break location.
  DCHECK(!Done());
  Next();
}

int BreakIterator::BreakIndexFromPosition(int source_position) {
  for (; !Done(); Next()) {
    if (GetDebugBreakType() == DEBUG_BREAK_SLOT_AT_SUSPEND) continue;
    if (source_position <= position()) {
      int first_break = break_index();
      for (; !Done(); Next()) {
        if (GetDebugBreakType() == DEBUG_BREAK_SLOT_AT_SUSPEND) continue;
        if (source_position == position()) return break_index();
      }
      return first_break;
    }
  }
  return break_index();
}

void BreakIterator::Next() {
  DisallowGarbageCollection no_gc;
  DCHECK(!Done());
  bool first = break_index_ == -1;
  while (!Done()) {
    if (!first) source_position_iterator_.Advance();
    first = false;
    if (Done()) return;
    position_ = source_position_iterator_.source_position().ScriptOffset();
    if (source_position_iterator_.is_statement()) {
      statement_position_ = position_;
    }
    DCHECK_LE(0, position_);
    DCHECK_LE(0, statement_position_);

    DebugBreakType type = GetDebugBreakType();
    if (type != NOT_DEBUG_BREAK) break;
  }
  break_index_++;
}

DebugBreakType BreakIterator::GetDebugBreakType() {
  Tagged<BytecodeArray> bytecode_array =
      debug_info_->OriginalBytecodeArray(isolate());
  interpreter::Bytecode bytecode =
      interpreter::Bytecodes::FromByte(bytecode_array->get(code_offset()));

  // Make sure we read the actual bytecode, not a prefix scaling bytecode.
  if (interpreter::Bytecodes::IsPrefixScalingBytecode(bytecode)) {
    bytecode = interpreter::Bytecodes::FromByte(
        bytecode_array->get(code_offset() + 1));
  }

  if (bytecode == interpreter::Bytecode::kDebugger) {
    return DEBUGGER_STATEMENT;
  } else if (bytecode == interpreter::Bytecode::kReturn) {
    return DEBUG_BREAK_SLOT_AT_RETURN;
  } else if (bytecode == interpreter::Bytecode::kSuspendGenerator) {
    // SuspendGenerator should always only carry an expression position that
    // is used in stack trace construction, but should never be a breakable
    // position reported to the debugger front-end.
    DCHECK(!source_position_iterator_.is_statement());
    return DEBUG_BREAK_SLOT_AT_SUSPEND;
  } else if (interpreter::Bytecodes::IsCallOrConstruct(bytecode)) {
    return DEBUG_BREAK_SLOT_AT_CALL;
  } else if (source_position_iterator_.is_statement()) {
    return DEBUG_BREAK_SLOT;
  } else {
    return NOT_DEBUG_BREAK;
  }
}

void BreakIterator::SkipToPosition(int position) {
  BreakIterator it(debug_info_);
  SkipTo(it.BreakIndexFromPosition(position));
}

void BreakIterator::SetDebugBreak() {
  DCHECK(GetDebugBreakType() >= DEBUGGER_STATEMENT);
  HandleScope scope(isolate());
  Handle<BytecodeArray> bytecode_array(
      debug_info_->DebugBytecodeArray(isolate()), isolate());
  interpreter::BytecodeArrayIterator(bytecode_array, code_offset())
      .ApplyDebugBreak();
}

void BreakIterator::ClearDebugBreak() {
  DCHECK(GetDebugBreakType() >= DEBUGGER_STATEMENT);
  Tagged<BytecodeArray> bytecode_array =
      debug_info_->DebugBytecodeArray(isolate());
  Tagged<BytecodeArray> original =
      debug_info_->OriginalBytecodeArray(isolate());
  bytecode_array->set(code_offset(), original->get(code_offset()));
}

BreakLocation BreakIterator::GetBreakLocation() {
  Handle<AbstractCode> code(
      Cast<AbstractCode>(debug_info_->DebugBytecodeArray(isolate())),
      isolate());
  DebugBreakType type = GetDebugBreakType();
  int generator_object_reg_index = -1;
  int generator_suspend_id = -1;
  if (type == DEBUG_BREAK_SLOT_AT_SUSPEND) {
    // For suspend break, we'll need the generator object to be able to step
    // over the suspend as if it didn't return. We get the interpreter register
    // index that holds the generator object by reading it directly off the
    // bytecode array, and we'll read the actual generator object off the
    // interpreter stack frame in GetGeneratorObjectForSuspendedFrame.
    Tagged<BytecodeArray> bytecode_array =
        debug_info_->OriginalBytecodeArray(isolate());
    interpreter::BytecodeArrayIterator iterator(
        handle(bytecode_array, isolate()), code_offset());

    DCHECK_EQ(iterator.current_bytecode(),
              interpreter::Bytecode::kSuspendGenerator);
    interpreter::Register generator_obj_reg = iterator.GetRegisterOperand(0);
    generator_object_reg_index = generator_obj_reg.index();

    // Also memorize the suspend ID, to be able to decide whether
    // we are paused on the implicit initial yield later.
    generator_suspend_id = iterator.GetUnsignedImmediateOperand(3);
  }
  return BreakLocation(code, type, code_offset(), position_,
                       generator_object_reg_index, generator_suspend_id);
}

Isolate* BreakIterator::isolate() { return debug_info_->GetIsolate(); }

// Threading support.
void Debug::ThreadInit() {
  thread_local_.break_frame_id_ = StackFrameId::NO_ID;
  thread_local_.last_step_action_ = StepNone;
  thread_local_.last_statement_position_ = kNoSourcePosition;
  thread_local_.last_bytecode_offset_ = kFunctionEntryBytecodeOffset;
  thread_local_.last_frame_count_ = -1;
  thread_local_.fast_forward_to_return_ = false;
  thread_local_.ignore_step_into_function_ = Smi::zero();
  thread_local_.target_frame_count_ = -1;
  thread_local_.return_value_ = Smi::zero();
  thread_local_.last_breakpoint_id_ = 0;
  clear_restart_frame();
  clear_suspended_generator();
  base::Relaxed_Store(&thread_local_.current_debug_scope_,
                      static_cast<base::AtomicWord>(0));
  thread_local_.break_on_next_function_call_ = false;
  thread_local_.scheduled_break_on_next_function_call_ = false;
  UpdateHookOnFunctionCall();
  thread_local_.muted_function_ = Smi::zero();
  thread_local_.muted_position_ = -1;
}

char* Debug::ArchiveDebug(char* storage) {
  MemCopy(storage, reinterpret_cast<char*>(&thread_local_),
          ArchiveSpacePerThread());
  return storage + ArchiveSpacePerThread();
}

char* Debug::RestoreDebug(char* storage) {
  MemCopy(reinterpret_cast<char*>(&thread_local_), storage,
          ArchiveSpacePerThread());

  // Enter the isolate.
  v8::Isolate::Scope isolate_scope(reinterpret_cast<v8::Isolate*>(isolate_));
  // Enter the debugger.
  DebugScope debug_scope(this);

  // Clear any one-shot breakpoints that may have been set by the other
  // thread, and reapply breakpoints for this thread.
  ClearOneShot();

  if (thread_local_.last_step_action_ != StepNone) {
    int current_frame_count = CurrentFrameCount();
    int target_frame_count = thread_local_.target_frame_count_;
    DCHECK(current_frame_count >= target_frame_count);
    DebuggableStackFrameIterator frames_it(isolate_);
    while (current_frame_count > target_frame_count) {
      current_frame_count -= frames_it.FrameFunctionCount();
      frames_it.Advance();
    }
    DCHECK(current_frame_count == target_frame_count);
    // Set frame to what it was at Step break
    thread_local_.break_frame_id_ = frames_it.frame()->id();

    // Reset the previous step action for this thread.
    PrepareStep(thread_local_.last_step_action_);
  }

  return storage + ArchiveSpacePerThread();
}

int Debug::ArchiveSpacePerThread() { return sizeof(ThreadLocal); }

void Debug::Iterate(RootVisitor* v) { Iterate(v, &thread_local_); }

char* Debug::Iterate(RootVisitor* v, char* thread_storage) {
  ThreadLocal* thread_local_data =
      reinterpret_cast<ThreadLocal*>(thread_storage);
  Iterate(v, thread_local_data);
  return thread_storage + ArchiveSpacePerThread();
}

void Debug::Iterate(RootVisitor* v, ThreadLocal* thread_local_data) {
  v->VisitRootPointer(Root::kDebug, nullptr,
                      FullObjectSlot(&thread_local_data->return_value_));
  v->VisitRootPointer(Root::kDebug, nullptr,
                      FullObjectSlot(&thread_local_data->suspended_generator_));
  v->VisitRootPointer(
      Root::kDebug, nullptr,
      FullObjectSlot(&thread_local_data->ignore_step_into_function_));
  v->VisitRootPointer(Root::kDebug, nullptr,
                      FullObjectSlot(&thread_local_data->muted_function_));
}

void DebugInfoCollection::Insert(Tagged<SharedFunctionInfo> sfi,
                                 Tagged<DebugInfo> debug_info) {
  DisallowGarbageCollection no_gc;
  base::SharedMutexGuard<base::kExclusive> mutex_guard(
      isolate_->shared_function_info_access());

  DCHECK_EQ(sfi, debug_info->shared());
  DCHECK(!Contains(sfi));
  HandleLocation location =
      isolate_->global_handles()->Create(debug_info).location();
  list_.push_back(location);
  map_.emplace(sfi->unique_id(), location);
  DCHECK(Contains(sfi));
  DCHECK_EQ(list_.size(), map_.size());
}

bool DebugInfoCollection::Contains(Tagged<SharedFunctionInfo> sfi) const {
  auto it = map_.find(sfi->unique_id());
  if (it == map_.end()) return false;
  DCHECK_EQ(Cast<DebugInfo>(Tagged<Object>(*it->second))->shared(), sfi);
  return true;
}

std::optional<Tagged<DebugInfo>> DebugInfoCollection::Find(
    Tagged<SharedFunctionInfo> sfi) const {
  auto it = map_.find(sfi->unique_id());
  if (it == map_.end()) return {};
  Tagged<DebugInfo> di = Cast<DebugInfo>(Tagged<Object>(*it->second));
  DCHECK_EQ(di->shared(), sfi);
  return di;
}

void DebugInfoCollection::DeleteSlow(Tagged<SharedFunctionInfo> sfi) {
  DebugInfoCollection::Iterator it(this);
  for (; it.HasNext(); it.Advance()) {
    Tagged<DebugInfo> debug_info = it.Next();
    if (debug_info->shared() != sfi) continue;
    it.DeleteNext();
    return;
  }
  UNREACHABLE();
}

Tagged<DebugInfo> DebugInfoCollection::EntryAsDebugInfo(size_t index) const {
  DCHECK_LT(index, list_.size());
  return Cast<DebugInfo>(Tagged<Object>(*list_[index]));
}

void DebugInfoCollection::DeleteIndex(size_t index) {
  base::SharedMutexGuard<base::kExclusive> mutex_guard(
      isolate_->shared_function_info_access());

  Tagged<DebugInfo> debug_info = EntryAsDebugInfo(index);
  Tagged<SharedFunctionInfo> sfi = debug_info->shared();
  DCHECK(Contains(sfi));

  auto it = map_.find(sfi->unique_id());
  HandleLocation location = it->second;
  DCHECK_EQ(location, list_[index]);
  map_.erase(it);

  list_[index] = list_.back();
  list_.pop_back();

  GlobalHandles::Destroy(location);
  DCHECK(!Contains(sfi));
  DCHECK_EQ(list_.size(), map_.size());
}

void Debug::Unload() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  ClearAllBreakPoints();
  ClearStepping();
  RemoveAllCoverageInfos();
  ClearAllDebuggerHints();
  debug_delegate_ = nullptr;
}

debug::DebugDelegate::ActionAfterInstrumentation
Debug::OnInstrumentationBreak() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (!debug_delegate_) {
    return debug::DebugDelegate::ActionAfterInstrumentation::
        kPauseIfBreakpointsHit;
  }
  DCHECK(in_debug_scope());
  HandleScope scope(isolate_);
  DisableBreak no_recursive_break(this);

  return debug_delegate_->BreakOnInstrumentation(
      v8::Utils::ToLocal(isolate_->native_context()), kInstrumentationId);
}

void Debug::Break(JavaScriptFrame* frame,
                  DirectHandle<JSFunction> break_target) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Just continue if breaks are disabled or debugger cannot be loaded.
  if (break_disabled()) return;

  // Enter the debugger.
  DebugScope debug_scope(this);
  DisableBreak no_recursive_break(this);

  // Return if we fail to retrieve debug info.
  Handle<SharedFunctionInfo> shared(break_target->shared(), isolate_);
  if (!EnsureBreakInfo(shared)) return;
  PrepareFunctionForDebugExecution(shared);

  Handle<DebugInfo> debug_info(TryGetDebugInfo(*shared).value(), isolate_);

  // Find the break location where execution has stopped.
  BreakLocation location = BreakLocation::FromFrame(debug_info, frame);
  const bool hitInstrumentationBreak =
      IsBreakOnInstrumentation(debug_info, location);
  bool shouldPauseAfterInstrumentation = false;
  if (hitInstrumentationBreak) {
    debug::DebugDelegate::ActionAfterInstrumentation action =
        OnInstrumentationBreak();
    switch (action) {
      case debug::DebugDelegate::ActionAfterInstrumentation::kPause:
        shouldPauseAfterInstrumentation = true;
        break;
      case debug::DebugDelegate::ActionAfterInstrumentation::
          kPauseIfBreakpointsHit:
        shouldPauseAfterInstrumentation = false;
        break;
      case debug::DebugDelegate::ActionAfterInstrumentation::kContinue:
        return;
    }
  }

  // Find actual break points, if any, and trigger debug break event.
  ClearMutedLocation();
  bool has_break_points;
  bool scheduled_break =
      scheduled_break_on_function_call() || shouldPauseAfterInstrumentation;
  MaybeHandle<FixedArray> break_points_hit =
      CheckBreakPoints(debug_info, &location, &has_break_points);
  if (!break_points_hit.is_null() || break_on_next_function_call() ||
      scheduled_break) {
    StepAction lastStepAction = last_step_action();
    debug::BreakReasons break_reasons;
    if (scheduled_break) {
      break_reasons.Add(debug::BreakReason::kScheduled);
    }
    // If it's a debugger statement, add the reason and then mute the location
    // so we don't stop a second time.
    bool is_debugger_statement = IsBreakOnDebuggerStatement(shared, location);
    if (is_debugger_statement) {
      break_reasons.Add(debug::BreakReason::kDebuggerStatement);
    }

    // Clear all current stepping setup.
    ClearStepping();
    // Notify the debug event listeners.
    OnDebugBreak(!break_points_hit.is_null()
                     ? break_points_hit.ToHandleChecked()
                     : isolate_->factory()->empty_fixed_array(),
                 lastStepAction, break_reasons);

    if (is_debugger_statement) {
      // Don't pause here a second time
      SetMutedLocation(shared, location);
    }
    return;
  }

  // Debug break at function entry, do not worry about stepping.
  if (location.IsDebugBreakAtEntry()) {
    DCHECK(debug_info->BreakAtEntry());
    return;
  }

  DCHECK_NOT_NULL(frame);

  // No break point. Check for stepping.
  StepAction step_action = last_step_action();
  int current_frame_count = CurrentFrameCount();
  int target_frame_count = thread_local_.target_frame_count_;
  int last_frame_count = thread_local_.last_frame_count_;

  // StepOut at not return position was requested and return break locations
  // were flooded with one shots.
  if (thread_local_.fast_forward_to_return_) {
    // We might hit an instrumentation breakpoint before running into a
    // return/suspend location.
    DCHECK(location.IsReturnOrSuspend() || hitInstrumentationBreak);
    // We have to ignore recursive calls to function.
    if (current_frame_count > target_frame_count) return;
    ClearStepping();
    PrepareStep(StepOut);
    return;
  }

  bool step_break = false;
  switch (step_action) {
    case StepNone:
      if (has_break_points) {
        SetMutedLocation(shared, location);
      }
      return;
    case StepOut:
      // StepOut should not break in a deeper frame than target frame.
      if (current_frame_count > target_frame_count) return;
      step_break = true;
      break;
    case StepOver:
      // StepOver should not break in a deeper frame than target frame.
      if (current_frame_count > target_frame_count) return;
      [[fallthrough]];
    case StepInto: {
      // StepInto and StepOver should enter "generator stepping" mode, except
      // for the implicit initial yield in generators, where it should simply
      // step out of the generator function.
      if (location.IsSuspend()) {
        DCHECK(!has_suspended_generator());
        ClearStepping();
        if (!IsGeneratorFunction(shared->kind()) ||
            location.generator_suspend_id() > 0) {
          thread_local_.suspended_generator_ =
              location.GetGeneratorObjectForSuspendedFrame(frame);
        } else {
          PrepareStep(StepOut);
        }
        return;
      }
      FrameSummary summary = FrameSummary::GetTop(frame);
      const bool frame_or_statement_changed =
          current_frame_count != last_frame_count ||
          thread_local_.last_statement_position_ !=
              summary.SourceStatementPosition();
      // If we stayed on the same frame and reached the same bytecode offset
      // since the last step, we are in a loop and should pause. Otherwise
      // we keep "stepping" through the loop without ever acutally pausing.
      const bool potential_single_statement_loop =
          current_frame_count == last_frame_count &&
          thread_local_.last_bytecode_offset_ == summary.code_offset();
      step_break = step_break || location.IsReturn() ||
                   potential_single_statement_loop ||
                   frame_or_statement_changed;
      break;
    }
  }

  StepAction lastStepAction = last_step_action();
  // Clear all current stepping setup.
  ClearStepping();

  if (step_break) {
    // If it's a debugger statement, add the reason and then mute the location
    // so we don't stop a second time.
    debug::BreakReasons break_reasons;
    bool is_debugger_statement = IsBreakOnDebuggerStatement(shared, location);
    if (is_debugger_statement) {
      break_reasons.Add(debug::BreakReason::kDebuggerStatement);
    }
    // Notify the debug event listeners.
    OnDebugBreak(isolate_->factory()->empty_fixed_array(), lastStepAction,
                 break_reasons);

    if (is_debugger_statement) {
      // Don't pause here a second time
      SetMutedLocation(shared, location);
    }
  } else {
    // Re-prepare to continue.
    PrepareStep(step_action);
  }
}

bool Debug::IsBreakOnInstrumentation(Handle<DebugInfo> debug_info,
                                     const BreakLocation& location) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  bool has_break_points_to_check =
      break_points_active_ && location.HasBreakPoint(isolate_, debug_info);
  if (!has_break_points_to_check) return {};

  DirectHandle<Object> break_points =
      debug_info->GetBreakPoints(isolate_, location.position());
  DCHECK(!IsUndefined(*break_points, isolate_));
  if (!IsFixedArray(*break_points)) {
    const auto break_point = Cast<BreakPoint>(break_points);
    return break_point->id() == kInstrumentationId;
  }

  DirectHandle<FixedArray> array(Cast<FixedArray>(*break_points), isolate_);
  for (int i = 0; i < array->length(); ++i) {
    const auto break_point =
        Cast<BreakPoint>(direct_handle(array->get(i), isolate_));
    if (break_point->id() == kInstrumentationId) {
      return true;
    }
  }
  return false;
}

bool Debug::IsBreakOnDebuggerStatement(
    DirectHandle<SharedFunctionInfo> function, const BreakLocation& location) {
  if (!function->HasBytecodeArray()) {
    return false;
  }
  Tagged<BytecodeArray> original_bytecode =
      function->GetBytecodeArray(isolate_);
  interpreter::Bytecode bytecode = interpreter::Bytecodes::FromByte(
      original_bytecode->get(location.code_offset()));
  return bytecode == interpreter::Bytecode::kDebugger;
}

// Find break point objects for this location, if any, and evaluate them.
// Return an array of break point objects that evaluated true, or an empty
// handle if none evaluated true.
// has_break_points will be true, if there is any (non-instrumentation)
// breakpoint.
MaybeHandle<FixedArray> Debug::CheckBreakPoints(Handle<DebugInfo> debug_info,
                                                BreakLocation* location,
                                                bool* has_break_points) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  bool has_break_points_to_check =
      break_points_active_ && location->HasBreakPoint(isolate_, debug_info);
  if (!has_break_points_to_check) {
    *has_break_points = false;
    return {};
  }

  return Debug::GetHitBreakPoints(debug_info, location->position(),
                                  has_break_points);
}

bool Debug::IsMutedAtAnyBreakLocation(
    DirectHandle<SharedFunctionInfo> function,
    const std::vector<BreakLocation>& locations) {
  // A break location is considered muted if break locations on the current
  // statement have at least one break point, and all of these break points
  // evaluate to false. Aside from not triggering a debug break event at the
  // break location, we also do not trigger one for debugger statements, nor
  // an exception event on exception at this location.
  // This should have been computed at last break, and we should just
  // check that we are not at that location.

  if (IsSmi(thread_local_.muted_function_) ||
      *function != thread_local_.muted_function_) {
    return false;
  }

  for (const BreakLocation& location : locations) {
    if (location.position() == thread_local_.muted_position_) {
      return true;
    }
  }

  return false;
}

#if V8_ENABLE_WEBASSEMBLY
void Debug::SetMutedWasmLocation(DirectHandle<Script> script, int position) {
  thread_local_.muted_function_ = *script;
  thread_local_.muted_position_ = position;
}

bool Debug::IsMutedAtWasmLocation(Tagged<Script> script, int position) {
  return script == thread_local_.muted_function_ &&
         position == thread_local_.muted_position_;
}
#endif  // V8_ENABLE_WEBASSEMBLY

namespace {

// Convenience helper for easier std::optional translation.
bool ToHandle(Isolate* isolate, std::optional<Tagged<DebugInfo>> debug_info,
              Handle<DebugInfo>* out) {
  if (!debug_info.has_value()) return false;
  *out = handle(debug_info.value(), isolate);
  return true;
}

}  // namespace

// Check whether a single break point object is triggered.
bool Debug::CheckBreakPoint(DirectHandle<BreakPoint> break_point,
                            bool is_break_at_entry) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);

  // Instrumentation breakpoints are handled separately.
  if (break_point->id() == kInstrumentationId) {
    return false;
  }

  if (!break_point->condition()->length()) return true;
  Handle<String> condition(break_point->condition(), isolate_);
  MaybeHandle<Object> maybe_result;
  Handle<Object> result;

  if (is_break_at_entry) {
    maybe_result = DebugEvaluate::WithTopmostArguments(isolate_, condition);
  } else {
    // Since we call CheckBreakpoint only for deoptimized frame on top of stack,
    // we can use 0 as index of inlined frame.
    const int inlined_jsframe_index = 0;
    const bool throw_on_side_effect = false;
    maybe_result =
        DebugEvaluate::Local(isolate_, break_frame_id(), inlined_jsframe_index,
                             condition, throw_on_side_effect);
  }

  Handle<Object> maybe_exception;
  bool exception_thrown = true;
  if (maybe_result.ToHandle(&result)) {
    exception_thrown = false;
  } else if (isolate_->has_exception()) {
    maybe_exception = handle(isolate_->exception(), isolate_);
    isolate_->clear_exception();
  }

  CHECK(in_debug_scope());
  DisableBreak no_recursive_break(this);

  {
    RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebuggerCallback);
    debug_delegate_->BreakpointConditionEvaluated(
        v8::Utils::ToLocal(isolate_->native_context()), break_point->id(),
        exception_thrown, v8::Utils::ToLocal(maybe_exception));
  }

  return !result.is_null() ? Object::BooleanValue(*result, isolate_) : false;
}

bool Debug::SetBreakpoint(Handle<SharedFunctionInfo> shared,
                          DirectHandle<BreakPoint> break_point,
                          int* source_position) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);

  // Make sure the function is compiled and has set up the debug info.
  if (!EnsureBreakInfo(shared)) return false;
  PrepareFunctionForDebugExecution(shared);

  Handle<DebugInfo> debug_info(TryGetDebugInfo(*shared).value(), isolate_);
  // Source positions starts with zero.
  DCHECK_LE(0, *source_position);

  // Find the break point and change it.
  *source_position = FindBreakablePosition(debug_info, *source_position);
  DebugInfo::SetBreakPoint(isolate_, debug_info, *source_position, break_point);
  // At least one active break point now.
  DCHECK_LT(0, debug_info->GetBreakPointCount(isolate_));

  ClearBreakPoints(debug_info);
  ApplyBreakPoints(debug_info);
  return true;
}

bool Debug::SetBreakPointForScript(Handle<Script> script,
                                   DirectHandle<String> condition,
                                   int* source_position, int* id) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  *id = ++thread_local_.last_breakpoint_id_;
  DirectHandle<BreakPoint> break_point =
      isolate_->factory()->NewBreakPoint(*id, condition);
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == Script::Type::kWasm) {
    RecordWasmScriptWithBreakpoints(script);
    return WasmScript::SetBreakPoint(script, source_position, break_point);
  }
#endif  //  V8_ENABLE_WEBASSEMBLY

  HandleScope scope(isolate_);

  // Obtain shared function info for the innermost function containing this
  // position.
  Handle<Object> result =
      FindInnermostContainingFunctionInfo(script, *source_position);
  if (IsUndefined(*result, isolate_)) return false;

  auto shared = Cast<SharedFunctionInfo>(result);
  if (!EnsureBreakInfo(shared)) return false;
  PrepareFunctionForDebugExecution(shared);

  // Find the nested shared function info that is closest to the position within
  // the containing function.
  shared = FindClosestSharedFunctionInfoFromPosition(*source_position, script,
                                                     shared);

  // Set the breakpoint in the function.
  return SetBreakpoint(shared, break_point, source_position);
}

int Debug::FindBreakablePosition(Handle<DebugInfo> debug_info,
                                 int source_position) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (debug_info->CanBreakAtEntry()) {
    return kBreakAtEntryPosition;
  } else {
    DCHECK(debug_info->HasInstrumentedBytecodeArray());
    BreakIterator it(debug_info);
    it.SkipToPosition(source_position);
    return it.position();
  }
}

void Debug::ApplyBreakPoints(Handle<DebugInfo> debug_info) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DisallowGarbageCollection no_gc;
  if (debug_info->CanBreakAtEntry()) {
    debug_info->SetBreakAtEntry();
  } else {
    if (!debug_info->HasInstrumentedBytecodeArray()) return;
    Tagged<FixedArray> break_points = debug_info->break_points();
    for (int i = 0; i < break_points->length(); i++) {
      if (IsUndefined(break_points->get(i), isolate_)) continue;
      Tagged<BreakPointInfo> info = Cast<BreakPointInfo>(break_points->get(i));
      if (info->GetBreakPointCount(isolate_) == 0) continue;
      DCHECK(debug_info->HasInstrumentedBytecodeArray());
      BreakIterator it(debug_info);
      it.SkipToPosition(info->source_position());
      it.SetDebugBreak();
    }
  }
  debug_info->SetDebugExecutionMode(DebugInfo::kBreakpoints);
}

void Debug::ClearBreakPoints(Handle<DebugInfo> debug_info) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (debug_info->CanBreakAtEntry()) {
    debug_info->ClearBreakAtEntry();
  } else {
    // If we attempt to clear breakpoints but none exist, simply return. This
    // can happen e.g. CoverageInfos exist but no breakpoints are set.
    if (!debug_info->HasInstrumentedBytecodeArray() ||
        !debug_info->HasBreakInfo()) {
      return;
    }

    DisallowGarbageCollection no_gc;
    for (BreakIterator it(debug_info); !it.Done(); it.Next()) {
      it.ClearDebugBreak();
    }
  }
}

void Debug::ClearBreakPoint(DirectHandle<BreakPoint> break_point) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);

  DebugInfoCollection::Iterator it(&debug_infos_);
  for (; it.HasNext(); it.Advance()) {
    Handle<DebugInfo> debug_info(it.Next(), isolate_);
    if (!debug_info->HasBreakInfo()) continue;

    DirectHandle<Object> result =
        DebugInfo::FindBreakPointInfo(isolate_, debug_info, break_point);
    if (IsUndefined(*result, isolate_)) continue;

    if (DebugInfo::ClearBreakPoint(isolate_, debug_info, break_point)) {
      ClearBreakPoints(debug_info);
      if (debug_info->GetBreakPointCount(isolate_) == 0) {
        debug_info->ClearBreakInfo(isolate_);
        if (debug_info->IsEmpty()) it.DeleteNext();
      } else {
        ApplyBreakPoints(debug_info);
      }
      return;
    }
  }
}

int Debug::GetFunctionDebuggingId(DirectHandle<JSFunction> function) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate_);
  DirectHandle<DebugInfo> debug_info = GetOrCreateDebugInfo(shared);
  int id = debug_info->debugging_id();
  if (id == DebugInfo::kNoDebuggingId) {
    id = isolate_->heap()->NextDebuggingId();
    debug_info->set_debugging_id(id);
  }
  return id;
}

bool Debug::SetBreakpointForFunction(Handle<SharedFunctionInfo> shared,
                                     DirectHandle<String> condition, int* id,
                                     BreakPointKind kind) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (kind == kInstrumentation) {
    *id = kInstrumentationId;
  } else {
    *id = ++thread_local_.last_breakpoint_id_;
  }
  DirectHandle<BreakPoint> breakpoint =
      isolate_->factory()->NewBreakPoint(*id, condition);
  int source_position = 0;
#if V8_ENABLE_WEBASSEMBLY
  if (shared->HasWasmExportedFunctionData()) {
    Tagged<WasmExportedFunctionData> function_data =
        shared->wasm_exported_function_data();
    int func_index = function_data->function_index();
    // TODO(42204563): Avoid crashing if the instance object is not available.
    CHECK(function_data->instance_data()->has_instance_object());
    Tagged<WasmModuleObject> module_obj =
        function_data->instance_data()->instance_object()->module_object();
    DirectHandle<Script> script(module_obj->script(), isolate_);
    return WasmScript::SetBreakPointOnFirstBreakableForFunction(
        script, func_index, breakpoint);
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return SetBreakpoint(shared, breakpoint, &source_position);
}

void Debug::RemoveBreakpoint(int id) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DirectHandle<BreakPoint> breakpoint = isolate_->factory()->NewBreakPoint(
      id, isolate_->factory()->empty_string());
  ClearBreakPoint(breakpoint);
}

#if V8_ENABLE_WEBASSEMBLY
void Debug::SetInstrumentationBreakpointForWasmScript(Handle<Script> script,
                                                      int* id) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(Script::Type::kWasm, script->type());
  *id = kInstrumentationId;

  DirectHandle<BreakPoint> break_point = isolate_->factory()->NewBreakPoint(
      *id, isolate_->factory()->empty_string());
  RecordWasmScriptWithBreakpoints(script);
  WasmScript::SetInstrumentationBreakpoint(script, break_point);
}

void Debug::RemoveBreakpointForWasmScript(DirectHandle<Script> script, int id) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (script->type() == Script::Type::kWasm) {
    WasmScript::ClearBreakPointById(script, id);
  }
}

void Debug::RecordWasmScriptWithBreakpoints(Handle<Script> script) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (wasm_scripts_with_break_points_.is_null()) {
    DirectHandle<WeakArrayList> new_list =
        isolate_->factory()->NewWeakArrayList(4);
    wasm_scripts_with_break_points_ =
        isolate_->global_handles()->Create(*new_list);
  }
  {
    DisallowGarbageCollection no_gc;
    for (int idx = wasm_scripts_with_break_points_->length() - 1; idx >= 0;
         --idx) {
      Tagged<HeapObject> wasm_script;
      if (wasm_scripts_with_break_points_->Get(idx).GetHeapObject(
              &wasm_script) &&
          wasm_script == *script) {
        return;
      }
    }
  }
  DirectHandle<WeakArrayList> new_list = WeakArrayList::Append(
      isolate_, wasm_scripts_with_break_points_, MaybeObjectHandle{script});
  if (*new_list != *wasm_scripts_with_break_points_) {
    isolate_->global_handles()->Destroy(
        wasm_scripts_with_break_points_.location());
    wasm_scripts_with_break_points_ =
        isolate_->global_handles()->Create(*new_list);
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Clear out all the debug break code.
void Debug::ClearAllBreakPoints() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  ClearAllDebugInfos([=, this](Handle<DebugInfo> info) {
    ClearBreakPoints(info);
    info->ClearBreakInfo(isolate_);
  });
#if V8_ENABLE_WEBASSEMBLY
  // Clear all wasm breakpoints.
  if (!wasm_scripts_with_break_points_.is_null()) {
    DisallowGarbageCollection no_gc;
    for (int idx = wasm_scripts_with_break_points_->length() - 1; idx >= 0;
         --idx) {
      Tagged<HeapObject> raw_wasm_script;
      if (wasm_scripts_with_break_points_->Get(idx).GetHeapObject(
              &raw_wasm_script)) {
        Tagged<Script> wasm_script = Cast<Script>(raw_wasm_script);
        WasmScript::ClearAllBreakpoints(wasm_script);
        wasm_script->wasm_native_module()->GetDebugInfo()->RemoveIsolate(
            isolate_);
      }
    }
    wasm_scripts_with_break_points_ = Handle<WeakArrayList>{};
  }
#endif  // V8_ENABLE_WEBASSEMBLY
}

void Debug::FloodWithOneShot(Handle<SharedFunctionInfo> shared,
                             bool returns_only) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (IsBlackboxed(shared)) return;
  // Make sure the function is compiled and has set up the debug info.
  if (!EnsureBreakInfo(shared)) return;
  PrepareFunctionForDebugExecution(shared);

  Handle<DebugInfo> debug_info(TryGetDebugInfo(*shared).value(), isolate_);
  // Flood the function with break points.
  DCHECK(debug_info->HasInstrumentedBytecodeArray());
  for (BreakIterator it(debug_info); !it.Done(); it.Next()) {
    if (returns_only && !it.GetBreakLocation().IsReturnOrSuspend()) continue;
    it.SetDebugBreak();
  }
}

void Debug::ChangeBreakOnException(ExceptionBreakType type, bool enable) {
  if (type == BreakUncaughtException) {
    break_on_uncaught_exception_ = enable;
  } else {
    break_on_caught_exception_ = enable;
  }
}

bool Debug::IsBreakOnException(ExceptionBreakType type) {
  if (type == BreakUncaughtException) {
    return break_on_uncaught_exception_;
  } else {
    return break_on_caught_exception_;
  }
}

MaybeHandle<FixedArray> Debug::GetHitBreakPoints(
    DirectHandle<DebugInfo> debug_info, int position, bool* has_break_points) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DirectHandle<Object> break_points =
      debug_info->GetBreakPoints(isolate_, position);
  bool is_break_at_entry = debug_info->BreakAtEntry();
  DCHECK(!IsUndefined(*break_points, isolate_));
  if (!IsFixedArray(*break_points)) {
    const auto break_point = Cast<BreakPoint>(break_points);
    *has_break_points = break_point->id() != kInstrumentationId;
    if (!CheckBreakPoint(break_point, is_break_at_entry)) {
      return {};
    }
    Handle<FixedArray> break_points_hit = isolate_->factory()->NewFixedArray(1);
    break_points_hit->set(0, *break_points);
    return break_points_hit;
  }

  DirectHandle<FixedArray> array(Cast<FixedArray>(*break_points), isolate_);
  int num_objects = array->length();
  Handle<FixedArray> break_points_hit =
      isolate_->factory()->NewFixedArray(num_objects);
  int break_points_hit_count = 0;
  *has_break_points = false;
  for (int i = 0; i < num_objects; ++i) {
    const auto break_point =
        Cast<BreakPoint>(direct_handle(array->get(i), isolate_));
    *has_break_points |= break_point->id() != kInstrumentationId;
    if (CheckBreakPoint(break_point, is_break_at_entry)) {
      break_points_hit->set(break_points_hit_count++, *break_point);
    }
  }
  if (break_points_hit_count == 0) return {};
  break_points_hit->RightTrim(isolate_, break_points_hit_count);
  return break_points_hit;
}

void Debug::SetBreakOnNextFunctionCall() {
  // This method forces V8 to break on next function call regardless current
  // last_step_action_. If any break happens between SetBreakOnNextFunctionCall
  // and ClearBreakOnNextFunctionCall, we will clear this flag and stepping. If
  // break does not happen, e.g. all called functions are blackboxed or no
  // function is called, then we will clear this flag and let stepping continue
  // its normal business.
  thread_local_.break_on_next_function_call_ = true;
  UpdateHookOnFunctionCall();
}

void Debug::ClearBreakOnNextFunctionCall() {
  thread_local_.break_on_next_function_call_ = false;
  UpdateHookOnFunctionCall();
}

void Debug::PrepareStepIn(DirectHandle<JSFunction> function) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  CHECK(last_step_action() >= StepInto || break_on_next_function_call() ||
        scheduled_break_on_function_call());
  if (ignore_events()) return;
  if (in_debug_scope()) return;
  if (break_disabled()) return;
  Handle<SharedFunctionInfo> shared(function->shared(), isolate_);
  if (IsBlackboxed(shared)) return;
  if (*function == thread_local_.ignore_step_into_function_) return;
  thread_local_.ignore_step_into_function_ = Smi::zero();
  FloodWithOneShot(shared);
}

void Debug::PrepareStepInSuspendedGenerator() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  CHECK(has_suspended_generator());
  if (ignore_events()) return;
  if (in_debug_scope()) return;
  if (break_disabled()) return;
  thread_local_.last_step_action_ = StepInto;
  UpdateHookOnFunctionCall();
  DirectHandle<JSFunction> function(
      Cast<JSGeneratorObject>(thread_local_.suspended_generator_)->function(),
      isolate_);
  FloodWithOneShot(handle(function->shared(), isolate_));
  clear_suspended_generator();
}

void Debug::PrepareStepOnThrow() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (last_step_action() == StepNone) return;
  if (ignore_events()) return;
  if (in_debug_scope()) return;
  if (break_disabled()) return;

  ClearOneShot();

  int current_frame_count = CurrentFrameCount();

  // Iterate through the JavaScript stack looking for handlers.
  JavaScriptStackFrameIterator it(isolate_);
  while (!it.done()) {
    JavaScriptFrame* frame = it.frame();
    if (frame->LookupExceptionHandlerInTable(nullptr, nullptr) > 0) break;
    std::vector<Tagged<SharedFunctionInfo>> infos;
    frame->GetFunctions(&infos);
    current_frame_count -= infos.size();
    it.Advance();
  }

  // No handler found. Nothing to instrument.
  if (it.done()) return;

  bool found_handler = false;
  // Iterate frames, including inlined frames. First, find the handler frame.
  // Then skip to the frame we want to break in, then instrument for stepping.
  for (; !it.done(); it.Advance()) {
    JavaScriptFrame* frame = JavaScriptFrame::cast(it.frame());
    if (last_step_action() == StepInto) {
      // Deoptimize frame to ensure calls are checked for step-in.
      Deoptimizer::DeoptimizeFunction(frame->function());
    }
    std::vector<FrameSummary> summaries;
    frame->Summarize(&summaries);
    for (size_t i = summaries.size(); i != 0; i--, current_frame_count--) {
      const FrameSummary& summary = summaries[i - 1];
      if (!found_handler) {
        // We have yet to find the handler. If the frame inlines multiple
        // functions, we have to check each one for the handler.
        // If it only contains one function, we already found the handler.
        if (summaries.size() > 1) {
          DirectHandle<AbstractCode> code =
              summary.AsJavaScript().abstract_code();
          CHECK_EQ(CodeKind::INTERPRETED_FUNCTION, code->kind(isolate_));
          HandlerTable table(code->GetBytecodeArray());
          int code_offset = summary.code_offset();
          found_handler = table.LookupHandlerIndexForRange(code_offset) !=
                          HandlerTable::kNoHandlerFound;
        } else {
          found_handler = true;
        }
      }

      if (found_handler) {
        // We found the handler. If we are stepping next or out, we need to
        // iterate until we found the suitable target frame to break in.
        if ((last_step_action() == StepOver || last_step_action() == StepOut) &&
            current_frame_count > thread_local_.target_frame_count_) {
          continue;
        }
        Handle<SharedFunctionInfo> info(
            summary.AsJavaScript().function()->shared(), isolate_);
        if (IsBlackboxed(info)) continue;
        FloodWithOneShot(info);
        return;
      }
    }
  }
}

void Debug::PrepareStep(StepAction step_action) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);

  DCHECK(in_debug_scope());

  // Get the frame where the execution has stopped and skip the debug frame if
  // any. The debug frame will only be present if execution was stopped due to
  // hitting a break point. In other situations (e.g. unhandled exception) the
  // debug frame is not present.
  StackFrameId frame_id = break_frame_id();
  // If there is no JavaScript stack don't do anything.
  if (frame_id == StackFrameId::NO_ID) return;

  thread_local_.last_step_action_ = step_action;

  DebuggableStackFrameIterator frames_it(isolate_, frame_id);
  CommonFrame* frame = frames_it.frame();

  BreakLocation location = BreakLocation::Invalid();
  Handle<SharedFunctionInfo> shared;
  int current_frame_count = CurrentFrameCount();

  if (frame->is_java_script()) {
    JavaScriptFrame* js_frame = JavaScriptFrame::cast(frame);
    DCHECK(IsJSFunction(js_frame->function()));

    // Get the debug info (create it if it does not exist).
    auto summary = FrameSummary::GetTop(frame).AsJavaScript();
    DirectHandle<JSFunction> function(summary.function());
    shared = Handle<SharedFunctionInfo>(function->shared(), isolate_);
    if (!EnsureBreakInfo(shared)) return;
    PrepareFunctionForDebugExecution(shared);

    // PrepareFunctionForDebugExecution can invalidate Baseline frames
    js_frame = JavaScriptFrame::cast(frames_it.Reframe());

    Handle<DebugInfo> debug_info(TryGetDebugInfo(*shared).value(), isolate_);
    location = BreakLocation::FromFrame(debug_info, js_frame);

    // Any step at a return is a step-out, and a step-out at a suspend behaves
    // like a return.
    if (location.IsReturn() ||
        (location.IsSuspend() && step_action == StepOut)) {
      // On StepOut we'll ignore our further calls to current function in
      // PrepareStepIn callback.
      if (last_step_action() == StepOut) {
        thread_local_.ignore_step_into_function_ = *function;
      }
      step_action = StepOut;
      thread_local_.last_step_action_ = StepInto;
    }

    // We need to schedule DebugOnFunction call callback
    UpdateHookOnFunctionCall();

    // A step-next in blackboxed function is a step-out.
    if (step_action == StepOver && IsBlackboxed(shared)) step_action = StepOut;

    thread_local_.last_statement_position_ = summary.SourceStatementPosition();
    thread_local_.last_bytecode_offset_ = summary.code_offset();
    thread_local_.last_frame_count_ = current_frame_count;
    // No longer perform the current async step.
    clear_suspended_generator();
#if V8_ENABLE_WEBASSEMBLY
  } else if (frame->is_wasm() && step_action != StepOut) {
#if V8_ENABLE_DRUMBRAKE
    // TODO(paolosev@microsoft.com) - If we are running with the interpreter, we
    // cannot step.
    if (frame->is_wasm_interpreter_entry()) return;
#endif  // V8_ENABLE_DRUMBRAKE
    // Handle stepping in wasm.
    WasmFrame* wasm_frame = WasmFrame::cast(frame);
    auto* debug_info = wasm_frame->native_module()->GetDebugInfo();
    if (debug_info->PrepareStep(wasm_frame)) {
      UpdateHookOnFunctionCall();
      return;
    }
    // If the wasm code is not debuggable or will return after this step
    // (indicated by {PrepareStep} returning false), then step out of that frame
    // instead.
    step_action = StepOut;
    UpdateHookOnFunctionCall();
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  switch (step_action) {
    case StepNone:
      UNREACHABLE();
    case StepOut: {
      // Clear last position info. For stepping out it does not matter.
      thread_local_.last_statement_position_ = kNoSourcePosition;
      thread_local_.last_bytecode_offset_ = kFunctionEntryBytecodeOffset;
      thread_local_.last_frame_count_ = -1;
      if (!shared.is_null()) {
        if (!location.IsReturnOrSuspend() && !IsBlackboxed(shared)) {
          // At not return position we flood return positions with one shots and
          // will repeat StepOut automatically at next break.
          thread_local_.target_frame_count_ = current_frame_count;
          thread_local_.fast_forward_to_return_ = true;
          FloodWithOneShot(shared, true);
          return;
        }
        if (IsAsyncFunction(shared->kind())) {
          // Stepping out of an async function whose implicit promise is awaited
          // by some other async function, should resume the latter. The return
          // value here is either a JSPromise or a JSGeneratorObject (for the
          // initial yield of async generators).
          Handle<JSReceiver> return_value(
              Cast<JSReceiver>(thread_local_.return_value_), isolate_);
          DirectHandle<Object> awaited_by_holder = JSReceiver::GetDataProperty(
              isolate_, return_value,
              isolate_->factory()->promise_awaited_by_symbol());
          if (IsWeakFixedArray(*awaited_by_holder, isolate_)) {
            auto weak_fixed_array = Cast<WeakFixedArray>(awaited_by_holder);
            if (weak_fixed_array->length() == 1 &&
                weak_fixed_array->get(0).IsWeak()) {
              DirectHandle<HeapObject> awaited_by(
                  weak_fixed_array->get(0).GetHeapObjectAssumeWeak(isolate_),
                  isolate_);
              if (IsJSGeneratorObject(*awaited_by)) {
                DCHECK(!has_suspended_generator());
                thread_local_.suspended_generator_ = *awaited_by;
                ClearStepping();
                return;
              }
            }
          }
        }
      }
      // Skip the current frame, find the first frame we want to step out to
      // and deoptimize every frame along the way.
      bool in_current_frame = true;
      for (; !frames_it.done(); frames_it.Advance()) {
#if V8_ENABLE_WEBASSEMBLY
#if V8_ENABLE_DRUMBRAKE
        // TODO(paolosev@microsoft.com): Implement stepping out from JS to wasm
        // interpreter.
        if (frame->is_wasm_interpreter_entry()) continue;
#endif  // V8_ENABLE_DRUMBRAKE
        if (frames_it.frame()->is_wasm()) {
          if (in_current_frame) {
            in_current_frame = false;
            continue;
          }
          // Handle stepping out into Wasm.
          WasmFrame* wasm_frame = WasmFrame::cast(frames_it.frame());
          auto* debug_info = wasm_frame->native_module()->GetDebugInfo();
          debug_info->PrepareStepOutTo(wasm_frame);
          return;
        }
#endif  // V8_ENABLE_WEBASSEMBLY
        JavaScriptFrame* js_frame = JavaScriptFrame::cast(frames_it.frame());
        if (last_step_action() == StepInto) {
          // Deoptimize frame to ensure calls are checked for step-in.
          Deoptimizer::DeoptimizeFunction(js_frame->function());
        }
        HandleScope inner_scope(isolate_);
        std::vector<Handle<SharedFunctionInfo>> infos;
        js_frame->GetFunctions(&infos);
        for (; !infos.empty(); current_frame_count--) {
          Handle<SharedFunctionInfo> info = infos.back();
          infos.pop_back();
          if (in_current_frame) {
            // We want to step out, so skip the current frame.
            in_current_frame = false;
            continue;
          }
          if (IsBlackboxed(info)) continue;
          FloodWithOneShot(info);
          thread_local_.target_frame_count_ = current_frame_count;
          return;
        }
      }
      break;
    }
    case StepOver:
      thread_local_.target_frame_count_ = current_frame_count;
      [[fallthrough]];
    case StepInto:
      FloodWithOneShot(shared);
      break;
  }
}

// Simple function for returning the source positions for active break points.
// static
Handle<Object> Debug::GetSourceBreakLocations(
    Isolate* isolate, DirectHandle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kDebugger);
  if (!shared->HasBreakInfo(isolate)) {
    return isolate->factory()->undefined_value();
  }

  DirectHandle<DebugInfo> debug_info(
      isolate->debug()->TryGetDebugInfo(*shared).value(), isolate);
  if (debug_info->GetBreakPointCount(isolate) == 0) {
    return isolate->factory()->undefined_value();
  }
  Handle<FixedArray> locations = isolate->factory()->NewFixedArray(
      debug_info->GetBreakPointCount(isolate));
  int count = 0;
  for (int i = 0; i < debug_info->break_points()->length(); ++i) {
    if (!IsUndefined(debug_info->break_points()->get(i), isolate)) {
      Tagged<BreakPointInfo> break_point_info =
          Cast<BreakPointInfo>(debug_info->break_points()->get(i));
      int break_points = break_point_info->GetBreakPointCount(isolate);
      if (break_points == 0) continue;
      for (int j = 0; j < break_points; ++j) {
        locations->set(count++,
                       Smi::FromInt(break_point_info->source_position()));
      }
    }
  }
  return locations;
}

void Debug::ClearStepping() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Clear the various stepping setup.
  ClearOneShot();
  ClearMutedLocation();

  thread_local_.last_step_action_ = StepNone;
  thread_local_.last_statement_position_ = kNoSourcePosition;
  thread_local_.last_bytecode_offset_ = kFunctionEntryBytecodeOffset;
  thread_local_.ignore_step_into_function_ = Smi::zero();
  thread_local_.fast_forward_to_return_ = false;
  thread_local_.last_frame_count_ = -1;
  thread_local_.target_frame_count_ = -1;
  thread_local_.break_on_next_function_call_ = false;
  thread_local_.scheduled_break_on_next_function_call_ = false;
  clear_restart_frame();
  UpdateHookOnFunctionCall();
}

// Clears all the one-shot break points that are currently set. Normally this
// function is called each time a break point is hit as one shot break points
// are used to support stepping.
void Debug::ClearOneShot() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // The current implementation just runs through all the breakpoints. When the
  // last break point for a function is removed that function is automatically
  // removed from the list.
  HandleScope scope(isolate_);
  DebugInfoCollection::Iterator it(&debug_infos_);
  for (; it.HasNext(); it.Advance()) {
    Handle<DebugInfo> debug_info(it.Next(), isolate_);
    ClearBreakPoints(debug_info);
    ApplyBreakPoints(debug_info);
  }
}

void Debug::ClearMutedLocation() {
  thread_local_.muted_function_ = Smi::zero();
  thread_local_.muted_position_ = -1;
}

void Debug::SetMutedLocation(DirectHandle<SharedFunctionInfo> function,
                             const BreakLocation& location) {
  thread_local_.muted_function_ = *function;
  thread_local_.muted_position_ = location.position();
}

namespace {
class DiscardBaselineCodeVisitor : public ThreadVisitor {
 public:
  explicit DiscardBaselineCodeVisitor(Tagged<SharedFunctionInfo> shared)
      : shared_(shared) {}
  DiscardBaselineCodeVisitor() : shared_(SharedFunctionInfo()) {}

  void VisitThread(Isolate* isolate, ThreadLocalTop* top) override {
    DisallowGarbageCollection diallow_gc;
    bool deopt_all = shared_ == SharedFunctionInfo();
    for (JavaScriptStackFrameIterator it(isolate, top); !it.done();
         it.Advance()) {
      if (!deopt_all && it.frame()->function()->shared() != shared_) continue;
      if (it.frame()->type() == StackFrame::BASELINE) {
        BaselineFrame* frame = BaselineFrame::cast(it.frame());
        int bytecode_offset = frame->GetBytecodeOffset();
        Address* pc_addr = frame->pc_address();
        Address advance;
        if (bytecode_offset == kFunctionEntryBytecodeOffset) {
          advance = BUILTIN_CODE(isolate, BaselineOutOfLinePrologueDeopt)
                        ->instruction_start();
        } else {
          advance = BUILTIN_CODE(isolate, InterpreterEnterAtNextBytecode)
                        ->instruction_start();
        }
        PointerAuthentication::ReplacePC(pc_addr, advance, kSystemPointerSize);
        InterpretedFrame::cast(it.Reframe())
            ->PatchBytecodeOffset(bytecode_offset);
      } else if (it.frame()->type() == StackFrame::INTERPRETED) {
        // Check if the PC is a baseline entry trampoline. If it is, replace it
        // with the corresponding interpreter entry trampoline.
        // This is the case if a baseline function was inlined into a function
        // we deoptimized in the debugger and are stepping into it.
        JavaScriptFrame* frame = it.frame();
        Address pc = frame->pc();
        Builtin builtin = OffHeapInstructionStream::TryLookupCode(isolate, pc);
        if (builtin == Builtin::kBaselineOrInterpreterEnterAtBytecode ||
            builtin == Builtin::kBaselineOrInterpreterEnterAtNextBytecode) {
          Address* pc_addr = frame->pc_address();
          Builtin advance =
              builtin == Builtin::kBaselineOrInterpreterEnterAtBytecode
                  ? Builtin::kInterpreterEnterAtBytecode
                  : Builtin::kInterpreterEnterAtNextBytecode;
          Address advance_pc =
              isolate->builtins()->code(advance)->instruction_start();
          PointerAuthentication::ReplacePC(pc_addr, advance_pc,
                                           kSystemPointerSize);
        }
      }
    }
  }

 private:
  Tagged<SharedFunctionInfo> shared_;
};
}  // namespace

void Debug::DiscardBaselineCode(Tagged<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK(shared->HasBaselineCode());
  DiscardBaselineCodeVisitor visitor(shared);
  visitor.VisitThread(isolate_, isolate_->thread_local_top());
  isolate_->thread_manager()->IterateArchivedThreads(&visitor);
  // TODO(v8:11429): Avoid this heap walk somehow.
  HeapObjectIterator iterator(isolate_->heap());
  auto trampoline = BUILTIN_CODE(isolate_, InterpreterEntryTrampoline);
  shared->FlushBaselineCode();
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    if (IsJSFunction(obj)) {
      Tagged<JSFunction> fun = Cast<JSFunction>(obj);
      if (fun->shared() == shared && fun->ActiveTierIsBaseline(isolate_)) {
        fun->set_code(*trampoline);
      }
    }
  }
}

void Debug::DiscardAllBaselineCode() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DiscardBaselineCodeVisitor visitor;
  visitor.VisitThread(isolate_, isolate_->thread_local_top());
  HeapObjectIterator iterator(isolate_->heap());
  auto trampoline = BUILTIN_CODE(isolate_, InterpreterEntryTrampoline);
  isolate_->thread_manager()->IterateArchivedThreads(&visitor);
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    if (IsJSFunction(obj)) {
      Tagged<JSFunction> fun = Cast<JSFunction>(obj);
      if (fun->ActiveTierIsBaseline(isolate_)) {
        fun->set_code(*trampoline);
      }
    } else if (IsSharedFunctionInfo(obj)) {
      Tagged<SharedFunctionInfo> shared = Cast<SharedFunctionInfo>(obj);
      if (shared->HasBaselineCode()) {
        shared->FlushBaselineCode();
      }
    }
  }
}

void Debug::DeoptimizeFunction(DirectHandle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);

  if (shared->HasBaselineCode()) {
    DiscardBaselineCode(*shared);
  }
  Deoptimizer::DeoptimizeAllOptimizedCodeWithFunction(isolate_, shared);
}

void Debug::PrepareFunctionForDebugExecution(
    DirectHandle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // To prepare bytecode for debugging, we already need to have the debug
  // info (containing the debug copy) upfront, but since we do not recompile,
  // preparing for break points cannot fail.
  DCHECK(shared->is_compiled());
  DirectHandle<DebugInfo> debug_info(TryGetDebugInfo(*shared).value(),
                                     isolate_);
  if (debug_info->flags(kRelaxedLoad) & DebugInfo::kPreparedForDebugExecution) {
    return;
  }

  // Have to discard baseline code before installing debug bytecode, since the
  // bytecode array field on the baseline code object is immutable.
  if (debug_info->CanBreakAtEntry()) {
    // Deopt everything in case the function is inlined anywhere.
    Deoptimizer::DeoptimizeAll(isolate_);
    DiscardAllBaselineCode();
  } else {
    DeoptimizeFunction(shared);
  }

  if (shared->HasBytecodeArray()) {
    DCHECK(!shared->HasBaselineCode());
    SharedFunctionInfo::InstallDebugBytecode(shared, isolate_);
  }

  if (debug_info->CanBreakAtEntry()) {
    InstallDebugBreakTrampoline();
  } else {
    // Update PCs on the stack to point to recompiled code.
    RedirectActiveFunctions redirect_visitor(
        isolate_, *shared, RedirectActiveFunctions::Mode::kUseDebugBytecode);
    redirect_visitor.VisitThread(isolate_, isolate_->thread_local_top());
    isolate_->thread_manager()->IterateArchivedThreads(&redirect_visitor);
  }

  debug_info->set_flags(
      debug_info->flags(kRelaxedLoad) | DebugInfo::kPreparedForDebugExecution,
      kRelaxedStore);
}

namespace {

bool IsJSFunctionAndNeedsTrampoline(Isolate* isolate,
                                    Tagged<Object> maybe_function) {
  if (!IsJSFunction(maybe_function)) return false;
  std::optional<Tagged<DebugInfo>> debug_info =
      isolate->debug()->TryGetDebugInfo(
          Cast<JSFunction>(maybe_function)->shared());
  return debug_info.has_value() && debug_info.value()->CanBreakAtEntry();
}

}  // namespace

void Debug::InstallDebugBreakTrampoline() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Check the list of debug infos whether the debug break trampoline needs to
  // be installed. If that's the case, iterate the heap for functions to rewire
  // to the trampoline.
  // If there is a breakpoint at function entry, we need to install trampoline.
  bool needs_to_use_trampoline = false;
  // If there we break at entry to an api callback, we need to clear ICs.
  bool needs_to_clear_ic = false;

  DebugInfoCollection::Iterator it(&debug_infos_);
  for (; it.HasNext(); it.Advance()) {
    Tagged<DebugInfo> debug_info = it.Next();
    if (debug_info->CanBreakAtEntry()) {
      needs_to_use_trampoline = true;
      if (debug_info->shared()->IsApiFunction()) {
        needs_to_clear_ic = true;
        break;
      }
    }
  }

  if (!needs_to_use_trampoline) return;

  HandleScope scope(isolate_);
  DirectHandle<Code> trampoline = BUILTIN_CODE(isolate_, DebugBreakTrampoline);
  std::vector<Handle<JSFunction>> needs_compile;
  using AccessorPairWithContext =
      std::pair<Handle<AccessorPair>, Handle<NativeContext>>;
  std::vector<AccessorPairWithContext> needs_instantiate;
  {
    // Deduplicate {needs_instantiate} by recording all collected AccessorPairs.
    std::set<Tagged<AccessorPair>> recorded;
    HeapObjectIterator iterator(isolate_->heap());
    DisallowGarbageCollection no_gc;
    for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
         obj = iterator.Next()) {
      if (needs_to_clear_ic && IsFeedbackVector(obj)) {
        Cast<FeedbackVector>(obj)->ClearSlots(isolate_);
        continue;
      } else if (IsJSFunctionAndNeedsTrampoline(isolate_, obj)) {
        Tagged<JSFunction> fun = Cast<JSFunction>(obj);
        if (!fun->is_compiled(isolate_)) {
          needs_compile.push_back(handle(fun, isolate_));
        } else {
          fun->set_code(*trampoline);
        }
      } else if (IsJSObject(obj)) {
        Tagged<JSObject> object = Cast<JSObject>(obj);
        Tagged<DescriptorArray> descriptors =
            object->map()->instance_descriptors(kRelaxedLoad);

        for (InternalIndex i : object->map()->IterateOwnDescriptors()) {
          if (descriptors->GetDetails(i).kind() == PropertyKind::kAccessor) {
            Tagged<Object> value = descriptors->GetStrongValue(i);
            if (!IsAccessorPair(value)) continue;

            Tagged<AccessorPair> accessor_pair = Cast<AccessorPair>(value);
            if (!IsFunctionTemplateInfo(accessor_pair->getter()) &&
                !IsFunctionTemplateInfo(accessor_pair->setter())) {
              continue;
            }
            if (recorded.find(accessor_pair) != recorded.end()) continue;

            needs_instantiate.emplace_back(
                handle(accessor_pair, isolate_),
                handle(object->GetCreationContext().value(), isolate_));
            recorded.insert(accessor_pair);
          }
        }
      }
    }
  }

  // Forcibly instantiate all lazy accessor pairs to make sure that they
  // properly hit the debug break trampoline.
  for (AccessorPairWithContext tuple : needs_instantiate) {
    DirectHandle<AccessorPair> accessor_pair = tuple.first;
    Handle<NativeContext> native_context = tuple.second;
    Handle<Object> getter = AccessorPair::GetComponent(
        isolate_, native_context, accessor_pair, ACCESSOR_GETTER);
    if (IsJSFunctionAndNeedsTrampoline(isolate_, *getter)) {
      Cast<JSFunction>(getter)->set_code(*trampoline);
    }

    DirectHandle<Object> setter = AccessorPair::GetComponent(
        isolate_, native_context, accessor_pair, ACCESSOR_SETTER);
    if (IsJSFunctionAndNeedsTrampoline(isolate_, *setter)) {
      Cast<JSFunction>(setter)->set_code(*trampoline);
    }
  }

  // By overwriting the function code with DebugBreakTrampoline, which tailcalls
  // to shared code, we bypass CompileLazy. Perform CompileLazy here instead.
  for (Handle<JSFunction> fun : needs_compile) {
    IsCompiledScope is_compiled_scope;
    Compiler::Compile(isolate_, fun, Compiler::CLEAR_EXCEPTION,
                      &is_compiled_scope);
    DCHECK(is_compiled_scope.is_compiled());
    fun->set_code(*trampoline);
  }
}

namespace {
void FindBreakablePositions(Handle<DebugInfo> debug_info, int start_position,
                            int end_position,
                            std::vector<BreakLocation>* locations) {
  DCHECK(debug_info->HasInstrumentedBytecodeArray());
  BreakIterator it(debug_info);
  while (!it.Done()) {
    if (it.GetDebugBreakType() != DEBUG_BREAK_SLOT_AT_SUSPEND &&
        it.position() >= start_position && it.position() < end_position) {
      locations->push_back(it.GetBreakLocation());
    }
    it.Next();
  }
}

bool CompileTopLevel(Isolate* isolate, Handle<Script> script,
                     MaybeHandle<SharedFunctionInfo>* result = nullptr) {
  if (script->compilation_type() == Script::CompilationType::kEval) {
    return false;
  }
  UnoptimizedCompileState compile_state;
  ReusableUnoptimizedCompileState reusable_state(isolate);
  UnoptimizedCompileFlags flags =
      UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
  flags.set_is_reparse(true);
  ParseInfo parse_info(isolate, flags, &compile_state, &reusable_state);
  IsCompiledScope is_compiled_scope;
  const MaybeHandle<SharedFunctionInfo> maybe_result =
      Compiler::CompileToplevel(&parse_info, script, isolate,
                                &is_compiled_scope);
  if (maybe_result.is_null()) {
    if (isolate->has_exception()) {
      isolate->clear_exception();
    }
    return false;
  }
  if (result) *result = maybe_result;
  return true;
}
}  // namespace

bool Debug::GetPossibleBreakpoints(Handle<Script> script, int start_position,
                                   int end_position, bool restrict_to_function,
                                   std::vector<BreakLocation>* locations) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (restrict_to_function) {
    Handle<Object> result =
        FindInnermostContainingFunctionInfo(script, start_position);
    if (IsUndefined(*result, isolate_)) return false;

    // Make sure the function has set up the debug info.
    Handle<SharedFunctionInfo> shared = Cast<SharedFunctionInfo>(result);
    if (!EnsureBreakInfo(shared)) return false;
    PrepareFunctionForDebugExecution(shared);

    Handle<DebugInfo> debug_info(TryGetDebugInfo(*shared).value(), isolate_);
    FindBreakablePositions(debug_info, start_position, end_position, locations);
    return true;
  }

  HandleScope scope(isolate_);
  std::vector<Handle<SharedFunctionInfo>> candidates;
  if (!FindSharedFunctionInfosIntersectingRange(script, start_position,
                                                end_position, &candidates)) {
    return false;
  }
  for (const auto& candidate : candidates) {
    CHECK(candidate->HasBreakInfo(isolate_));
    Handle<DebugInfo> debug_info(TryGetDebugInfo(*candidate).value(), isolate_);
    FindBreakablePositions(debug_info, start_position, end_position, locations);
  }
  return true;
}

class SharedFunctionInfoFinder {
 public:
  explicit SharedFunctionInfoFinder(int target_position)
      : current_start_position_(kNoSourcePosition),
        target_position_(target_position) {}

  void NewCandidate(Tagged<SharedFunctionInfo> shared,
                    Tagged<JSFunction> closure = JSFunction()) {
    if (!shared->IsSubjectToDebugging()) return;
    int start_position = shared->function_token_position();
    if (start_position == kNoSourcePosition) {
      start_position = shared->StartPosition();
    }

    if (start_position > target_position_) return;
    if (target_position_ >= shared->EndPosition()) {
      // The SharedFunctionInfo::EndPosition() is generally exclusive, but there
      // are assumptions in various places in the debugger that for script level
      // (toplevel function) there's an end position that is technically outside
      // the script. It might be worth revisiting the overall design here at
      // some point in the future.
      if (!shared->is_toplevel() || target_position_ > shared->EndPosition()) {
        return;
      }
    }

    if (!current_candidate_.is_null()) {
      if (current_start_position_ == start_position &&
          shared->EndPosition() == current_candidate_->EndPosition()) {
        // If we already have a matching closure, do not throw it away.
        if (!current_candidate_closure_.is_null() && closure.is_null()) return;
        // If a top-level function contains only one function
        // declaration the source for the top-level and the function
        // is the same. In that case prefer the non top-level function.
        if (!current_candidate_->is_toplevel() && shared->is_toplevel()) return;
      } else if (start_position < current_start_position_ ||
                 current_candidate_->EndPosition() < shared->EndPosition()) {
        return;
      }
    }

    current_start_position_ = start_position;
    current_candidate_ = shared;
    current_candidate_closure_ = closure;
  }

  Tagged<SharedFunctionInfo> Result() { return current_candidate_; }

  Tagged<JSFunction> ResultClosure() { return current_candidate_closure_; }

 private:
  Tagged<SharedFunctionInfo> current_candidate_;
  Tagged<JSFunction> current_candidate_closure_;
  int current_start_position_;
  int target_position_;
  DISALLOW_GARBAGE_COLLECTION(no_gc_)
};

namespace {
Tagged<SharedFunctionInfo> FindSharedFunctionInfoCandidate(
    int position, DirectHandle<Script> script, Isolate* isolate) {
  SharedFunctionInfoFinder finder(position);
  SharedFunctionInfo::ScriptIterator iterator(isolate, *script);
  for (Tagged<SharedFunctionInfo> info = iterator.Next(); !info.is_null();
       info = iterator.Next()) {
    finder.NewCandidate(info);
  }
  return finder.Result();
}
}  // namespace

Handle<SharedFunctionInfo> Debug::FindClosestSharedFunctionInfoFromPosition(
    int position, Handle<Script> script,
    Handle<SharedFunctionInfo> outer_shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  Handle<DebugInfo> outer_debug_info(TryGetDebugInfo(*outer_shared).value(),
                                     isolate_);
  CHECK(outer_debug_info->HasBreakInfo());
  int closest_position = FindBreakablePosition(outer_debug_info, position);
  Handle<SharedFunctionInfo> closest_candidate = outer_shared;
  if (closest_position == position) return outer_shared;

  const int start_position = outer_shared->StartPosition();
  const int end_position = outer_shared->EndPosition();
  if (start_position == end_position) return outer_shared;

  if (closest_position == 0) closest_position = end_position;
  std::vector<Handle<SharedFunctionInfo>> candidates;
  // Find all shared function infos of functions that are intersecting from
  // the requested position until the end of the enclosing function.
  if (!FindSharedFunctionInfosIntersectingRange(
          script, position, closest_position, &candidates)) {
    return outer_shared;
  }

  for (auto candidate : candidates) {
    Handle<DebugInfo> debug_info(TryGetDebugInfo(*candidate).value(), isolate_);
    CHECK(debug_info->HasBreakInfo());
    const int candidate_position = FindBreakablePosition(debug_info, position);
    if (candidate_position >= position &&
        candidate_position < closest_position) {
      closest_position = candidate_position;
      closest_candidate = candidate;
    }
    if (closest_position == position) break;
  }
  return closest_candidate;
}

bool Debug::FindSharedFunctionInfosIntersectingRange(
    Handle<Script> script, int start_position, int end_position,
    std::vector<Handle<SharedFunctionInfo>>* intersecting_shared) {
  bool candidateSubsumesRange = false;
  bool triedTopLevelCompile = false;

  while (true) {
    std::vector<Handle<SharedFunctionInfo>> candidates;
    std::vector<IsCompiledScope> compiled_scopes;
    {
      DisallowGarbageCollection no_gc;
      SharedFunctionInfo::ScriptIterator iterator(isolate_, *script);
      for (Tagged<SharedFunctionInfo> info = iterator.Next(); !info.is_null();
           info = iterator.Next()) {
        if (info->EndPosition() < start_position ||
            info->StartPosition() >= end_position) {
          continue;
        }
        candidateSubsumesRange |= info->StartPosition() <= start_position &&
                                  info->EndPosition() >= end_position;
        if (!info->IsSubjectToDebugging()) continue;
        if (!info->is_compiled() && !info->allows_lazy_compilation()) continue;
        candidates.push_back(i::handle(info, isolate_));
      }
    }

    if (!triedTopLevelCompile && !candidateSubsumesRange &&
        script->infos()->length() > 0) {
      MaybeHandle<SharedFunctionInfo> shared =
          GetTopLevelWithRecompile(script, &triedTopLevelCompile);
      if (shared.is_null()) return false;
      if (triedTopLevelCompile) continue;
    }

    bool was_compiled = false;
    for (const auto& candidate : candidates) {
      IsCompiledScope is_compiled_scope(candidate->is_compiled_scope(isolate_));
      if (!is_compiled_scope.is_compiled()) {
        // InstructionStream that cannot be compiled lazily are internal and not
        // debuggable.
        DCHECK(candidate->allows_lazy_compilation());
        if (!Compiler::Compile(isolate_, candidate, Compiler::CLEAR_EXCEPTION,
                               &is_compiled_scope)) {
          return false;
        } else {
          was_compiled = true;
        }
      }
      DCHECK(is_compiled_scope.is_compiled());
      compiled_scopes.push_back(is_compiled_scope);
      if (!EnsureBreakInfo(candidate)) return false;
      PrepareFunctionForDebugExecution(candidate);
    }
    if (was_compiled) continue;
    *intersecting_shared = std::move(candidates);
    return true;
  }
  UNREACHABLE();
}

MaybeHandle<SharedFunctionInfo> Debug::GetTopLevelWithRecompile(
    Handle<Script> script, bool* did_compile) {
  DCHECK_LE(kFunctionLiteralIdTopLevel, script->infos()->length());
  Tagged<MaybeObject> maybeToplevel =
      script->infos()->get(kFunctionLiteralIdTopLevel);
  Tagged<HeapObject> heap_object;
  const bool topLevelInfoExists =
      maybeToplevel.GetHeapObject(&heap_object) && !IsUndefined(heap_object);
  if (topLevelInfoExists) {
    if (did_compile) *did_compile = false;
    return handle(Cast<SharedFunctionInfo>(heap_object), isolate_);
  }

  MaybeHandle<SharedFunctionInfo> shared;
  CompileTopLevel(isolate_, script, &shared);
  if (did_compile) *did_compile = true;
  return shared;
}

// We need to find a SFI for a literal that may not yet have been compiled yet,
// and there may not be a JSFunction referencing it. Find the SFI closest to
// the given position, compile it to reveal possible inner SFIs and repeat.
// While we are at this, also ensure code with debug break slots so that we do
// not have to compile a SFI without JSFunction, which is paifu for those that
// cannot be compiled without context (need to find outer compilable SFI etc.)
Handle<Object> Debug::FindInnermostContainingFunctionInfo(Handle<Script> script,
                                                          int position) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  for (int iteration = 0;; iteration++) {
    // Go through all shared function infos associated with this script to
    // find the innermost function containing this position.
    // If there is no shared function info for this script at all, there is
    // no point in looking for it by walking the heap.

    Tagged<SharedFunctionInfo> shared;
    IsCompiledScope is_compiled_scope;
    {
      shared = FindSharedFunctionInfoCandidate(position, script, isolate_);
      if (shared.is_null()) {
        if (iteration > 0) break;
        // It might be that the shared function info is not available as the
        // top level functions are removed due to the GC. Try to recompile
        // the top level functions.
        const bool success = CompileTopLevel(isolate_, script);
        if (!success) break;
        continue;
      }
      // We found it if it's already compiled.
      is_compiled_scope = shared->is_compiled_scope(isolate_);
      if (is_compiled_scope.is_compiled()) {
        Handle<SharedFunctionInfo> shared_handle(shared, isolate_);
        // If the iteration count is larger than 1, we had to compile the outer
        // function in order to create this shared function info. So there can
        // be no JSFunction referencing it. We can anticipate creating a debug
        // info while bypassing PrepareFunctionForDebugExecution.
        if (iteration > 1) {
          CreateBreakInfo(shared_handle);
        }
        return shared_handle;
      }
    }
    // If not, compile to reveal inner functions.
    HandleScope scope(isolate_);
    // InstructionStream that cannot be compiled lazily are internal and not
    // debuggable.
    DCHECK(shared->allows_lazy_compilation());
    if (!Compiler::Compile(isolate_, handle(shared, isolate_),
                           Compiler::CLEAR_EXCEPTION, &is_compiled_scope)) {
      break;
    }
  }
  return isolate_->factory()->undefined_value();
}

// Ensures the debug information is present for shared.
bool Debug::EnsureBreakInfo(Handle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Return if we already have the break info for shared.
  if (shared->HasBreakInfo(isolate_)) {
    DCHECK(shared->is_compiled());
    return true;
  }
  if (!shared->IsSubjectToDebugging() && !CanBreakAtEntry(shared)) {
    return false;
  }
  IsCompiledScope is_compiled_scope = shared->is_compiled_scope(isolate_);
  if (!is_compiled_scope.is_compiled() &&
      !Compiler::Compile(isolate_, shared, Compiler::CLEAR_EXCEPTION,
                         &is_compiled_scope, CreateSourcePositions::kYes)) {
    return false;
  }
  CreateBreakInfo(shared);
  return true;
}

void Debug::CreateBreakInfo(Handle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);
  DirectHandle<DebugInfo> debug_info = GetOrCreateDebugInfo(shared);

  // Initialize with break information.

  DCHECK(!debug_info->HasBreakInfo());

  Factory* factory = isolate_->factory();
  DirectHandle<FixedArray> break_points(
      factory->NewFixedArray(DebugInfo::kEstimatedNofBreakPointsInFunction));

  int flags = debug_info->flags(kRelaxedLoad);
  flags |= DebugInfo::kHasBreakInfo;
  if (CanBreakAtEntry(shared)) flags |= DebugInfo::kCanBreakAtEntry;
  debug_info->set_flags(flags, kRelaxedStore);
  debug_info->set_break_points(*break_points);

  SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate_, shared);
}

Handle<DebugInfo> Debug::GetOrCreateDebugInfo(
    DirectHandle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);

  if (std::optional<Tagged<DebugInfo>> di = debug_infos_.Find(*shared)) {
    return handle(di.value(), isolate_);
  }

  Handle<DebugInfo> debug_info = isolate_->factory()->NewDebugInfo(shared);
  debug_infos_.Insert(*shared, *debug_info);
  return debug_info;
}

void Debug::InstallCoverageInfo(DirectHandle<SharedFunctionInfo> shared,
                                Handle<CoverageInfo> coverage_info) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK(!coverage_info.is_null());

  DirectHandle<DebugInfo> debug_info = GetOrCreateDebugInfo(shared);

  DCHECK(!debug_info->HasCoverageInfo());

  debug_info->set_flags(
      debug_info->flags(kRelaxedLoad) | DebugInfo::kHasCoverageInfo,
      kRelaxedStore);
  debug_info->set_coverage_info(*coverage_info);
}

void Debug::RemoveAllCoverageInfos() {
  ClearAllDebugInfos([=, this](DirectHandle<DebugInfo> info) {
    info->ClearCoverageInfo(isolate_);
  });
}

void Debug::ClearAllDebuggerHints() {
  ClearAllDebugInfos(
      [=](DirectHandle<DebugInfo> info) { info->set_debugger_hints(0); });
}

void Debug::ClearAllDebugInfos(const DebugInfoClearFunction& clear_function) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);

  HandleScope scope(isolate_);
  DebugInfoCollection::Iterator it(&debug_infos_);
  for (; it.HasNext(); it.Advance()) {
    Handle<DebugInfo> debug_info(it.Next(), isolate_);
    clear_function(debug_info);
    if (debug_info->IsEmpty()) it.DeleteNext();
  }
}

void Debug::RemoveBreakInfoAndMaybeFree(DirectHandle<DebugInfo> debug_info) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  debug_info->ClearBreakInfo(isolate_);
  if (debug_info->IsEmpty()) {
    debug_infos_.DeleteSlow(debug_info->shared());
  }
}

bool Debug::IsBreakAtReturn(JavaScriptFrame* frame) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);

  // Get the executing function in which the debug break occurred.
  DirectHandle<SharedFunctionInfo> shared(frame->function()->shared(),
                                          isolate_);

  // With no debug info there are no break points, so we can't be at a return.
  Handle<DebugInfo> debug_info;
  if (!ToHandle(isolate_, TryGetDebugInfo(*shared), &debug_info) ||
      !debug_info->HasBreakInfo()) {
    return false;
  }

  DCHECK(!frame->is_optimized());
  BreakLocation location = BreakLocation::FromFrame(debug_info, frame);
  return location.IsReturn();
}

Handle<FixedArray> Debug::GetLoadedScripts() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  isolate_->heap()->CollectAllGarbage(GCFlag::kNoFlags,
                                      GarbageCollectionReason::kDebugger);
  Factory* factory = isolate_->factory();
  if (!IsWeakArrayList(*factory->script_list())) {
    return factory->empty_fixed_array();
  }
  auto array = Cast<WeakArrayList>(factory->script_list());
  Handle<FixedArray> results = factory->NewFixedArray(array->length());
  int length = 0;
  {
    Script::Iterator iterator(isolate_);
    for (Tagged<Script> script = iterator.Next(); !script.is_null();
         script = iterator.Next()) {
      if (script->HasValidSource()) results->set(length++, script);
    }
  }
  return FixedArray::RightTrimOrEmpty(isolate_, results, length);
}

std::optional<Tagged<DebugInfo>> Debug::TryGetDebugInfo(
    Tagged<SharedFunctionInfo> sfi) {
  return debug_infos_.Find(sfi);
}

bool Debug::HasDebugInfo(Tagged<SharedFunctionInfo> sfi) {
  return TryGetDebugInfo(sfi).has_value();
}

bool Debug::HasCoverageInfo(Tagged<SharedFunctionInfo> sfi) {
  if (std::optional<Tagged<DebugInfo>> debug_info = TryGetDebugInfo(sfi)) {
    return debug_info.value()->HasCoverageInfo();
  }
  return false;
}

bool Debug::HasBreakInfo(Tagged<SharedFunctionInfo> sfi) {
  if (std::optional<Tagged<DebugInfo>> debug_info = TryGetDebugInfo(sfi)) {
    return debug_info.value()->HasBreakInfo();
  }
  return false;
}

bool Debug::BreakAtEntry(Tagged<SharedFunctionInfo> sfi) {
  if (std::optional<Tagged<DebugInfo>> debug_info = TryGetDebugInfo(sfi)) {
    return debug_info.value()->BreakAtEntry();
  }
  return false;
}

std::optional<Tagged<Object>> Debug::OnThrow(Handle<Object> exception) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (in_debug_scope() || ignore_events()) return {};
  // Temporarily clear any exception to allow evaluating
  // JavaScript from the debug event handler.
  HandleScope scope(isolate_);
  {
    std::optional<Isolate::ExceptionScope> exception_scope;
    if (isolate_->has_exception()) exception_scope.emplace(isolate_);
    Isolate::CatchType catch_type = isolate_->PredictExceptionCatcher();
    OnException(exception, MaybeHandle<JSPromise>(),
                catch_type == Isolate::CAUGHT_BY_ASYNC_AWAIT ||
                        catch_type == Isolate::CAUGHT_BY_PROMISE
                    ? v8::debug::kPromiseRejection
                    : v8::debug::kException);
  }
  PrepareStepOnThrow();
  // If the OnException handler requested termination, then indicated this to
  // our caller Isolate::Throw so it can deal with it immediatelly instead of
  // throwing the original exception.
  if (isolate_->stack_guard()->CheckTerminateExecution()) {
    isolate_->stack_guard()->ClearTerminateExecution();
    return isolate_->TerminateExecution();
  }
  return {};
}

void Debug::OnPromiseReject(Handle<Object> promise, Handle<Object> value) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (in_debug_scope() || ignore_events()) return;
  MaybeHandle<JSPromise> maybe_promise;
  if (IsJSPromise(*promise)) {
    Handle<JSPromise> js_promise = Cast<JSPromise>(promise);
    if (js_promise->is_silent()) {
      return;
    }
    maybe_promise = js_promise;
  }
  OnException(value, maybe_promise, v8::debug::kPromiseRejection);
}

bool Debug::IsFrameBlackboxed(JavaScriptFrame* frame) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);
  std::vector<Handle<SharedFunctionInfo>> infos;
  frame->GetFunctions(&infos);
  for (const auto& info : infos) {
    if (!IsBlackboxed(info)) return false;
  }
  return true;
}

void Debug::OnException(Handle<Object> exception,
                        MaybeHandle<JSPromise> promise,
                        v8::debug::ExceptionType exception_type) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Do not trigger exception event on stack overflow. We cannot perform
  // anything useful for debugging in that situation.
  StackLimitCheck stack_limit_check(isolate_);
  if (stack_limit_check.JsHasOverflowed()) return;

  // Return if the event has nowhere to go.
  if (!debug_delegate_) return;

  // Return if we are not interested in exception events.
  if (!break_on_caught_exception_ && !break_on_uncaught_exception_) return;

  HandleScope scope(isolate_);

  bool all_frames_ignored = true;
  bool is_debuggable = false;
  bool uncaught = !isolate_->WalkCallStackAndPromiseTree(
      promise, [this, &all_frames_ignored,
                &is_debuggable](Isolate::PromiseHandler handler) {
        if (!handler.async) {
          is_debuggable = true;
        } else if (!is_debuggable) {
          // Don't bother checking ignore listing if there are no debuggable
          // frames on the callstack
          return;
        }
        all_frames_ignored =
            all_frames_ignored &&
            IsBlackboxed(handle(handler.function_info, isolate_));
      });

  if (all_frames_ignored || !is_debuggable) {
    return;
  }

  if (!uncaught) {
    if (!break_on_caught_exception_) {
      return;
    }
  } else {
    if (!break_on_uncaught_exception_) {
      return;
    }
  }

  {
    StackFrameIterator it(isolate_);
    for (; !it.done(); it.Advance()) {
      if (it.frame()->is_java_script()) {
        JavaScriptFrame* frame = JavaScriptFrame::cast(it.frame());
        FrameSummary summary = FrameSummary::GetTop(frame);
        DirectHandle<SharedFunctionInfo> shared{
            summary.AsJavaScript().function()->shared(), isolate_};
        if (shared->IsSubjectToDebugging()) {
          Handle<DebugInfo> debug_info;
          std::vector<BreakLocation> break_locations;
          if (ToHandle(isolate_, TryGetDebugInfo(*shared), &debug_info) &&
              debug_info->HasBreakInfo()) {
            // Enter the debugger.
            DebugScope debug_scope(this);
            BreakLocation::AllAtCurrentStatement(debug_info, frame,
                                                 &break_locations);
          }
          if (IsMutedAtAnyBreakLocation(shared, break_locations)) {
            return;
          }
          break;  // Stop at first debuggable function
        }
      }
#if V8_ENABLE_WEBASSEMBLY
      else if (it.frame()->is_wasm()) {
        const WasmFrame* frame = WasmFrame::cast(it.frame());
        if (IsMutedAtWasmLocation(frame->script(), frame->position())) {
          return;
        }
        // Wasm is always subject to debugging
        break;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    }

    if (it.done()) return;  // Do not trigger an event with an empty stack.
  }

  DebugScope debug_scope(this);
  DisableBreak no_recursive_break(this);

  {
    Handle<Object> promise_object;
    if (!promise.ToHandle(&promise_object)) {
      promise_object = isolate_->factory()->undefined_value();
    }
    RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebuggerCallback);
    debug_delegate_->ExceptionThrown(
        v8::Utils::ToLocal(isolate_->native_context()),
        v8::Utils::ToLocal(exception), v8::Utils::ToLocal(promise_object),
        uncaught, exception_type);
  }
}

void Debug::OnDebugBreak(Handle<FixedArray> break_points_hit,
                         StepAction lastStepAction,
                         v8::debug::BreakReasons break_reasons) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK(!break_points_hit.is_null());
  // The caller provided for DebugScope.
  AssertDebugContext();
  // Bail out if there is no listener for this event
  if (ignore_events()) return;

#ifdef DEBUG
  PrintBreakLocation();
#endif  // DEBUG

  if (!debug_delegate_) return;
  DCHECK(in_debug_scope());
  HandleScope scope(isolate_);
  DisableBreak no_recursive_break(this);

  if ((lastStepAction == StepAction::StepOver ||
       lastStepAction == StepAction::StepInto) &&
      ShouldBeSkipped()) {
    PrepareStep(lastStepAction);
    return;
  }

  std::vector<int> inspector_break_points_hit;
  // This array contains breakpoints installed using JS debug API.
  for (int i = 0; i < break_points_hit->length(); ++i) {
    Tagged<BreakPoint> break_point = Cast<BreakPoint>(break_points_hit->get(i));
    inspector_break_points_hit.push_back(break_point->id());
  }
  {
    RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebuggerCallback);
    if (lastStepAction != StepAction::StepNone)
      break_reasons.Add(debug::BreakReason::kStep);
    debug_delegate_->BreakProgramRequested(
        v8::Utils::ToLocal(isolate_->native_context()),
        inspector_break_points_hit, break_reasons);
  }
}

namespace {
debug::Location GetDebugLocation(DirectHandle<Script> script,
                                 int source_position) {
  Script::PositionInfo info;
  Script::GetPositionInfo(script, source_position, &info);
  // V8 provides ScriptCompiler::CompileFunction method which takes
  // expression and compile it as anonymous function like (function() ..
  // expression ..). To produce correct locations for stmts inside of this
  // expression V8 compile this function with negative offset. Instead of stmt
  // position blackboxing use function start position which is negative in
  // described case.
  return debug::Location(std::max(info.line, 0), std::max(info.column, 0));
}
}  // namespace

bool Debug::IsBlackboxed(DirectHandle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  if (!debug_delegate_) return !shared->IsSubjectToDebugging();
  DirectHandle<DebugInfo> debug_info = GetOrCreateDebugInfo(shared);
  if (!debug_info->computed_debug_is_blackboxed()) {
    bool is_blackboxed =
        !shared->IsSubjectToDebugging() || !IsScript(shared->script());
    if (!is_blackboxed) {
      SuppressDebug while_processing(this);
      HandleScope handle_scope(isolate_);
      PostponeInterruptsScope no_interrupts(isolate_);
      DisableBreak no_recursive_break(this);
      DCHECK(IsScript(shared->script()));
      DirectHandle<Script> script(Cast<Script>(shared->script()), isolate_);
      DCHECK(script->IsUserJavaScript());
      debug::Location start = GetDebugLocation(script, shared->StartPosition());
      debug::Location end = GetDebugLocation(script, shared->EndPosition());
      {
        RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebuggerCallback);
        is_blackboxed = debug_delegate_->IsFunctionBlackboxed(
            ToApiHandle<debug::Script>(script), start, end);
      }
    }
    debug_info->set_debug_is_blackboxed(is_blackboxed);
    debug_info->set_computed_debug_is_blackboxed(true);
  }
  return debug_info->debug_is_blackboxed();
}

bool Debug::ShouldBeSkipped() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  SuppressDebug while_processing(this);
  PostponeInterruptsScope no_interrupts(isolate_);
  DisableBreak no_recursive_break(this);

  DebuggableStackFrameIterator iterator(isolate_);
  FrameSummary summary = iterator.GetTopValidFrame();
  Handle<Object> script_obj = summary.script();
  if (!IsScript(*script_obj)) return false;

  DirectHandle<Script> script = Cast<Script>(script_obj);
  summary.EnsureSourcePositionsAvailable();
  int source_position = summary.SourcePosition();
  Script::PositionInfo info;
  Script::GetPositionInfo(script, source_position, &info);
  {
    RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebuggerCallback);
    return debug_delegate_->ShouldBeSkipped(ToApiHandle<debug::Script>(script),
                                            info.line, info.column);
  }
}

bool Debug::AllFramesOnStackAreBlackboxed() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);

  for (StackFrameIterator it(isolate_); !it.done(); it.Advance()) {
    StackFrame* frame = it.frame();
    if (frame->is_java_script() &&
        !IsFrameBlackboxed(JavaScriptFrame::cast(frame))) {
      return false;
    }
  }
  return true;
}

bool Debug::CanBreakAtEntry(DirectHandle<SharedFunctionInfo> shared) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Allow break at entry for builtin functions.
  if (shared->native() || shared->IsApiFunction()) {
    // Functions that are subject to debugging can have regular breakpoints.
    DCHECK(!shared->IsSubjectToDebugging());
    return true;
  }
  return false;
}

bool Debug::SetScriptSource(Handle<Script> script, Handle<String> source,
                            bool preview, bool allow_top_frame_live_editing,
                            debug::LiveEditResult* result) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DebugScope debug_scope(this);
  running_live_edit_ = true;
  LiveEdit::PatchScript(isolate_, script, source, preview,
                        allow_top_frame_live_editing, result);
  running_live_edit_ = false;
  return result->status == debug::LiveEditResult::OK;
}

void Debug::OnCompileError(DirectHandle<Script> script) {
  ProcessCompileEvent(true, script);
}

void Debug::OnAfterCompile(DirectHandle<Script> script) {
  ProcessCompileEvent(false, script);
}

void Debug::ProcessCompileEvent(bool has_compile_error,
                                DirectHandle<Script> script) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Ignore temporary scripts.
  if (script->id() == Script::kTemporaryScriptId) return;
  // TODO(kozyatinskiy): teach devtools to work with liveedit scripts better
  // first and then remove this fast return.
  if (running_live_edit_) return;
  // Attach the correct debug id to the script. The debug id is used by the
  // inspector to filter scripts by native context.
  script->set_context_data(isolate_->native_context()->debug_context_id());
  if (ignore_events()) return;
  if (!script->IsSubjectToDebugging()) return;
  if (!debug_delegate_) return;
  SuppressDebug while_processing(this);
  DebugScope debug_scope(this);
  HandleScope scope(isolate_);
  DisableBreak no_recursive_break(this);
  AllowJavascriptExecution allow_script(isolate_);
  {
    RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebuggerCallback);
    debug_delegate_->ScriptCompiled(ToApiHandle<debug::Script>(script),
                                    running_live_edit_, has_compile_error);
  }
}

int Debug::CurrentFrameCount() {
  DebuggableStackFrameIterator it(isolate_);
  if (break_frame_id() != StackFrameId::NO_ID) {
    // Skip to break frame.
    DCHECK(in_debug_scope());
    while (!it.done() && it.frame()->id() != break_frame_id()) it.Advance();
  }
  int counter = 0;
  for (; !it.done(); it.Advance()) {
    counter += it.FrameFunctionCount();
  }
  return counter;
}

void Debug::SetDebugDelegate(debug::DebugDelegate* delegate) {
  debug_delegate_ = delegate;
  UpdateState();
}

void Debug::UpdateState() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  bool is_active = debug_delegate_ != nullptr;
  if (is_active == is_active_) return;
  if (is_active) {
    // Note that the debug context could have already been loaded to
    // bootstrap test cases.
    isolate_->compilation_cache()->DisableScriptAndEval();
    isolate_->CollectSourcePositionsForAllBytecodeArrays();
    is_active = true;
  } else {
    isolate_->compilation_cache()->EnableScriptAndEval();
    Unload();
  }
  is_active_ = is_active;
  isolate_->PromiseHookStateUpdated();
}

void Debug::UpdateHookOnFunctionCall() {
  static_assert(LastStepAction == StepInto);
  hook_on_function_call_ =
      thread_local_.last_step_action_ == StepInto ||
      isolate_->debug_execution_mode() == DebugInfo::kSideEffects ||
      thread_local_.break_on_next_function_call_;
}

void Debug::HandleDebugBreak(IgnoreBreakMode ignore_break_mode,
                             v8::debug::BreakReasons break_reasons) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Ignore debug break during bootstrapping.
  if (isolate_->bootstrapper()->IsActive()) return;
  // Just continue if breaks are disabled.
  if (break_disabled()) return;
  // Ignore debug break if debugger is not active.
  if (!is_active()) return;

  StackLimitCheck check(isolate_);
  if (check.HasOverflowed()) return;

  HandleScope scope(isolate_);
  MaybeHandle<FixedArray> break_points;
  {
    DebuggableStackFrameIterator it(isolate_);
    DCHECK(!it.done());
    JavaScriptFrame* frame = it.frame()->is_java_script()
                                 ? JavaScriptFrame::cast(it.frame())
                                 : nullptr;
    if (frame && IsJSFunction(frame->function())) {
      DirectHandle<JSFunction> function(frame->function(), isolate_);
      DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate_);

      // kScheduled breaks are triggered by the stack check. While we could
      // pause here, the JSFunction didn't have time yet to create and push
      // it's context. Instead, we step into the function and pause at the
      // first official breakable position.
      // This behavior mirrors "BreakOnNextFunctionCall".
      if (break_reasons.contains(v8::debug::BreakReason::kScheduled) &&
          BreakLocation::IsPausedInJsFunctionEntry(frame)) {
        thread_local_.scheduled_break_on_next_function_call_ = true;
        PrepareStepIn(function);
        return;
      }

      // Don't stop in builtin and blackboxed functions.
      bool ignore_break = ignore_break_mode == kIgnoreIfTopFrameBlackboxed
                              ? IsBlackboxed(shared)
                              : AllFramesOnStackAreBlackboxed();
      if (ignore_break) return;
      Handle<DebugInfo> debug_info;
      if (ToHandle(isolate_, TryGetDebugInfo(*shared), &debug_info) &&
          debug_info->HasBreakInfo()) {
        // Enter the debugger.
        DebugScope debug_scope(this);

        std::vector<BreakLocation> break_locations;
        BreakLocation::AllAtCurrentStatement(debug_info, frame,
                                             &break_locations);

        if (IsMutedAtAnyBreakLocation(shared, break_locations)) {
          // If we get to this point, a break was triggered because e.g. of
          // a debugger statement, an assert, .. . However, we do not stop
          // if this position "is muted", which happens if a conditional
          // breakpoint at this point evaluated to false.
          return;
        }
      }
    }
  }

  StepAction lastStepAction = last_step_action();

  // Clear stepping to avoid duplicate breaks.
  ClearStepping();

  DebugScope debug_scope(this);
  OnDebugBreak(break_points.is_null() ? isolate_->factory()->empty_fixed_array()
                                      : break_points.ToHandleChecked(),
               lastStepAction, break_reasons);
}

#ifdef DEBUG
void Debug::PrintBreakLocation() {
  if (!v8_flags.print_break_location) return;
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  HandleScope scope(isolate_);
  DebuggableStackFrameIterator iterator(isolate_);
  if (iterator.done()) return;
  CommonFrame* frame = iterator.frame();
  std::vector<FrameSummary> frames;
  frame->Summarize(&frames);
  int inlined_frame_index = static_cast<int>(frames.size() - 1);
  FrameInspector inspector(frame, inlined_frame_index, isolate_);
  int source_position = inspector.GetSourcePosition();
  Handle<Object> script_obj = inspector.GetScript();
  PrintF("[debug] break in function '");
  inspector.GetFunctionName()->PrintOn(stdout);
  PrintF("'.\n");
  if (IsScript(*script_obj)) {
    DirectHandle<Script> script = Cast<Script>(script_obj);
    DirectHandle<String> source(Cast<String>(script->source()), isolate_);
    Script::InitLineEnds(isolate_, script);
    Script::PositionInfo info;
    Script::GetPositionInfo(script, source_position, &info,
                            Script::OffsetFlag::kNoOffset);
    int line = info.line;
    int column = info.column;
    DirectHandle<FixedArray> line_ends(Cast<FixedArray>(script->line_ends()),
                                       isolate_);
    int line_start = line == 0 ? 0 : Smi::ToInt(line_ends->get(line - 1)) + 1;
    int line_end = Smi::ToInt(line_ends->get(line));
    DisallowGarbageCollection no_gc;
    String::FlatContent content = source->GetFlatContent(no_gc);
    if (content.IsOneByte()) {
      PrintF("[debug] %.*s\n", line_end - line_start,
             content.ToOneByteVector().begin() + line_start);
      PrintF("[debug] ");
      for (int i = 0; i < column; i++) PrintF(" ");
      PrintF("^\n");
    } else {
      PrintF("[debug] at line %d column %d\n", line, column);
    }
  }
}
#endif  // DEBUG

DebugScope::DebugScope(Debug* debug)
    : debug_(debug),
      prev_(reinterpret_cast<DebugScope*>(
          base::Relaxed_Load(&debug->thread_local_.current_debug_scope_))),
      no_interrupts_(debug_->isolate_) {
  timer_.Start();
  // Link recursive debugger entry.
  base::Relaxed_Store(&debug_->thread_local_.current_debug_scope_,
                      reinterpret_cast<base::AtomicWord>(this));
  // Store the previous frame id and return value.
  break_frame_id_ = debug_->break_frame_id();

  // Create the new break info. If there is no proper frames there is no break
  // frame id.
  DebuggableStackFrameIterator it(isolate());
  bool has_frames = !it.done();
  debug_->thread_local_.break_frame_id_ =
      has_frames ? it.frame()->id() : StackFrameId::NO_ID;

  debug_->UpdateState();
}

void DebugScope::set_terminate_on_resume() { terminate_on_resume_ = true; }

base::TimeDelta DebugScope::ElapsedTimeSinceCreation() {
  return timer_.Elapsed();
}

DebugScope::~DebugScope() {
  // Terminate on resume must have been handled by retrieving it, if this is
  // the outer scope.
  if (terminate_on_resume_) {
    if (!prev_) {
      debug_->isolate_->stack_guard()->RequestTerminateExecution();
    } else {
      prev_->set_terminate_on_resume();
    }
  }
  // Leaving this debugger entry.
  base::Relaxed_Store(&debug_->thread_local_.current_debug_scope_,
                      reinterpret_cast<base::AtomicWord>(prev_));

  // Restore to the previous break state.
  debug_->thread_local_.break_frame_id_ = break_frame_id_;

  debug_->UpdateState();
}

ReturnValueScope::ReturnValueScope(Debug* debug) : debug_(debug) {
  return_value_ = debug_->return_value_handle();
}

ReturnValueScope::~ReturnValueScope() {
  debug_->set_return_value(*return_value_);
}

void Debug::UpdateDebugInfosForExecutionMode() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  // Walk all debug infos and update their execution mode if it is different
  // from the isolate execution mode.
  const DebugInfo::ExecutionMode current_debug_execution_mode =
      isolate_->debug_execution_mode();

  HandleScope scope(isolate_);
  DebugInfoCollection::Iterator it(&debug_infos_);
  for (; it.HasNext(); it.Advance()) {
    Handle<DebugInfo> debug_info(it.Next(), isolate_);
    if (debug_info->HasInstrumentedBytecodeArray() &&
        debug_info->DebugExecutionMode() != current_debug_execution_mode) {
      DCHECK(debug_info->shared()->HasBytecodeArray());
      if (current_debug_execution_mode == DebugInfo::kBreakpoints) {
        ClearSideEffectChecks(debug_info);
        ApplyBreakPoints(debug_info);
      } else {
        ClearBreakPoints(debug_info);
        ApplySideEffectChecks(debug_info);
      }
    }
  }
}

void Debug::SetTerminateOnResume() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DebugScope* scope = reinterpret_cast<DebugScope*>(
      base::Acquire_Load(&thread_local_.current_debug_scope_));
  CHECK_NOT_NULL(scope);
  scope->set_terminate_on_resume();
}

void Debug::StartSideEffectCheckMode() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kBreakpoints);
  isolate_->set_debug_execution_mode(DebugInfo::kSideEffects);
  UpdateHookOnFunctionCall();
  side_effect_check_failed_ = false;

  DCHECK(!temporary_objects_);
  temporary_objects_.reset(new TemporaryObjectsTracker());
  isolate_->heap()->AddHeapObjectAllocationTracker(temporary_objects_.get());

  DirectHandle<RegExpMatchInfo> current_match_info(
      isolate_->native_context()->regexp_last_match_info(), isolate_);
  int register_count = current_match_info->number_of_capture_registers();
  regexp_match_info_ = RegExpMatchInfo::New(
      isolate_, JSRegExp::CaptureCountForRegisters(register_count));
  DCHECK_EQ(regexp_match_info_->number_of_capture_registers(),
            current_match_info->number_of_capture_registers());
  regexp_match_info_->set_last_subject(current_match_info->last_subject());
  regexp_match_info_->set_last_input(current_match_info->last_input());
  RegExpMatchInfo::CopyElements(isolate_, *regexp_match_info_, 0,
                                *current_match_info, 0, register_count,
                                SKIP_WRITE_BARRIER);

  // Update debug infos to have correct execution mode.
  UpdateDebugInfosForExecutionMode();
}

void Debug::StopSideEffectCheckMode() {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kSideEffects);
  if (side_effect_check_failed_) {
    DCHECK(isolate_->has_exception());
    DCHECK_IMPLIES(v8_flags.strict_termination_checks,
                   isolate_->is_execution_terminating());
    // Convert the termination exception into a regular exception.
    isolate_->CancelTerminateExecution();
    isolate_->Throw(*isolate_->factory()->NewEvalError(
        MessageTemplate::kNoSideEffectDebugEvaluate));
  }
  isolate_->set_debug_execution_mode(DebugInfo::kBreakpoints);
  UpdateHookOnFunctionCall();
  side_effect_check_failed_ = false;

  DCHECK(temporary_objects_);
  isolate_->heap()->RemoveHeapObjectAllocationTracker(temporary_objects_.get());
  temporary_objects_.reset();
  isolate_->native_context()->set_regexp_last_match_info(*regexp_match_info_);
  regexp_match_info_ = Handle<RegExpMatchInfo>::null();

  // Update debug infos to have correct execution mode.
  UpdateDebugInfosForExecutionMode();
}

void Debug::ApplySideEffectChecks(DirectHandle<DebugInfo> debug_info) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK(debug_info->HasInstrumentedBytecodeArray());
  Handle<BytecodeArray> debug_bytecode(debug_info->DebugBytecodeArray(isolate_),
                                       isolate_);
  DebugEvaluate::ApplySideEffectChecks(debug_bytecode);
  debug_info->SetDebugExecutionMode(DebugInfo::kSideEffects);
}

void Debug::ClearSideEffectChecks(DirectHandle<DebugInfo> debug_info) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK(debug_info->HasInstrumentedBytecodeArray());
  Handle<BytecodeArray> debug_bytecode(debug_info->DebugBytecodeArray(isolate_),
                                       isolate_);
  DirectHandle<BytecodeArray> original(
      debug_info->OriginalBytecodeArray(isolate_), isolate_);
  for (interpreter::BytecodeArrayIterator it(debug_bytecode); !it.done();
       it.Advance()) {
    // Restore from original. This may copy only the scaling prefix, which is
    // correct, since we patch scaling prefixes to debug breaks if exists.
    debug_bytecode->set(it.current_offset(),
                        original->get(it.current_offset()));
  }
}

bool Debug::PerformSideEffectCheck(Handle<JSFunction> function,
                                   Handle<Object> receiver) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kSideEffects);
  DisallowJavascriptExecution no_js(isolate_);
  IsCompiledScope is_compiled_scope(
      function->shared()->is_compiled_scope(isolate_));
  if (!function->is_compiled(isolate_) &&
      !Compiler::Compile(isolate_, function, Compiler::KEEP_EXCEPTION,
                         &is_compiled_scope)) {
    return false;
  }
  DCHECK(is_compiled_scope.is_compiled());
  DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate_);
  DirectHandle<DebugInfo> debug_info = GetOrCreateDebugInfo(shared);
  DebugInfo::SideEffectState side_effect_state =
      debug_info->GetSideEffectState(isolate_);
  if (shared->HasBuiltinId()) {
    PrepareBuiltinForSideEffectCheck(isolate_, shared->builtin_id());
  }
  switch (side_effect_state) {
    case DebugInfo::kHasSideEffects:
      if (v8_flags.trace_side_effect_free_debug_evaluate) {
        PrintF("[debug-evaluate] Function %s failed side effect check.\n",
               function->shared()->DebugNameCStr().get());
      }
      side_effect_check_failed_ = true;
      // Throw an uncatchable termination exception.
      isolate_->TerminateExecution();
      return false;
    case DebugInfo::kRequiresRuntimeChecks: {
      if (!shared->HasBytecodeArray()) {
        return PerformSideEffectCheckForObject(receiver);
      }
      // If function has bytecode array then prepare function for debug
      // execution to perform runtime side effect checks.
      DCHECK(shared->is_compiled());
      PrepareFunctionForDebugExecution(shared);
      ApplySideEffectChecks(debug_info);
      return true;
    }
    case DebugInfo::kHasNoSideEffect:
      return true;
    case DebugInfo::kNotComputed:
    default:
      UNREACHABLE();
  }
}

Handle<Object> Debug::return_value_handle() {
  return handle(thread_local_.return_value_, isolate_);
}

void Debug::PrepareBuiltinForSideEffectCheck(Isolate* isolate, Builtin id) {
  switch (id) {
    case Builtin::kStringPrototypeMatch:
    case Builtin::kStringPrototypeSearch:
    case Builtin::kStringPrototypeSplit:
    case Builtin::kStringPrototypeMatchAll:
    case Builtin::kStringPrototypeReplace:
    case Builtin::kStringPrototypeReplaceAll:
      if (Protectors::IsRegExpSpeciesLookupChainIntact(isolate_)) {
        // Force RegExps to go slow path so that we have a chance to perform
        // side-effect checks for the functions for Symbol.match,
        // Symbol.matchAll, Symbol.search, Symbol.split and Symbol.replace.
        if (v8_flags.trace_side_effect_free_debug_evaluate) {
          PrintF("[debug-evaluate] invalidating protector cell for RegExps\n");
        }
        Protectors::InvalidateRegExpSpeciesLookupChain(isolate_);
      }
      return;
    default:
      return;
  }
}

bool Debug::PerformSideEffectCheckForAccessor(
    DirectHandle<AccessorInfo> accessor_info, Handle<Object> receiver,
    AccessorComponent component) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kSideEffects);

  // List of allowlisted internal accessors can be found in accessors.h.
  SideEffectType side_effect_type =
      component == AccessorComponent::ACCESSOR_SETTER
          ? accessor_info->setter_side_effect_type()
          : accessor_info->getter_side_effect_type();

  switch (side_effect_type) {
    case SideEffectType::kHasNoSideEffect:
      // We do not support setter accessors with no side effects, since
      // calling set accessors go through a store bytecode. Store bytecodes
      // are considered to cause side effects (to non-temporary objects).
      DCHECK_NE(AccessorComponent::ACCESSOR_SETTER, component);
      return true;

    case SideEffectType::kHasSideEffectToReceiver:
      DCHECK(!receiver.is_null());
      if (PerformSideEffectCheckForObject(receiver)) return true;
      return false;

    case SideEffectType::kHasSideEffect:
      break;
  }
  if (v8_flags.trace_side_effect_free_debug_evaluate) {
    PrintF("[debug-evaluate] API Callback '");
    ShortPrint(accessor_info->name());
    PrintF("' may cause side effect.\n");
  }

  side_effect_check_failed_ = true;
  // Throw an uncatchable termination exception.
  isolate_->TerminateExecution();
  return false;
}

void Debug::IgnoreSideEffectsOnNextCallTo(
    Handle<FunctionTemplateInfo> function) {
  DCHECK(function->has_side_effects());
  // There must be only one such call handler info.
  CHECK(ignore_side_effects_for_function_template_info_.is_null());
  ignore_side_effects_for_function_template_info_ = function;
}

bool Debug::PerformSideEffectCheckForCallback(
    Handle<FunctionTemplateInfo> function) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kSideEffects);

  // If an empty |function| handle is passed here then it means that
  // the callback IS side-effectful (see CallApiCallbackWithSideEffects
  // builtin).
  if (!function.is_null() && !function->has_side_effects()) {
    return true;
  }
  if (!ignore_side_effects_for_function_template_info_.is_null()) {
    // If the |ignore_side_effects_for_function_template_info_| is set then
    // the next API callback call must be made to this function.
    CHECK(ignore_side_effects_for_function_template_info_.is_identical_to(
        function));
    ignore_side_effects_for_function_template_info_ = {};
    return true;
  }

  if (v8_flags.trace_side_effect_free_debug_evaluate) {
    PrintF("[debug-evaluate] FunctionTemplateInfo may cause side effect.\n");
  }

  side_effect_check_failed_ = true;
  // Throw an uncatchable termination exception.
  isolate_->TerminateExecution();
  return false;
}

bool Debug::PerformSideEffectCheckForInterceptor(
    Handle<InterceptorInfo> interceptor_info) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kSideEffects);

  // Empty InterceptorInfo represents operations that do produce side effects.
  if (!interceptor_info.is_null()) {
    if (interceptor_info->has_no_side_effect()) return true;
  }
  if (v8_flags.trace_side_effect_free_debug_evaluate) {
    PrintF("[debug-evaluate] API Interceptor may cause side effect.\n");
  }

  side_effect_check_failed_ = true;
  // Throw an uncatchable termination exception.
  isolate_->TerminateExecution();
  return false;
}

bool Debug::PerformSideEffectCheckAtBytecode(InterpretedFrame* frame) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  using interpreter::Bytecode;

  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kSideEffects);
  Tagged<SharedFunctionInfo> shared = frame->function()->shared();
  Tagged<BytecodeArray> bytecode_array = shared->GetBytecodeArray(isolate_);
  int offset = frame->GetBytecodeOffset();
  interpreter::BytecodeArrayIterator bytecode_iterator(
      handle(bytecode_array, isolate_), offset);

  Bytecode bytecode = bytecode_iterator.current_bytecode();
  if (interpreter::Bytecodes::IsCallRuntime(bytecode)) {
    auto id = (bytecode == Bytecode::kInvokeIntrinsic)
                  ? bytecode_iterator.GetIntrinsicIdOperand(0)
                  : bytecode_iterator.GetRuntimeIdOperand(0);
    if (DebugEvaluate::IsSideEffectFreeIntrinsic(id)) {
      return true;
    }
    side_effect_check_failed_ = true;
    // Throw an uncatchable termination exception.
    isolate_->TerminateExecution();
    return false;
  }
  interpreter::Register reg;
  switch (bytecode) {
    case Bytecode::kStaCurrentContextSlot:
      reg = interpreter::Register::current_context();
      break;
    default:
      reg = bytecode_iterator.GetRegisterOperand(0);
      break;
  }
  Handle<Object> object =
      handle(frame->ReadInterpreterRegister(reg.index()), isolate_);
  return PerformSideEffectCheckForObject(object);
}

bool Debug::PerformSideEffectCheckForObject(Handle<Object> object) {
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kDebugger);
  DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kSideEffects);

  // We expect no side-effects for primitives.
  if (IsNumber(*object)) return true;
  if (IsName(*object)) return true;

  if (temporary_objects_->HasObject(Cast<HeapObject>(object))) {
    return true;
  }

  if (v8_flags.trace_side_effect_free_debug_evaluate) {
    PrintF("[debug-evaluate] failed runtime side effect check.\n");
  }
  side_effect_check_failed_ = true;
  // Throw an uncatchable termination exception.
  isolate_->TerminateExecution();
  return false;
}

void Debug::SetTemporaryObjectTrackingDisabled(bool disabled) {
  if (temporary_objects_) {
    temporary_objects_->disabled = disabled;
  }
}

bool Debug::GetTemporaryObjectTrackingDisabled() const {
  if (temporary_objects_) {
    return temporary_objects_->disabled;
  }
  return false;
}

void Debug::PrepareRestartFrame(JavaScriptFrame* frame,
                                int inlined_frame_index) {
  if (frame->is_optimized()) Deoptimizer::DeoptimizeFunction(frame->function());

  thread_local_.restart_frame_id_ = frame->id();
  thread_local_.restart_inline_frame_index_ = inlined_frame_index;

  // TODO(crbug.com/1303521): A full "StepInto" is probably not needed. Get the
  // necessary bits out of PrepareSTep into a separate method or fold them
  // into Debug::PrepareRestartFrame.
  PrepareStep(StepInto);
}

void Debug::NotifyDebuggerPausedEventSent() {
  DebugScope* scope = reinterpret_cast<DebugScope*>(
      base::Relaxed_Load(&thread_local_.current_debug_scope_));
  CHECK(scope);
  isolate_->counters()->debug_pause_to_paused_event()->AddTimedSample(
      scope->ElapsedTimeSinceCreation());
}

}  // namespace internal
}  // namespace v8
