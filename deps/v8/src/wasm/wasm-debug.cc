// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-debug.h"

#include <iomanip>
#include <unordered_map>

#include "src/common/assert-scope.h"
#include "src/common/simd128.h"
#include "src/compiler/wasm-compiler.h"
#include "src/debug/debug-evaluate.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/heap/factory.h"
#include "src/wasm/baseline/liftoff-compiler.h"
#include "src/wasm/baseline/liftoff-register.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/wasm/wasm-value.h"
#include "src/zone/accounting-allocator.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

using ImportExportKey = std::pair<ImportExportKindCode, uint32_t>;

enum ReturnLocation { kAfterBreakpoint, kAfterWasmCall };

Address FindNewPC(WasmFrame* frame, WasmCode* wasm_code, int byte_offset,
                  ReturnLocation return_location) {
  base::Vector<const uint8_t> new_pos_table = wasm_code->source_positions();

  DCHECK_LE(0, byte_offset);

  // Find the size of the call instruction by computing the distance from the
  // source position entry to the return address.
  WasmCode* old_code = frame->wasm_code();
  int pc_offset = static_cast<int>(frame->pc() - old_code->instruction_start());
  base::Vector<const uint8_t> old_pos_table = old_code->source_positions();
  SourcePositionTableIterator old_it(old_pos_table);
  int call_offset = -1;
  while (!old_it.done() && old_it.code_offset() < pc_offset) {
    call_offset = old_it.code_offset();
    old_it.Advance();
  }
  DCHECK_LE(0, call_offset);
  int call_instruction_size = pc_offset - call_offset;

  // If {return_location == kAfterBreakpoint} we search for the first code
  // offset which is marked as instruction (i.e. not the breakpoint).
  // If {return_location == kAfterWasmCall} we return the last code offset
  // associated with the byte offset.
  SourcePositionTableIterator it(new_pos_table);
  while (!it.done() && it.source_position().ScriptOffset() != byte_offset) {
    it.Advance();
  }
  if (return_location == kAfterBreakpoint) {
    while (!it.is_statement()) it.Advance();
    DCHECK_EQ(byte_offset, it.source_position().ScriptOffset());
    return wasm_code->instruction_start() + it.code_offset() +
           call_instruction_size;
  }

  DCHECK_EQ(kAfterWasmCall, return_location);
  int code_offset;
  do {
    code_offset = it.code_offset();
    it.Advance();
  } while (!it.done() && it.source_position().ScriptOffset() == byte_offset);
  return wasm_code->instruction_start() + code_offset + call_instruction_size;
}

}  // namespace

void DebugSideTable::Print(std::ostream& os) const {
  os << "Debug side table (" << num_locals_ << " locals, " << entries_.size()
     << " entries):\n";
  for (auto& entry : entries_) entry.Print(os);
  os << "\n";
}

void DebugSideTable::Entry::Print(std::ostream& os) const {
  os << std::setw(6) << std::hex << pc_offset_ << std::dec << " stack height "
     << stack_height_ << " [";
  for (auto& value : changed_values_) {
    os << " " << value.type.name() << ":";
    switch (value.storage) {
      case kConstant:
        os << "const#" << value.i32_const;
        break;
      case kRegister:
        os << "reg#" << value.reg_code;
        break;
      case kStack:
        os << "stack#" << value.stack_offset;
        break;
    }
  }
  os << " ]\n";
}

size_t DebugSideTable::Entry::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(DebugSideTable::Entry, 32);
  return ContentSize(changed_values_);
}

size_t DebugSideTable::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(DebugSideTable, 32);
  size_t result = sizeof(DebugSideTable) + ContentSize(entries_);
  for (const Entry& entry : entries_) {
    result += entry.EstimateCurrentMemoryConsumption();
  }
  return result;
}

class DebugInfoImpl {
 public:
  explicit DebugInfoImpl(NativeModule* native_module)
      : native_module_(native_module) {}

  DebugInfoImpl(const DebugInfoImpl&) = delete;
  DebugInfoImpl& operator=(const DebugInfoImpl&) = delete;

  int GetNumLocals(Address pc, Isolate* isolate) {
    FrameInspectionScope scope(this, pc, isolate);
    if (!scope.is_inspectable()) return 0;
    return scope.debug_side_table->num_locals();
  }

  WasmValue GetLocalValue(int local, Address pc, Address fp,
                          Address debug_break_fp, Isolate* isolate) {
    FrameInspectionScope scope(this, pc, isolate);
    return GetValue(scope.debug_side_table, scope.debug_side_table_entry, local,
                    fp, debug_break_fp, isolate);
  }

  int GetStackDepth(Address pc, Isolate* isolate) {
    FrameInspectionScope scope(this, pc, isolate);
    if (!scope.is_inspectable()) return 0;
    int num_locals = scope.debug_side_table->num_locals();
    int stack_height = scope.debug_side_table_entry->stack_height();
    return stack_height - num_locals;
  }

  WasmValue GetStackValue(int index, Address pc, Address fp,
                          Address debug_break_fp, Isolate* isolate) {
    FrameInspectionScope scope(this, pc, isolate);
    int num_locals = scope.debug_side_table->num_locals();
    int value_count = scope.debug_side_table_entry->stack_height();
    if (num_locals + index >= value_count) return {};
    return GetValue(scope.debug_side_table, scope.debug_side_table_entry,
                    num_locals + index, fp, debug_break_fp, isolate);
  }

  const WasmFunction& GetFunctionAtAddress(Address pc, Isolate* isolate) {
    FrameInspectionScope scope(this, pc, isolate);
    auto* module = native_module_->module();
    return module->functions[scope.code->index()];
  }

  // If the frame position is not in the list of breakpoints, return that
  // position. Return 0 otherwise.
  // This is used to generate a "dead breakpoint" in Liftoff, which is necessary
  // for OSR to find the correct return address.
  int DeadBreakpoint(WasmFrame* frame, base::Vector<const int> breakpoints) {
    const auto& function =
        native_module_->module()->functions[frame->function_index()];
    int offset = frame->position() - function.code.offset();
    if (std::binary_search(breakpoints.begin(), breakpoints.end(), offset)) {
      return 0;
    }
    return offset;
  }

