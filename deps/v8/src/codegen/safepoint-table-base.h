// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SAFEPOINT_TABLE_BASE_H_
#define V8_CODEGEN_SAFEPOINT_TABLE_BASE_H_

#include <cstdint>

#include "src/base/logging.h"

namespace v8 {
namespace internal {

// Both Turbofan and Malgev safepoint tables store the stack slots as the first
// data entry in the header.
constexpr int kSafepointTableStackSlotsOffset = 0;
using SafepointTableStackSlotsField_t = uint32_t;

class SafepointEntryBase {
 public:
  static constexpr int kNoDeoptIndex = -1;
  static constexpr int kNoTrampolinePC = -1;

  SafepointEntryBase() = default;

  SafepointEntryBase(int pc, int deopt_index, int trampoline_pc)
      : pc_(pc), deopt_index_(deopt_index), trampoline_pc_(trampoline_pc) {
    DCHECK(is_initialized());
  }

  bool is_initialized() const { return pc_ != 0; }

  int pc() const {
    DCHECK(is_initialized());
    return pc_;
  }

  int trampoline_pc() const { return trampoline_pc_; }

  bool has_deoptimization_index() const {
    return deopt_index_ != kNoDeoptIndex;
  }

  int deoptimization_index() const {
    DCHECK(has_deoptimization_index());
    return deopt_index_;
  }

  void Reset() { pc_ = 0; }

 protected:
  bool operator==(const SafepointEntryBase& other) const {
    return pc_ == other.pc_ && deopt_index_ == other.deopt_index_ &&
           trampoline_pc_ == other.trampoline_pc_;
  }

 private:
  int pc_ = 0;
  int deopt_index_ = kNoDeoptIndex;
  int trampoline_pc_ = kNoTrampolinePC;
};

class SafepointTableBuilderBase {
 public:
  bool emitted() const {
    return safepoint_table_offset_ != kNoSafepointTableOffset;
  }

  int safepoint_table_offset() const {
    DCHECK(emitted());
    return safepoint_table_offset_;
  }

 protected:
  void set_safepoint_table_offset(int offset) {
    DCHECK(!emitted());
    safepoint_table_offset_ = offset;
    DCHECK(emitted());
  }

 private:
  static constexpr int kNoSafepointTableOffset = -1;
  int safepoint_table_offset_ = kNoSafepointTableOffset;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_SAFEPOINT_TABLE_BASE_H_
