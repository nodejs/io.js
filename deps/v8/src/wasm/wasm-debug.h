// Copyright 2019 the V8 project authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_DEBUG_H_
#define V8_WASM_WASM_DEBUG_H_

#include <algorithm>
#include <memory>
#include <vector>

#include "include/v8-internal.h"
#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/vector.h"
#include "src/wasm/value-type.h"

namespace v8 {
namespace internal {

class WasmFrame;

namespace wasm {

class DebugInfoImpl;
class NativeModule;
class WasmCode;
class WireBytesRef;
class WasmValue;
struct WasmFunction;

// Side table storing information used to inspect Liftoff frames at runtime.
// This table is only created on demand for debugging, so it is not optimized
// for memory size.
class DebugSideTable {
 public:
  class Entry {
   public:
    enum Storage : int8_t { kConstant, kRegister, kStack };
    struct Value {
      int index;
      ValueType type;
      Storage storage;
      union {
        int32_t i32_const;  // if kind == kConstant
        int reg_code;       // if kind == kRegister
        int stack_offset;   // if kind == kStack
      };

      bool operator==(const Value& other) const {
        if (index != other.index) return false;
        if (type != other.type) return false;
        if (storage != other.storage) return false;
        switch (storage) {
          case kConstant:
            return i32_const == other.i32_const;
          case kRegister:
            return reg_code == other.reg_code;
          case kStack:
            return stack_offset == other.stack_offset;
        }
      }
      bool operator!=(const Value& other) const { return !(*this == other); }

      bool is_constant() const { return storage == kConstant; }
      bool is_register() const { return storage == kRegister; }
    };

    Entry(int pc_offset, int stack_height, std::vector<Value> changed_values)
        : pc_offset_(pc_offset),
          stack_height_(stack_height),
          changed_values_(std::move(changed_values)) {}

    // Constructor for map lookups (only initializes the {pc_offset_}).
    explicit Entry(int pc_offset) : pc_offset_(pc_offset) {}

    int pc_offset() const { return pc_offset_; }

    // Stack height, including locals.
    int stack_height() const { return stack_height_; }

    base::Vector<const Value> changed_values() const {
      return base::VectorOf(changed_values_);
    }

    const Value* FindChangedValue(int stack_index) const {
      DCHECK_GT(stack_height_, stack_index);
      auto it = std::lower_bound(
          changed_values_.begin(), changed_values_.end(), stack_index,
          [](const Value& changed_value, int stack_index) {
            return changed_value.index < stack_index;
          });
      return it != changed_values_.end() && it->index == stack_index ? &*it
                                                                     : nullptr;
    }

    void Print(std::ostream&) const;

    size_t EstimateCurrentMemoryConsumption() const;

   private:
    int pc_offset_;
    int stack_height_;
    // Only store differences from the last entry, to keep the table small.
    std::vector<Value> changed_values_;
  };

  // Technically it would be fine to copy this class, but there should not be a
  // reason to do so, hence mark it move only.
  MOVE_ONLY_NO_DEFAULT_CONSTRUCTOR(DebugSideTable);

  explicit DebugSideTable(int num_locals, std::vector<Entry> entries)
      : num_locals_(num_locals), entries_(std::move(entries)) {
    DCHECK(
        std::is_sorted(entries_.begin(), entries_.end(), EntryPositionLess{}));
  }

  const Entry* GetEntry(int pc_offset) const {
    auto it = std::lower_bound(entries_.begin(), entries_.end(),
                               Entry{pc_offset}, EntryPositionLess{});
    if (it == entries_.end() || it->pc_offset() != pc_offset) return nullptr;
    DCHECK_LE(num_locals_, it->stack_height());
    return &*it;
  }

  const Entry::Value* FindValue(const Entry* entry, int stack_index) const {
    while (true) {
      if (auto* value = entry->FindChangedValue(stack_index)) {
        // Check that the table was correctly minimized: If the previous stack
        // also had an entry for {stack_index}, it must be different.
        DCHECK(entry == &entries_.front() ||
               (entry - 1)->stack_height() <= stack_index ||
               *FindValue(entry - 1, stack_index) != *value);
        return value;
      }
      DCHECK_NE(&entries_.front(), entry);
      --entry;
    }
  }

  auto entries() const {
    return base::make_iterator_range(entries_.begin(), entries_.end());
  }

  int num_locals() const { return num_locals_; }

  void Print(std::ostream&) const;

  size_t EstimateCurrentMemoryConsumption() const;

 private:
  struct EntryPositionLess {
    bool operator()(const Entry& a, const Entry& b) const {
      return a.pc_offset() < b.pc_offset();
    }
  };

  int num_locals_;
  std::vector<Entry> entries_;
};

// Debug info per NativeModule, created lazily on demand.
// Implementation in {wasm-debug.cc} using PIMPL.
class V8_EXPORT_PRIVATE DebugInfo {
 public:
  explicit DebugInfo(NativeModule*);
  ~DebugInfo();

  // For the frame inspection methods below:
  // {fp} is the frame pointer of the Liftoff frame, {debug_break_fp} that of
  // the {WasmDebugBreak} frame (if any).
  int GetNumLocals(Address pc, Isolate* isolate);
  WasmValue GetLocalValue(int local, Address pc, Address fp,
                          Address debug_break_fp, Isolate* isolate);
  int GetStackDepth(Address pc, Isolate* isolate);

  const wasm::WasmFunction& GetFunctionAtAddress(Address pc, Isolate* isolate);

  WasmValue GetStackValue(int index, Address pc, Address fp,
                          Address debug_break_fp, Isolate* isolate);

  void SetBreakpoint(int func_index, int offset, Isolate* current_isolate);

  // Returns true if we stay inside the passed frame (or a called frame) after
  // the step. False if the frame will return after the step.
  bool PrepareStep(WasmFrame*);

  void PrepareStepOutTo(WasmFrame*);

  void ClearStepping(Isolate*);

  // Remove stepping code from a single frame; this is a performance
  // optimization only, hitting debug breaks while not stepping and not at a set
  // breakpoint would be unobservable otherwise.
  void ClearStepping(WasmFrame*);

  bool IsStepping(WasmFrame*);

  void RemoveBreakpoint(int func_index, int offset, Isolate* current_isolate);

  void RemoveDebugSideTables(base::Vector<WasmCode* const>);

  // Return the debug side table for the given code object, but only if it has
  // already been created. This will never trigger generation of the table.
  DebugSideTable* GetDebugSideTableIfExists(const WasmCode*) const;

  void RemoveIsolate(Isolate*);

  size_t EstimateCurrentMemoryConsumption() const;

 private:
  std::unique_ptr<DebugInfoImpl> impl_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_DEBUG_H_