  // Find the dead breakpoint (see above) for the top wasm frame, if that frame
  // is in the function of the given index.
  int DeadBreakpoint(int func_index, base::Vector<const int> breakpoints,
                     Isolate* isolate) {
    DebuggableStackFrameIterator it(isolate);
#if !V8_ENABLE_DRUMBRAKE
    if (it.done() || !it.is_wasm()) return 0;
#else   // !V8_ENABLE_DRUMBRAKE
    // TODO(paolosev@microsoft.com) - Implement for Wasm interpreter.
    if (it.done() || !it.is_wasm() || it.is_wasm_interpreter_entry()) {
      return 0;
    }
#endif  // !V8_ENABLE_DRUMBRAKE
    auto* wasm_frame = WasmFrame::cast(it.frame());
    if (static_cast<int>(wasm_frame->function_index()) != func_index) return 0;
    return DeadBreakpoint(wasm_frame, breakpoints);
  }

  WasmCode* RecompileLiftoffWithBreakpoints(int func_index,
                                            base::Vector<const int> offsets,
                                            int dead_breakpoint) {
    DCHECK(!mutex_.TryLock());  // Mutex is held externally.

    ForDebugging for_debugging = offsets.size() == 1 && offsets[0] == 0
                                     ? kForStepping
                                     : kWithBreakpoints;

    // Check the cache first.
    for (auto begin = cached_debugging_code_.begin(), it = begin,
              end = cached_debugging_code_.end();
         it != end; ++it) {
      if (it->func_index == func_index &&
          it->breakpoint_offsets.as_vector() == offsets &&
          it->dead_breakpoint == dead_breakpoint) {
        // Rotate the cache entry to the front (for LRU).
        for (; it != begin; --it) std::iter_swap(it, it - 1);
        if (for_debugging == kWithBreakpoints) {
          // Re-install the code, in case it was replaced in the meantime.
          native_module_->ReinstallDebugCode(it->code);
        }
        return it->code;
      }
    }

    // Recompile the function with Liftoff, setting the new breakpoints.
    // Not thread-safe. The caller is responsible for locking {mutex_}.
    CompilationEnv env = CompilationEnv::ForModule(native_module_);
    const WasmFunction* function = &env.module->functions[func_index];
    base::Vector<const uint8_t> wire_bytes = native_module_->wire_bytes();
    bool is_shared = env.module->types[function->sig_index].is_shared;
    FunctionBody body{function->sig, function->code.offset(),
                      wire_bytes.begin() + function->code.offset(),
                      wire_bytes.begin() + function->code.end_offset(),
                      is_shared};
    std::unique_ptr<DebugSideTable> debug_sidetable;

    // Debug side tables for stepping are generated lazily.
    bool generate_debug_sidetable = for_debugging == kWithBreakpoints;
    // If lazy validation is on, we might need to lazily validate here.
    if (V8_UNLIKELY(!env.module->function_was_validated(func_index))) {
      WasmDetectedFeatures unused_detected_features;
      Zone validation_zone(wasm::GetWasmEngine()->allocator(), ZONE_NAME);
      DecodeResult validation_result =
          ValidateFunctionBody(&validation_zone, env.enabled_features,
                               env.module, &unused_detected_features, body);
      // Handling illegal modules here is tricky. As lazy validation is off by
      // default anyway and this is for debugging only, we just crash for now.
      CHECK_WITH_MSG(validation_result.ok(),
                     validation_result.error().message().c_str());
      env.module->set_function_validated(func_index);
    }
    WasmCompilationResult result = ExecuteLiftoffCompilation(
        &env, body,
        LiftoffOptions{}
            .set_func_index(func_index)
            .set_for_debugging(for_debugging)
            .set_breakpoints(offsets)
            .set_dead_breakpoint(dead_breakpoint)
            .set_debug_sidetable(generate_debug_sidetable ? &debug_sidetable
                                                          : nullptr));
    // Liftoff compilation failure is a FATAL error. We rely on complete Liftoff
    // support for debugging.
    if (!result.succeeded()) FATAL("Liftoff compilation failed");
    DCHECK_EQ(generate_debug_sidetable, debug_sidetable != nullptr);

    WasmCode* new_code =
        native_module_->PublishCode(native_module_->AddCompiledCode(result));

    DCHECK(new_code->is_inspectable());
    if (generate_debug_sidetable) {
      base::MutexGuard lock(&debug_side_tables_mutex_);
      DCHECK_EQ(0, debug_side_tables_.count(new_code));
      debug_side_tables_.emplace(new_code, std::move(debug_sidetable));
    }

    // Insert new code into the cache. Insert before existing elements for LRU.
    cached_debugging_code_.insert(
        cached_debugging_code_.begin(),
        CachedDebuggingCode{func_index, base::OwnedVector<int>::Of(offsets),
                            dead_breakpoint, new_code});
    // Increase the ref count (for the cache entry).
    new_code->IncRef();
    // Remove exceeding element.
    if (cached_debugging_code_.size() > kMaxCachedDebuggingCode) {
      // Put the code in the surrounding CodeRefScope to delay deletion until
      // after the mutex is released.
      WasmCodeRefScope::AddRef(cached_debugging_code_.back().code);
      cached_debugging_code_.back().code->DecRefOnLiveCode();
      cached_debugging_code_.pop_back();
    }
    DCHECK_GE(kMaxCachedDebuggingCode, cached_debugging_code_.size());

    return new_code;
  }

  void SetBreakpoint(int func_index, int offset, Isolate* isolate) {
    // Put the code ref scope outside of the mutex, so we don't unnecessarily
    // hold the mutex while freeing code.
    WasmCodeRefScope wasm_code_ref_scope;

    // Hold the mutex while modifying breakpoints, to ensure consistency when
    // multiple isolates set/remove breakpoints at the same time.
    base::MutexGuard guard(&mutex_);

    // offset == 0 indicates flooding and should not happen here.
    DCHECK_NE(0, offset);

    // Get the set of previously set breakpoints, to check later whether a new
    // breakpoint was actually added.
    std::vector<int> all_breakpoints = FindAllBreakpoints(func_index);

    auto& isolate_data = per_isolate_data_[isolate];
    std::vector<int>& breakpoints =
        isolate_data.breakpoints_per_function[func_index];
    auto insertion_point =
        std::lower_bound(breakpoints.begin(), breakpoints.end(), offset);
    if (insertion_point != breakpoints.end() && *insertion_point == offset) {
      // The breakpoint is already set for this isolate.
      return;
    }
    breakpoints.insert(insertion_point, offset);

    DCHECK(std::is_sorted(all_breakpoints.begin(), all_breakpoints.end()));
    // Find the insertion position within {all_breakpoints}.
    insertion_point = std::lower_bound(all_breakpoints.begin(),
                                       all_breakpoints.end(), offset);
    bool breakpoint_exists =
        insertion_point != all_breakpoints.end() && *insertion_point == offset;
    // If the breakpoint was already set before, then we can just reuse the old
    // code. Otherwise, recompile it. In any case, rewrite this isolate's stack
    // to make sure that it uses up-to-date code containing the breakpoint.
    WasmCode* new_code;
    if (breakpoint_exists) {
      new_code = native_module_->GetCode(func_index);
    } else {
      all_breakpoints.insert(insertion_point, offset);
      int dead_breakpoint =
          DeadBreakpoint(func_index, base::VectorOf(all_breakpoints), isolate);
      new_code = RecompileLiftoffWithBreakpoints(
          func_index, base::VectorOf(all_breakpoints), dead_breakpoint);
    }
    UpdateReturnAddresses(isolate, new_code, isolate_data.stepping_frame);
  }

