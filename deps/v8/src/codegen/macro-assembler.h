// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_MACRO_ASSEMBLER_H_
#define V8_CODEGEN_MACRO_ASSEMBLER_H_

#include "src/codegen/macro-assembler-base.h"
#include "src/execution/frames.h"
#include "src/heap/heap.h"

// Helper types to make boolean flag easier to read at call-site.
enum class InvokeType { kCall, kJump };

// Flags used for the AllocateInNewSpace functions.
enum AllocationFlags {
  // No special flags.
  NO_ALLOCATION_FLAGS = 0,
  // The content of the result register already contains the allocation top in
  // new space.
  RESULT_CONTAINS_TOP = 1 << 0,
  // Specify that the requested size of the space to allocate is specified in
  // words instead of bytes.
  SIZE_IN_WORDS = 1 << 1,
  // Align the allocation to a multiple of kDoubleSize
  DOUBLE_ALIGNMENT = 1 << 2,
  // Directly allocate in old space
  PRETENURE = 1 << 3,
};

enum class JumpMode {
  kJump,          // Does a direct jump to the given address
  kPushAndReturn  // Pushes the given address as the current return address and
                  // does a return
};

enum class SmiCheck { kOmit, kInline };

enum class ComparisonMode {
  // The default compare mode will use a 32-bit comparison when pointer
  // compression is enabled and the root is a tagged value.
  kDefault,
  // This mode can be used when the value to compare may not be located inside
  // the main pointer compression cage.
  kFullPointer,
};

enum class SetIsolateDataSlots {
  kNo,
  kYes,
};

// This is the only place allowed to include the platform-specific headers.
#define INCLUDED_FROM_MACRO_ASSEMBLER_H
#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/macro-assembler-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/macro-assembler-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/codegen/arm64/constants-arm64.h"
#include "src/codegen/arm64/macro-assembler-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/codegen/arm/constants-arm.h"
#include "src/codegen/arm/macro-assembler-arm.h"
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
#include "src/codegen/ppc/constants-ppc.h"
#include "src/codegen/ppc/macro-assembler-ppc.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/codegen/mips64/constants-mips64.h"
#include "src/codegen/mips64/macro-assembler-mips64.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/codegen/loong64/constants-loong64.h"
#include "src/codegen/loong64/macro-assembler-loong64.h"
#elif V8_TARGET_ARCH_S390
#include "src/codegen/s390/constants-s390.h"
#include "src/codegen/s390/macro-assembler-s390.h"
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#include "src/codegen/riscv/constants-riscv.h"
#include "src/codegen/riscv/macro-assembler-riscv.h"
#else
#error Unsupported target architecture.
#endif
#undef INCLUDED_FROM_MACRO_ASSEMBLER_H

namespace v8 {
namespace internal {

// Maximum number of parameters supported in calls to C/C++. The C++ standard
// defines a limit of 256 parameters but in simulator builds we provide only
// limited support.
#ifdef USE_SIMULATOR
static constexpr int kMaxCParameters = 20;
#else
static constexpr int kMaxCParameters = 256;
#endif

class V8_NODISCARD FrameScope {
 public:
  explicit FrameScope(MacroAssembler* masm, StackFrame::Type type,
                      const SourceLocation& loc = SourceLocation())
      :
#ifdef V8_CODE_COMMENTS
        comment_(masm, frame_name(type), loc),
#endif
        masm_(masm),
        type_(type),
        old_has_frame_(masm->has_frame()) {
    masm->set_has_frame(true);
    if (type != StackFrame::MANUAL && type_ != StackFrame::NO_FRAME_TYPE) {
      masm->EnterFrame(type);
    }
  }

  ~FrameScope() {
    if (type_ != StackFrame::MANUAL && type_ != StackFrame::NO_FRAME_TYPE) {
      masm_->LeaveFrame(type_);
    }
    masm_->set_has_frame(old_has_frame_);
  }

 private:
#ifdef V8_CODE_COMMENTS
  const char* frame_name(StackFrame::Type type) {
    switch (type) {
      case StackFrame::NO_FRAME_TYPE:
        return "Frame: NO_FRAME_TYPE";
      case StackFrame::MANUAL:
        return "Frame: MANUAL";
#define FRAME_TYPE_CASE(type, field) \
  case StackFrame::type:             \
    return "Frame: " #type;
        STACK_FRAME_TYPE_LIST(FRAME_TYPE_CASE)
#undef FRAME_TYPE_CASE
      case StackFrame::NUMBER_OF_TYPES:
        break;
    }
    return "Frame";
  }

  Assembler::CodeComment comment_;
#endif  // V8_CODE_COMMENTS

  MacroAssembler* masm_;
  StackFrame::Type const type_;
  bool const old_has_frame_;
};

class V8_NODISCARD FrameAndConstantPoolScope {
 public:
  FrameAndConstantPoolScope(MacroAssembler* masm, StackFrame::Type type)
      : masm_(masm),
        type_(type),
        old_has_frame_(masm->has_frame()),
        old_constant_pool_available_(V8_EMBEDDED_CONSTANT_POOL_BOOL &&
                                     masm->is_constant_pool_available()) {
    masm->set_has_frame(true);
    if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
      masm->set_constant_pool_available(true);
    }
    if (type_ != StackFrame::MANUAL && type_ != StackFrame::NO_FRAME_TYPE) {
      masm->EnterFrame(type, !old_constant_pool_available_);
    }
  }

  ~FrameAndConstantPoolScope() {
    masm_->LeaveFrame(type_);
    masm_->set_has_frame(old_has_frame_);
    if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
      masm_->set_constant_pool_available(old_constant_pool_available_);
    }
  }

 private:
  MacroAssembler* masm_;
  StackFrame::Type type_;
  bool old_has_frame_;
  bool old_constant_pool_available_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FrameAndConstantPoolScope);
};

// Class for scoping the the unavailability of constant pool access.
class V8_NODISCARD ConstantPoolUnavailableScope {
 public:
  explicit ConstantPoolUnavailableScope(Assembler* assembler)
      : assembler_(assembler),
        old_constant_pool_available_(V8_EMBEDDED_CONSTANT_POOL_BOOL &&
                                     assembler->is_constant_pool_available()) {
    if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
      assembler->set_constant_pool_available(false);
    }
  }
  ~ConstantPoolUnavailableScope() {
    if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
      assembler_->set_constant_pool_available(old_constant_pool_available_);
    }
  }

 private:
  Assembler* assembler_;
  int old_constant_pool_available_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(ConstantPoolUnavailableScope);
};

class V8_NODISCARD AllowExternalCallThatCantCauseGC : public FrameScope {
 public:
  explicit AllowExternalCallThatCantCauseGC(MacroAssembler* masm)
      : FrameScope(masm, StackFrame::NO_FRAME_TYPE) {}
};

// Prevent the use of the RootArray during the lifetime of this
// scope object.
class V8_NODISCARD NoRootArrayScope {
 public:
  explicit NoRootArrayScope(MacroAssembler* masm)
      : masm_(masm), old_value_(masm->root_array_available()) {
    masm->set_root_array_available(false);
  }

  ~NoRootArrayScope() { masm_->set_root_array_available(old_value_); }

 private:
  MacroAssembler* masm_;
  bool old_value_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_MACRO_ASSEMBLER_H_