  std::vector<int> FindAllBreakpoints(int func_index) {
    DCHECK(!mutex_.TryLock());  // Mutex must be held externally.
    std::set<int> breakpoints;
    for (auto& data : per_isolate_data_) {
      auto it = data.second.breakpoints_per_function.find(func_index);
      if (it == data.second.breakpoints_per_function.end()) continue;
      for (int offset : it->second) breakpoints.insert(offset);
    }
    return {breakpoints.begin(), breakpoints.end()};
  }

  void UpdateBreakpoints(int func_index, base::Vector<int> breakpoints,
                         Isolate* isolate, StackFrameId stepping_frame,
                         int dead_breakpoint) {
    DCHECK(!mutex_.TryLock());  // Mutex is held externally.
    WasmCode* new_code = RecompileLiftoffWithBreakpoints(
        func_index, breakpoints, dead_breakpoint);
    UpdateReturnAddresses(isolate, new_code, stepping_frame);
  }

  void FloodWithBreakpoints(WasmFrame* frame, ReturnLocation return_location) {
    // 0 is an invalid offset used to indicate flooding.
    constexpr int kFloodingBreakpoints[] = {0};
    DCHECK(frame->wasm_code()->is_liftoff());
    // Generate an additional source position for the current byte offset.
    base::MutexGuard guard(&mutex_);
    WasmCode* new_code = RecompileLiftoffWithBreakpoints(
        frame->function_index(), base::ArrayVector(kFloodingBreakpoints), 0);
    UpdateReturnAddress(frame, new_code, return_location);

    per_isolate_data_[frame->isolate()].stepping_frame = frame->id();
  }

  bool PrepareStep(WasmFrame* frame) {
    WasmCodeRefScope wasm_code_ref_scope;
    wasm::WasmCode* code = frame->wasm_code();
    if (!code->is_liftoff()) return false;  // Cannot step in TurboFan code.
    if (IsAtReturn(frame)) return false;    // Will return after this step.
    FloodWithBreakpoints(frame, kAfterBreakpoint);
    return true;
  }

  void PrepareStepOutTo(WasmFrame* frame) {
    WasmCodeRefScope wasm_code_ref_scope;
    wasm::WasmCode* code = frame->wasm_code();
    if (!code->is_liftoff()) return;  // Cannot step out to TurboFan code.
    FloodWithBreakpoints(frame, kAfterWasmCall);
  }

  void ClearStepping(WasmFrame* frame) {
    WasmCodeRefScope wasm_code_ref_scope;
    base::MutexGuard guard(&mutex_);
    auto* code = frame->wasm_code();
    if (code->for_debugging() != kForStepping) return;
    int func_index = code->index();
    std::vector<int> breakpoints = FindAllBreakpoints(func_index);
    int dead_breakpoint = DeadBreakpoint(frame, base::VectorOf(breakpoints));
    WasmCode* new_code = RecompileLiftoffWithBreakpoints(
        func_index, base::VectorOf(breakpoints), dead_breakpoint);
    UpdateReturnAddress(frame, new_code, kAfterBreakpoint);
  }

  void ClearStepping(Isolate* isolate) {
    base::MutexGuard guard(&mutex_);
    auto it = per_isolate_data_.find(isolate);
    if (it != per_isolate_data_.end()) it->second.stepping_frame = NO_ID;
  }

  bool IsStepping(WasmFrame* frame) {
    Isolate* isolate = frame->isolate();
    if (isolate->debug()->last_step_action() == StepInto) return true;
    base::MutexGuard guard(&mutex_);
    auto it = per_isolate_data_.find(isolate);
    return it != per_isolate_data_.end() &&
           it->second.stepping_frame == frame->id();
  }

  void RemoveBreakpoint(int func_index, int position, Isolate* isolate) {
    // Put the code ref scope outside of the mutex, so we don't unnecessarily
    // hold the mutex while freeing code.
    WasmCodeRefScope wasm_code_ref_scope;

    // Hold the mutex while modifying breakpoints, to ensure consistency when
    // multiple isolates set/remove breakpoints at the same time.
    base::MutexGuard guard(&mutex_);

    const auto& function = native_module_->module()->functions[func_index];
    int offset = position - function.code.offset();

    auto& isolate_data = per_isolate_data_[isolate];
    std::vector<int>& breakpoints =
        isolate_data.breakpoints_per_function[func_index];
    DCHECK_LT(0, offset);
    auto insertion_point =
        std::lower_bound(breakpoints.begin(), breakpoints.end(), offset);
    if (insertion_point == breakpoints.end()) return;
    if (*insertion_point != offset) return;
    breakpoints.erase(insertion_point);

    std::vector<int> remaining = FindAllBreakpoints(func_index);
    // If the breakpoint is still set in another isolate, don't remove it.
    DCHECK(std::is_sorted(remaining.begin(), remaining.end()));
    if (std::binary_search(remaining.begin(), remaining.end(), offset)) return;
    int dead_breakpoint =
        DeadBreakpoint(func_index, base::VectorOf(remaining), isolate);
    UpdateBreakpoints(func_index, base::VectorOf(remaining), isolate,
                      isolate_data.stepping_frame, dead_breakpoint);
  }

  void RemoveDebugSideTables(base::Vector<WasmCode* const> codes) {
    base::MutexGuard guard(&debug_side_tables_mutex_);
    for (auto* code : codes) {
      debug_side_tables_.erase(code);
    }
  }

  DebugSideTable* GetDebugSideTableIfExists(const WasmCode* code) const {
    base::MutexGuard guard(&debug_side_tables_mutex_);
    auto it = debug_side_tables_.find(code);
    return it == debug_side_tables_.end() ? nullptr : it->second.get();
  }

  static bool HasRemovedBreakpoints(const std::vector<int>& removed,
                                    const std::vector<int>& remaining) {
    DCHECK(std::is_sorted(remaining.begin(), remaining.end()));
    for (int offset : removed) {
      // Return true if we removed a breakpoint which is not part of remaining.
      if (!std::binary_search(remaining.begin(), remaining.end(), offset)) {
        return true;
      }
    }
    return false;
  }

  void RemoveIsolate(Isolate* isolate) {
    // Put the code ref scope outside of the mutex, so we don't unnecessarily
    // hold the mutex while freeing code.
    WasmCodeRefScope wasm_code_ref_scope;

    base::MutexGuard guard(&mutex_);
    auto per_isolate_data_it = per_isolate_data_.find(isolate);
    if (per_isolate_data_it == per_isolate_data_.end()) return;
    std::unordered_map<int, std::vector<int>> removed_per_function =
        std::move(per_isolate_data_it->second.breakpoints_per_function);
    per_isolate_data_.erase(per_isolate_data_it);
    for (auto& entry : removed_per_function) {
      int func_index = entry.first;
      std::vector<int>& removed = entry.second;
      std::vector<int> remaining = FindAllBreakpoints(func_index);
      if (HasRemovedBreakpoints(removed, remaining)) {
        RecompileLiftoffWithBreakpoints(func_index, base::VectorOf(remaining),
                                        0);
      }
    }
  }

  size_t EstimateCurrentMemoryConsumption() const {
    UPDATE_WHEN_CLASS_CHANGES(DebugInfoImpl, 208);
    UPDATE_WHEN_CLASS_CHANGES(CachedDebuggingCode, 40);
    UPDATE_WHEN_CLASS_CHANGES(PerIsolateDebugData, 48);
    size_t result = sizeof(DebugInfoImpl);
    {
      base::MutexGuard lock(&debug_side_tables_mutex_);
      result += ContentSize(debug_side_tables_);
      for (const auto& [code, table] : debug_side_tables_) {
        result += table->EstimateCurrentMemoryConsumption();
      }
    }
    {
      base::MutexGuard lock(&mutex_);
      result += ContentSize(cached_debugging_code_);
      for (const CachedDebuggingCode& code : cached_debugging_code_) {
        result += code.breakpoint_offsets.size() * sizeof(int);
      }
      result += ContentSize(per_isolate_data_);
      for (const auto& [isolate, data] : per_isolate_data_) {
        // Inlined handling of {PerIsolateDebugData}.
        result += ContentSize(data.breakpoints_per_function);
        for (const auto& [idx, breakpoints] : data.breakpoints_per_function) {
          result += ContentSize(breakpoints);
        }
      }
    }
    if (v8_flags.trace_wasm_offheap_memory) {
      PrintF("DebugInfo: %zu\n", result);
    }
    return result;
  }

 private:
  struct FrameInspectionScope {
    FrameInspectionScope(DebugInfoImpl* debug_info, Address pc,
                         Isolate* isolate)
        : code(wasm::GetWasmCodeManager()->LookupCode(isolate, pc)),
          pc_offset(static_cast<int>(pc - code->instruction_start())),
          debug_side_table(code->is_inspectable()
                               ? debug_info->GetDebugSideTable(code)
                               : nullptr),
          debug_side_table_entry(debug_side_table
                                     ? debug_side_table->GetEntry(pc_offset)
                                     : nullptr) {
      DCHECK_IMPLIES(code->is_inspectable(), debug_side_table_entry != nullptr);
    }

    bool is_inspectable() const { return debug_side_table_entry; }

    wasm::WasmCodeRefScope wasm_code_ref_scope;
    wasm::WasmCode* code;
    int pc_offset;
    const DebugSideTable* debug_side_table;
    const DebugSideTable::Entry* debug_side_table_entry;
  };

  const DebugSideTable* GetDebugSideTable(WasmCode* code) {
    DCHECK(code->is_inspectable());
    {
      // Only hold the mutex temporarily. We can't hold it while generating the
      // debug side table, because compilation takes the {NativeModule} lock.
      base::MutexGuard guard(&debug_side_tables_mutex_);
      auto it = debug_side_tables_.find(code);
      if (it != debug_side_tables_.end()) return it->second.get();
    }

    // Otherwise create the debug side table now.
    std::unique_ptr<DebugSideTable> debug_side_table =
        GenerateLiftoffDebugSideTable(code);
    DebugSideTable* ret = debug_side_table.get();

    // Check cache again, maybe another thread concurrently generated a debug
    // side table already.
    {
      base::MutexGuard guard(&debug_side_tables_mutex_);
      auto& slot = debug_side_tables_[code];
      if (slot != nullptr) return slot.get();
      slot = std::move(debug_side_table);
    }

    // Print the code together with the debug table, if requested.
    code->MaybePrint();
    return ret;
  }

  // Get the value of a local (including parameters) or stack value. Stack
  // values follow the locals in the same index space.
  WasmValue GetValue(const DebugSideTable* debug_side_table,
                     const DebugSideTable::Entry* debug_side_table_entry,
                     int index, Address stack_frame_base,
                     Address debug_break_fp, Isolate* isolate) const {
    const auto* value =
        debug_side_table->FindValue(debug_side_table_entry, index);
    if (value->is_constant()) {
      DCHECK(value->type == kWasmI32 || value->type == kWasmI64);
      return value->type == kWasmI32 ? WasmValue(value->i32_const)
                                     : WasmValue(int64_t{value->i32_const});
    }

    if (value->is_register()) {
      auto reg = LiftoffRegister::from_liftoff_code(value->reg_code);
      auto gp_addr = [debug_break_fp](Register reg) {
        return debug_break_fp +
               WasmDebugBreakFrameConstants::GetPushedGpRegisterOffset(
                   reg.code());
      };
      if (reg.is_gp_pair()) {
        DCHECK_EQ(kWasmI64, value->type);
        uint32_t low_word = ReadUnalignedValue<uint32_t>(gp_addr(reg.low_gp()));
        uint32_t high_word =
            ReadUnalignedValue<uint32_t>(gp_addr(reg.high_gp()));
        return WasmValue((uint64_t{high_word} << 32) | low_word);
      }
      if (reg.is_gp()) {
        if (value->type == kWasmI32) {
          return WasmValue(ReadUnalignedValue<uint32_t>(gp_addr(reg.gp())));
        } else if (value->type == kWasmI64) {
          return WasmValue(ReadUnalignedValue<uint64_t>(gp_addr(reg.gp())));
        } else if (value->type.is_reference()) {
          Handle<Object> obj(
              Tagged<Object>(ReadUnalignedValue<Address>(gp_addr(reg.gp()))),
              isolate);
          return WasmValue(obj, value->type);
        } else {
          UNREACHABLE();
        }
      }
      DCHECK(reg.is_fp() || reg.is_fp_pair());
      // ifdef here to workaround unreachable code for is_fp_pair.
#ifdef V8_TARGET_ARCH_ARM
      int code = reg.is_fp_pair() ? reg.low_fp().code() : reg.fp().code();
#else
      int code = reg.fp().code();
#endif
      Address spilled_addr =
          debug_break_fp +
          WasmDebugBreakFrameConstants::GetPushedFpRegisterOffset(code);
      if (value->type == kWasmF32) {
        return WasmValue(ReadUnalignedValue<float>(spilled_addr));
      } else if (value->type == kWasmF64) {
        return WasmValue(ReadUnalignedValue<double>(spilled_addr));
      } else if (value->type == kWasmS128) {
        return WasmValue(Simd128(ReadUnalignedValue<int8x16>(spilled_addr)));
      } else {
        // All other cases should have been handled above.
        UNREACHABLE();
      }
    }

    // Otherwise load the value from the stack.
    Address stack_address = stack_frame_base - value->stack_offset;
    switch (value->type.kind()) {
      case kI32:
        return WasmValue(ReadUnalignedValue<int32_t>(stack_address));
      case kI64:
        return WasmValue(ReadUnalignedValue<int64_t>(stack_address));
      case kF32:
        return WasmValue(ReadUnalignedValue<float>(stack_address));
      case kF64:
        return WasmValue(ReadUnalignedValue<double>(stack_address));
      case kS128:
        return WasmValue(Simd128(ReadUnalignedValue<int8x16>(stack_address)));
      case kRef:
      case kRefNull:
      case kRtt: {
        Handle<Object> obj(
            Tagged<Object>(ReadUnalignedValue<Address>(stack_address)),
            isolate);
        return WasmValue(obj, value->type);
      }
      case kI8:
      case kI16:
      case kF16:
      case kVoid:
      case kBottom:
        UNREACHABLE();
    }
  }

  // After installing a Liftoff code object with a different set of breakpoints,
  // update return addresses on the stack so that execution resumes in the new
  // code. The frame layout itself should be independent of breakpoints.
  void UpdateReturnAddresses(Isolate* isolate, WasmCode* new_code,
                             StackFrameId stepping_frame) {
    // The first return location is after the breakpoint, others are after wasm
    // calls.
    ReturnLocation return_location = kAfterBreakpoint;
    for (DebuggableStackFrameIterator it(isolate); !it.done();
         it.Advance(), return_location = kAfterWasmCall) {
      // We still need the flooded function for stepping.
      if (it.frame()->id() == stepping_frame) continue;
#if !V8_ENABLE_DRUMBRAKE
      if (!it.is_wasm()) continue;
#else   // !V8_ENABLE_DRUMBRAKE
      // TODO(paolosev@microsoft.com) - Implement for Wasm interpreter.
      if (!it.is_wasm() || it.is_wasm_interpreter_entry()) continue;
#endif  // !V8_ENABLE_DRUMBRAKE
      WasmFrame* frame = WasmFrame::cast(it.frame());
      if (frame->native_module() != new_code->native_module()) continue;
      if (frame->function_index() != new_code->index()) continue;
      if (!frame->wasm_code()->is_liftoff()) continue;
      UpdateReturnAddress(frame, new_code, return_location);
    }
  }

  void UpdateReturnAddress(WasmFrame* frame, WasmCode* new_code,
                           ReturnLocation return_location) {
    DCHECK(new_code->is_liftoff());
    DCHECK_EQ(frame->function_index(), new_code->index());
    DCHECK_EQ(frame->native_module(), new_code->native_module());
    DCHECK(frame->wasm_code()->is_liftoff());
    Address new_pc = FindNewPC(frame, new_code, frame->generated_code_offset(),
                               return_location);
#ifdef DEBUG
    int old_position = frame->position();
#endif
#if V8_TARGET_ARCH_X64
    if (frame->wasm_code()->for_debugging()) {
      base::Memory<Address>(frame->fp() - kOSRTargetOffset) = new_pc;
    }
#else
    PointerAuthentication::ReplacePC(frame->pc_address(), new_pc,
                                     kSystemPointerSize);
#endif
    // The frame position should still be the same after OSR.
    DCHECK_EQ(old_position, frame->position());
  }

  bool IsAtReturn(WasmFrame* frame) {
    DisallowGarbageCollection no_gc;
    int position = frame->position();
    NativeModule* native_module = frame->native_module();
    uint8_t opcode = native_module->wire_bytes()[position];
    if (opcode == kExprReturn) return true;
    // Another implicit return is at the last kExprEnd in the function body.
    int func_index = frame->function_index();
    WireBytesRef code = native_module->module()->functions[func_index].code;
    return static_cast<size_t>(position) == code.end_offset() - 1;
  }

  // Isolate-specific data, for debugging modules that are shared by multiple
  // isolates.
  struct PerIsolateDebugData {
    // Keeps track of the currently set breakpoints (by offset within that
    // function).
    std::unordered_map<int, std::vector<int>> breakpoints_per_function;

    // Store the frame ID when stepping, to avoid overwriting that frame when
    // setting or removing a breakpoint.
    StackFrameId stepping_frame = NO_ID;
  };

  NativeModule* const native_module_;

  mutable base::Mutex debug_side_tables_mutex_;

  // DebugSideTable per code object, lazily initialized.
  std::unordered_map<const WasmCode*, std::unique_ptr<DebugSideTable>>
      debug_side_tables_;

  // {mutex_} protects all fields below.
  mutable base::Mutex mutex_;

  // Cache a fixed number of WasmCode objects that were generated for debugging.
  // This is useful especially in stepping, because stepping code is cleared on
  // every pause and re-installed on the next step.
  // This is a LRU cache (most recently used entries first).
  static constexpr size_t kMaxCachedDebuggingCode = 3;
  struct CachedDebuggingCode {
    int func_index;
    base::OwnedVector<const int> breakpoint_offsets;
    int dead_breakpoint;
    WasmCode* code;
  };
  std::vector<CachedDebuggingCode> cached_debugging_code_;

  // Isolate-specific data.
  std::unordered_map<Isolate*, PerIsolateDebugData> per_isolate_data_;
};

DebugInfo::DebugInfo(NativeModule* native_module)
    : impl_(std::make_unique<DebugInfoImpl>(native_module)) {}

DebugInfo::~DebugInfo() = default;

int DebugInfo::GetNumLocals(Address pc, Isolate* isolate) {
  return impl_->GetNumLocals(pc, isolate);
}

WasmValue DebugInfo::GetLocalValue(int local, Address pc, Address fp,
                                   Address debug_break_fp, Isolate* isolate) {
  return impl_->GetLocalValue(local, pc, fp, debug_break_fp, isolate);
}

int DebugInfo::GetStackDepth(Address pc, Isolate* isolate) {
  return impl_->GetStackDepth(pc, isolate);
}

WasmValue DebugInfo::GetStackValue(int index, Address pc, Address fp,
                                   Address debug_break_fp, Isolate* isolate) {
  return impl_->GetStackValue(index, pc, fp, debug_break_fp, isolate);
}

const wasm::WasmFunction& DebugInfo::GetFunctionAtAddress(Address pc,
                                                          Isolate* isolate) {
  return impl_->GetFunctionAtAddress(pc, isolate);
}

void DebugInfo::SetBreakpoint(int func_index, int offset,
                              Isolate* current_isolate) {
  impl_->SetBreakpoint(func_index, offset, current_isolate);
}

bool DebugInfo::PrepareStep(WasmFrame* frame) {
  return impl_->PrepareStep(frame);
}

void DebugInfo::PrepareStepOutTo(WasmFrame* frame) {
  impl_->PrepareStepOutTo(frame);
}

void DebugInfo::ClearStepping(Isolate* isolate) {
  impl_->ClearStepping(isolate);
}

void DebugInfo::ClearStepping(WasmFrame* frame) { impl_->ClearStepping(frame); }

bool DebugInfo::IsStepping(WasmFrame* frame) {
  return impl_->IsStepping(frame);
}

void DebugInfo::RemoveBreakpoint(int func_index, int offset,
                                 Isolate* current_isolate) {
  impl_->RemoveBreakpoint(func_index, offset, current_isolate);
}

void DebugInfo::RemoveDebugSideTables(base::Vector<WasmCode* const> code) {
  impl_->RemoveDebugSideTables(code);
}

DebugSideTable* DebugInfo::GetDebugSideTableIfExists(
    const WasmCode* code) const {
  return impl_->GetDebugSideTableIfExists(code);
}

void DebugInfo::RemoveIsolate(Isolate* isolate) {
  return impl_->RemoveIsolate(isolate);
}

size_t DebugInfo::EstimateCurrentMemoryConsumption() const {
  return impl_->EstimateCurrentMemoryConsumption();
}

}  // namespace wasm

namespace {

// Return the next breakable position at or after {offset_in_func} in function
// {func_index}, or 0 if there is none.
// Note that 0 is never a breakable position in wasm, since the first uint8_t
// contains the locals count for the function.
int FindNextBreakablePosition(wasm::NativeModule* native_module, int func_index,
                              int offset_in_func) {
  Zone zone{wasm::GetWasmEngine()->allocator(), ZONE_NAME};
  wasm::BodyLocalDecls locals;
  const uint8_t* module_start = native_module->wire_bytes().begin();
  const wasm::WasmFunction& func =
      native_module->module()->functions[func_index];
  wasm::BytecodeIterator iterator(module_start + func.code.offset(),
                                  module_start + func.code.end_offset(),
                                  &locals, &zone);
  DCHECK_LT(0, locals.encoded_size);
  if (offset_in_func < 0) return 0;
  for (; iterator.has_next(); iterator.next()) {
    if (iterator.pc_offset() < static_cast<uint32_t>(offset_in_func)) continue;
    if (!wasm::WasmOpcodes::IsBreakable(iterator.current())) continue;
    return static_cast<int>(iterator.pc_offset());
  }
  return 0;
}

void SetBreakOnEntryFlag(Tagged<Script> script, bool enabled) {
  if (script->break_on_entry() == enabled) return;

  script->set_break_on_entry(enabled);
  // Update the "break_on_entry" flag on all live instances.
  i::Tagged<i::WeakArrayList> weak_instance_list =
      script->wasm_weak_instance_list();
  i::Isolate* isolate = script->GetIsolate();
  for (int i = 0; i < weak_instance_list->length(); ++i) {
    if (weak_instance_list->Get(i).IsCleared()) continue;
    i::Tagged<i::WasmInstanceObject> instance = i::Cast<i::WasmInstanceObject>(
        weak_instance_list->Get(i).GetHeapObject());
    instance->trusted_data(isolate)->set_break_on_entry(enabled);
  }
}
}  // namespace

// static
bool WasmScript::SetBreakPoint(DirectHandle<Script> script, int* position,
                               DirectHandle<BreakPoint> break_point) {
  DCHECK_NE(kOnEntryBreakpointPosition, *position);

  // Find the function for this breakpoint.
  const wasm::WasmModule* module = script->wasm_native_module()->module();
  int func_index = GetContainingWasmFunction(module, *position);
  if (func_index < 0) return false;
  const wasm::WasmFunction& func = module->functions[func_index];
  int offset_in_func = *position - func.code.offset();

  int breakable_offset = FindNextBreakablePosition(script->wasm_native_module(),
                                                   func_index, offset_in_func);
  if (breakable_offset == 0) return false;
  *position = func.code.offset() + breakable_offset;

  return WasmScript::SetBreakPointForFunction(script, func_index,
                                              breakable_offset, break_point);
}

// static
void WasmScript::SetInstrumentationBreakpoint(
    DirectHandle<Script> script, DirectHandle<BreakPoint> break_point) {
  // Special handling for on-entry breakpoints.
  AddBreakpointToInfo(script, kOnEntryBreakpointPosition, break_point);

  // Update the "break_on_entry" flag on all live instances.
  SetBreakOnEntryFlag(*script, true);
}

// static
bool WasmScript::SetBreakPointOnFirstBreakableForFunction(
    DirectHandle<Script> script, int func_index,
    DirectHandle<BreakPoint> break_point) {
  if (func_index < 0) return false;
  int offset_in_func = 0;

  int breakable_offset = FindNextBreakablePosition(script->wasm_native_module(),
                                                   func_index, offset_in_func);
  if (breakable_offset == 0) return false;
  return WasmScript::SetBreakPointForFunction(script, func_index,
                                              breakable_offset, break_point);
}

// static
bool WasmScript::SetBreakPointForFunction(
    DirectHandle<Script> script, int func_index, int offset,
    DirectHandle<BreakPoint> break_point) {
  Isolate* isolate = script->GetIsolate();

  DCHECK_LE(0, func_index);
  DCHECK_NE(0, offset);

  // Find the function for this breakpoint.
  wasm::NativeModule* native_module = script->wasm_native_module();
  const wasm::WasmModule* module = native_module->module();
  const wasm::WasmFunction& func = module->functions[func_index];

  // Insert new break point into {wasm_breakpoint_infos} of the script.
  AddBreakpointToInfo(script, func.code.offset() + offset, break_point);

  native_module->GetDebugInfo()->SetBreakpoint(func_index, offset, isolate);

  return true;
}

namespace {

int GetBreakpointPos(Isolate* isolate,
                     Tagged<Object> break_point_info_or_undef) {
  if (IsUndefined(break_point_info_or_undef, isolate)) return kMaxInt;
  return Cast<BreakPointInfo>(break_point_info_or_undef)->source_position();
}

int FindBreakpointInfoInsertPos(Isolate* isolate,
                                DirectHandle<FixedArray> breakpoint_infos,
                                int position) {
  // Find insert location via binary search, taking care of undefined values on
  // the right. {position} is either {kOnEntryBreakpointPosition} (which is -1),
  // or positive.
  DCHECK(position == WasmScript::kOnEntryBreakpointPosition || position > 0);

  int left = 0;                            // inclusive
  int right = breakpoint_infos->length();  // exclusive
  while (right - left > 1) {
    int mid = left + (right - left) / 2;
    Tagged<Object> mid_obj = breakpoint_infos->get(mid);
    if (GetBreakpointPos(isolate, mid_obj) <= position) {
      left = mid;
    } else {
      right = mid;
    }
  }

  int left_pos = GetBreakpointPos(isolate, breakpoint_infos->get(left));
  return left_pos < position ? left + 1 : left;
}

}  // namespace

// static
bool WasmScript::ClearBreakPoint(DirectHandle<Script> script, int position,
                                 DirectHandle<BreakPoint> break_point) {
  if (!script->has_wasm_breakpoint_infos()) return false;

  Isolate* isolate = script->GetIsolate();
  DirectHandle<FixedArray> breakpoint_infos(script->wasm_breakpoint_infos(),
                                            isolate);

  int pos = FindBreakpointInfoInsertPos(isolate, breakpoint_infos, position);

  // Does a BreakPointInfo object already exist for this position?
  if (pos == breakpoint_infos->length()) return false;

  DirectHandle<BreakPointInfo> info(
      Cast<BreakPointInfo>(breakpoint_infos->get(pos)), isolate);
  BreakPointInfo::ClearBreakPoint(isolate, info, break_point);

  // Check if there are no more breakpoints at this location.
  if (info->GetBreakPointCount(isolate) == 0) {
    // Update array by moving breakpoints up one position.
    for (int i = pos; i < breakpoint_infos->length() - 1; i++) {
      Tagged<Object> entry = breakpoint_infos->get(i + 1);
      breakpoint_infos->set(i, entry);
      if (IsUndefined(entry, isolate)) break;
    }
    // Make sure last array element is empty as a result.
    breakpoint_infos->set(breakpoint_infos->length() - 1,
                          ReadOnlyRoots{isolate}.undefined_value(),
                          SKIP_WRITE_BARRIER);
  }

  if (break_point->id() == v8::internal::Debug::kInstrumentationId) {
    // Special handling for instrumentation breakpoints.
    SetBreakOnEntryFlag(*script, false);
  } else {
    // Remove the breakpoint from DebugInfo and recompile.
    wasm::NativeModule* native_module = script->wasm_native_module();
    const wasm::WasmModule* module = native_module->module();
    int func_index = GetContainingWasmFunction(module, position);
    native_module->GetDebugInfo()->RemoveBreakpoint(func_index, position,
                                                    isolate);
  }

  return true;
}

// static
bool WasmScript::ClearBreakPointById(DirectHandle<Script> script,
                                     int breakpoint_id) {
  if (!script->has_wasm_breakpoint_infos()) {
    return false;
  }
  Isolate* isolate = script->GetIsolate();
  DirectHandle<FixedArray> breakpoint_infos(script->wasm_breakpoint_infos(),
                                            isolate);
  // If the array exists, it should not be empty.
  DCHECK_LT(0, breakpoint_infos->length());

  for (int i = 0, e = breakpoint_infos->length(); i < e; ++i) {
    DirectHandle<Object> obj(breakpoint_infos->get(i), isolate);
    if (IsUndefined(*obj, isolate)) {
      continue;
    }
    auto breakpoint_info = Cast<BreakPointInfo>(obj);
    Handle<BreakPoint> breakpoint;
    if (BreakPointInfo::GetBreakPointById(isolate, breakpoint_info,
                                          breakpoint_id)
            .ToHandle(&breakpoint)) {
      DCHECK(breakpoint->id() == breakpoint_id);
      return WasmScript::ClearBreakPoint(
          script, breakpoint_info->source_position(), breakpoint);
    }
  }
  return false;
}

// static
void WasmScript::ClearAllBreakpoints(Tagged<Script> script) {
  script->set_wasm_breakpoint_infos(
      ReadOnlyRoots(script->GetIsolate()).empty_fixed_array());
  SetBreakOnEntryFlag(script, false);
}

// static
void WasmScript::AddBreakpointToInfo(DirectHandle<Script> script, int position,
                                     DirectHandle<BreakPoint> break_point) {
  Isolate* isolate = script->GetIsolate();
  DirectHandle<FixedArray> breakpoint_infos;
  if (script->has_wasm_breakpoint_infos()) {
    breakpoint_infos = direct_handle(script->wasm_breakpoint_infos(), isolate);
  } else {
    breakpoint_infos =
        isolate->factory()->NewFixedArray(4, AllocationType::kOld);
    script->set_wasm_breakpoint_infos(*breakpoint_infos);
  }

  int insert_pos =
      FindBreakpointInfoInsertPos(isolate, breakpoint_infos, position);

  // If a BreakPointInfo object already exists for this position, add the new
  // breakpoint object and return.
  if (insert_pos < breakpoint_infos->length() &&
      GetBreakpointPos(isolate, breakpoint_infos->get(insert_pos)) ==
          position) {
    DirectHandle<BreakPointInfo> old_info(
        Cast<BreakPointInfo>(breakpoint_infos->get(insert_pos)), isolate);
    BreakPointInfo::SetBreakPoint(isolate, old_info, break_point);
    return;
  }

  // Enlarge break positions array if necessary.
  bool need_realloc = !IsUndefined(
      breakpoint_infos->get(breakpoint_infos->length() - 1), isolate);
  DirectHandle<FixedArray> new_breakpoint_infos = breakpoint_infos;
  if (need_realloc) {
    new_breakpoint_infos = isolate->factory()->NewFixedArray(
        2 * breakpoint_infos->length(), AllocationType::kOld);
    script->set_wasm_breakpoint_infos(*new_breakpoint_infos);
    // Copy over the entries [0, insert_pos).
    for (int i = 0; i < insert_pos; ++i)
      new_breakpoint_infos->set(i, breakpoint_infos->get(i));
  }

  // Move elements [insert_pos, ...] up by one.
  for (int i = breakpoint_infos->length() - 1; i >= insert_pos; --i) {
    Tagged<Object> entry = breakpoint_infos->get(i);
    if (IsUndefined(entry, isolate)) continue;
    new_breakpoint_infos->set(i + 1, entry);
  }

  // Generate new BreakpointInfo.
  DirectHandle<BreakPointInfo> breakpoint_info =
      isolate->factory()->NewBreakPointInfo(position);
  BreakPointInfo::SetBreakPoint(isolate, breakpoint_info, break_point);

  // Now insert new position at insert_pos.
  new_breakpoint_infos->set(insert_pos, *breakpoint_info);
}

// static
bool WasmScript::GetPossibleBreakpoints(
    wasm::NativeModule* native_module, const v8::debug::Location& start,
    const v8::debug::Location& end,
    std::vector<v8::debug::BreakLocation>* locations) {
  DisallowGarbageCollection no_gc;

  const wasm::WasmModule* module = native_module->module();
  const std::vector<wasm::WasmFunction>& functions = module->functions;

  if (start.GetLineNumber() != 0 || start.GetColumnNumber() < 0 ||
      (!end.IsEmpty() &&
       (end.GetLineNumber() != 0 || end.GetColumnNumber() < 0 ||
        end.GetColumnNumber() < start.GetColumnNumber())))
    return false;

  // start_func_index, start_offset and end_func_index is inclusive.
  // end_offset is exclusive.
  // start_offset and end_offset are module-relative byte offsets.
  // We set strict to false because offsets may be between functions.
  int start_func_index =
      GetNearestWasmFunction(module, start.GetColumnNumber());
  if (start_func_index < 0) return false;
  uint32_t start_offset = start.GetColumnNumber();
  int end_func_index;
  uint32_t end_offset;

  if (end.IsEmpty()) {
    // Default: everything till the end of the Script.
    end_func_index = static_cast<uint32_t>(functions.size() - 1);
    end_offset = functions[end_func_index].code.end_offset();
  } else {
    // If end is specified: Use it and check for valid input.
    end_offset = end.GetColumnNumber();
    end_func_index = GetNearestWasmFunction(module, end_offset);
    DCHECK_GE(end_func_index, start_func_index);
  }

  if (start_func_index == end_func_index &&
      start_offset > functions[end_func_index].code.end_offset())
    return false;
  Zone zone{wasm::GetWasmEngine()->allocator(), ZONE_NAME};
  const uint8_t* module_start = native_module->wire_bytes().begin();

  for (int func_idx = start_func_index; func_idx <= end_func_index;
       ++func_idx) {
    const wasm::WasmFunction& func = functions[func_idx];
    if (func.code.length() == 0) continue;

    wasm::BodyLocalDecls locals;
    wasm::BytecodeIterator iterator(module_start + func.code.offset(),
                                    module_start + func.code.end_offset(),
                                    &locals, &zone);
    DCHECK_LT(0u, locals.encoded_size);
    for (; iterator.has_next(); iterator.next()) {
      uint32_t total_offset = func.code.offset() + iterator.pc_offset();
      if (total_offset >= end_offset) {
        DCHECK_EQ(end_func_index, func_idx);
        break;
      }
      if (total_offset < start_offset) continue;
      if (!wasm::WasmOpcodes::IsBreakable(iterator.current())) continue;
      locations->emplace_back(0, total_offset, debug::kCommonBreakLocation);
    }
  }
  return true;
}

namespace {

bool CheckBreakPoint(Isolate* isolate, DirectHandle<BreakPoint> break_point,
                     StackFrameId frame_id) {
  if (break_point->condition()->length() == 0) return true;

  HandleScope scope(isolate);
  Handle<String> condition(break_point->condition(), isolate);
  Handle<Object> result;
  // The Wasm engine doesn't perform any sort of inlining.
  const int inlined_jsframe_index = 0;
  const bool throw_on_side_effect = false;
  if (!DebugEvaluate::Local(isolate, frame_id, inlined_jsframe_index, condition,
                            throw_on_side_effect)
           .ToHandle(&result)) {
    isolate->clear_exception();
    return false;
  }
  return Object::BooleanValue(*result, isolate);
}

}  // namespace

// static
MaybeHandle<FixedArray> WasmScript::CheckBreakPoints(
    Isolate* isolate, DirectHandle<Script> script, int position,
    StackFrameId frame_id) {
  if (!script->has_wasm_breakpoint_infos()) return {};

  DirectHandle<FixedArray> breakpoint_infos(script->wasm_breakpoint_infos(),
                                            isolate);
  int insert_pos =
      FindBreakpointInfoInsertPos(isolate, breakpoint_infos, position);
  if (insert_pos >= breakpoint_infos->length()) return {};

  DirectHandle<Object> maybe_breakpoint_info(breakpoint_infos->get(insert_pos),
                                             isolate);
  if (IsUndefined(*maybe_breakpoint_info, isolate)) return {};
  auto breakpoint_info = Cast<BreakPointInfo>(maybe_breakpoint_info);
  if (breakpoint_info->source_position() != position) return {};

  DirectHandle<Object> break_points(breakpoint_info->break_points(), isolate);
  if (!IsFixedArray(*break_points)) {
    if (!CheckBreakPoint(isolate, Cast<BreakPoint>(break_points), frame_id)) {
      // A breakpoint that doesn't break mutes traps. (Rule enables the
      // "Never Pause Here" feature.)
      isolate->debug()->SetMutedWasmLocation(script, position);
      return {};
    }
    // If breakpoint does fire, clear any prior muting behavior.
    isolate->debug()->ClearMutedLocation();
    Handle<FixedArray> break_points_hit = isolate->factory()->NewFixedArray(1);
    break_points_hit->set(0, *break_points);
    return break_points_hit;
  }

  auto array = Cast<FixedArray>(break_points);
  Handle<FixedArray> break_points_hit =
      isolate->factory()->NewFixedArray(array->length());
  int break_points_hit_count = 0;
  for (int i = 0; i < array->length(); ++i) {
    DirectHandle<BreakPoint> break_point(Cast<BreakPoint>(array->get(i)),
                                         isolate);
    if (CheckBreakPoint(isolate, break_point, frame_id)) {
      break_points_hit->set(break_points_hit_count++, *break_point);
    }
  }
  if (break_points_hit_count == 0) return {};
  break_points_hit->RightTrim(isolate, break_points_hit_count);
  return break_points_hit;
}

}  // namespace internal
}  // namespace v8
