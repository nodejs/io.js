// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LINKAGE_H_
#define V8_COMPILER_LINKAGE_H_

#include "src/base/compiler-specific.h"
#include "src/base/flags.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/linkage-location.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register.h"
#include "src/codegen/reglist.h"
#include "src/codegen/signature.h"
#include "src/common/globals.h"
#include "src/compiler/frame.h"
#include "src/compiler/operator.h"
#include "src/execution/encoded-c-signature.h"
#include "src/runtime/runtime.h"
#include "src/zone/zone.h"

namespace v8 {
class CFunctionInfo;

namespace internal {

class CallInterfaceDescriptor;
class OptimizedCompilationInfo;

namespace compiler {

constexpr RegList kNoCalleeSaved;
constexpr DoubleRegList kNoCalleeSavedFp;

class OsrHelper;

// Describes a call to various parts of the compiler. Every call has the notion
// of a "target", which is the first input to the call.
class V8_EXPORT_PRIVATE CallDescriptor final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  // Describes the kind of this call, which determines the target.
  enum Kind {
    kCallCodeObject,         // target is a Code object
    kCallJSFunction,         // target is a JSFunction object
    kCallAddress,            // target is a machine pointer
#if V8_ENABLE_WEBASSEMBLY    // ↓ WebAssembly only
    kCallWasmCapiFunction,   // target is a Wasm C API function
    kCallWasmFunction,       // target is a wasm function
    kCallWasmImportWrapper,  // target is a wasm import wrapper
#endif                       // ↑ WebAssembly only
    kCallBuiltinPointer,     // target is a builtin pointer
  };

  // NOTE: The lowest 10 bits of the Flags field are encoded in InstructionCode
  // (for use in the code generator). All higher bits are lost.
  static constexpr int kFlagsBitsEncodedInInstructionCode = 10;
  enum Flag {
    kNoFlags = 0u,
    kNeedsFrameState = 1u << 0,
    kHasExceptionHandler = 1u << 1,
    kCanUseRoots = 1u << 2,
    // Causes the code generator to initialize the root register.
    kInitializeRootRegister = 1u << 3,
    // Does not ever try to allocate space on our heap.
    kNoAllocate = 1u << 4,
    // Use the kJavaScriptCallCodeStartRegister (fixed) register for the
    // indirect target address when calling.
    kFixedTargetRegister = 1u << 5,
    kCallerSavedRegisters = 1u << 6,
    // The kCallerSavedFPRegisters only matters (and set) when the more general
    // flag for kCallerSavedRegisters above is also set.
    kCallerSavedFPRegisters = 1u << 7,
    // Tail calls for tier up are special (in fact they are different enough
    // from normal tail calls to warrant a dedicated opcode; but they also have
    // enough similar aspects that reusing the TailCall opcode is pragmatic).
    // Specifically:
    //
    // 1. Caller and callee are both JS-linkage Code objects.
    // 2. JS runtime arguments are passed unchanged from caller to callee.
    // 3. JS runtime arguments are not attached as inputs to the TailCall node.
    // 4. Prior to the tail call, frame and register state is torn down to just
    //    before the caller frame was constructed.
    // 5. Unlike normal tail calls, inlined arguments frames (if present) are
    //    *not* torn down.
    //
    // In other words, behavior is identical to a jmp instruction prior caller
    // frame construction.
    kIsTailCallForTierUp = 1u << 8,

    // AIX has a function descriptor by default but it can be disabled for a
    // certain CFunction call (only used for Kind::kCallAddress).
    kNoFunctionDescriptor = 1u << 9,

    // Flags past here are *not* encoded in InstructionCode and are thus not
    // accessible from the code generator. See also
    // kFlagsBitsEncodedInInstructionCode.
  };
  using Flags = base::Flags<Flag>;

  CallDescriptor(Kind kind, CodeEntrypointTag tag, MachineType target_type,
                 LinkageLocation target_loc, LocationSignature* location_sig,
                 size_t param_slot_count, Operator::Properties properties,
                 RegList callee_saved_registers,
                 DoubleRegList callee_saved_fp_registers, Flags flags,
                 const char* debug_name = "",
                 StackArgumentOrder stack_order = StackArgumentOrder::kDefault,
                 const RegList allocatable_registers = {},
                 size_t return_slot_count = 0)
      : kind_(kind),
        tag_(tag),
        target_type_(target_type),
        target_loc_(target_loc),
        location_sig_(location_sig),
        param_slot_count_(param_slot_count),
        return_slot_count_(return_slot_count),
        properties_(properties),
        callee_saved_registers_(callee_saved_registers),
        callee_saved_fp_registers_(callee_saved_fp_registers),
        allocatable_registers_(allocatable_registers),
        flags_(flags),
        stack_order_(stack_order),
        debug_name_(debug_name) {}

  CallDescriptor(const CallDescriptor&) = delete;
  CallDescriptor& operator=(const CallDescriptor&) = delete;

  // Returns the kind of this call.
  Kind kind() const { return kind_; }

  // Returns the entrypoint tag for this call.
  CodeEntrypointTag tag() const { return tag_; }

  // Returns the entrypoint tag for this call, shifted to the right by
  // kCodeEntrypointTagShift so that it fits into a 32-bit immediate.
  uint32_t shifted_tag() const {
    static_assert(kCodeEntrypointTagShift >= 32);
    return tag_ >> kCodeEntrypointTagShift;
  }

  // Returns {true} if this descriptor is a call to a Code object.
  bool IsCodeObjectCall() const { return kind_ == kCallCodeObject; }

  // Returns {true} if this descriptor is a call to a C function.
  bool IsCFunctionCall() const { return kind_ == kCallAddress; }

  // Returns {true} if this descriptor is a call to a JSFunction.
  bool IsJSFunctionCall() const { return kind_ == kCallJSFunction; }

#if V8_ENABLE_WEBASSEMBLY
  // Returns {true} if this descriptor is a call to a WebAssembly function.
  bool IsWasmFunctionCall() const { return kind_ == kCallWasmFunction; }

  // Returns {true} if this descriptor is a call to a WebAssembly function.
  bool IsWasmImportWrapper() const { return kind_ == kCallWasmImportWrapper; }

  // Returns {true} if this descriptor is a call to a Wasm C API function.
  bool IsWasmCapiFunction() const { return kind_ == kCallWasmCapiFunction; }
#endif  // V8_ENABLE_WEBASSEMBLY

  bool IsBuiltinPointerCall() const { return kind_ == kCallBuiltinPointer; }

  bool RequiresFrameAsIncoming() const {
    if (IsCFunctionCall() || IsJSFunctionCall()) return true;
#if V8_ENABLE_WEBASSEMBLY
    if (IsWasmFunctionCall()) return true;
#endif  // V8_ENABLE_WEBASSEMBLY
    if (CalleeSavedRegisters() != kNoCalleeSaved) return true;
    return false;
  }

  bool RequiresEntrypointTagForCall() const { return IsCodeObjectCall(); }

  // The number of return values from this call.
  size_t ReturnCount() const { return location_sig_->return_count(); }

  // The number of C parameters to this call. The following invariant
  // should hold true:
  // ParameterCount() == GPParameterCount() + FPParameterCount()
  size_t ParameterCount() const { return location_sig_->parameter_count(); }

  // The number of general purpose C parameters to this call.
  size_t GPParameterCount() const {
    if (!gp_param_count_) {
      ComputeParamCounts();
    }
    return gp_param_count_.value();
  }

  // The number of floating point C parameters to this call.
  size_t FPParameterCount() const {
    if (!fp_param_count_) {
      ComputeParamCounts();
    }
    return fp_param_count_.value();
  }

  // The number of stack parameter slots to the call.
  size_t ParameterSlotCount() const { return param_slot_count_; }

  // The number of stack return value slots from the call.
  size_t ReturnSlotCount() const { return return_slot_count_; }

  // The number of parameters to the JS function call.
  size_t JSParameterCount() const {
    DCHECK(IsJSFunctionCall());
    return param_slot_count_;
  }

  int GetStackIndexFromSlot(int slot_index) const {
    switch (GetStackArgumentOrder()) {
      case StackArgumentOrder::kDefault:
        return -slot_index - 1;
      case StackArgumentOrder::kJS:
        return slot_index + static_cast<int>(ParameterSlotCount());
    }
  }

  // The total number of inputs to this call, which includes the target,
  // receiver, context, etc.
  // TODO(titzer): this should input the framestate input too.
  size_t InputCount() const { return 1 + location_sig_->parameter_count(); }

  size_t FrameStateCount() const { return NeedsFrameState() ? 1 : 0; }

  Flags flags() const { return flags_; }

  bool NeedsFrameState() const { return flags() & kNeedsFrameState; }
  bool InitializeRootRegister() const {
    return flags() & kInitializeRootRegister;
  }
  bool NeedsCallerSavedRegisters() const {
    return flags() & kCallerSavedRegisters;
  }
  bool NeedsCallerSavedFPRegisters() const {
    return flags() & kCallerSavedFPRegisters;
  }
  bool IsTailCallForTierUp() const { return flags() & kIsTailCallForTierUp; }
  bool NoFunctionDescriptor() const { return flags() & kNoFunctionDescriptor; }

  LinkageLocation GetReturnLocation(size_t index) const {
    return location_sig_->GetReturn(index);
  }

  LinkageLocation GetInputLocation(size_t index) const {
    if (index == 0) return target_loc_;
    return location_sig_->GetParam(index - 1);
  }

  MachineSignature* GetMachineSignature(Zone* zone) const;

  MachineType GetReturnType(size_t index) const {
    return location_sig_->GetReturn(index).GetType();
  }

  MachineType GetInputType(size_t index) const {
    if (index == 0) return target_type_;
    return location_sig_->GetParam(index - 1).GetType();
  }

  MachineType GetParameterType(size_t index) const {
    return location_sig_->GetParam(index).GetType();
  }

  StackArgumentOrder GetStackArgumentOrder() const { return stack_order_; }

  // Operator properties describe how this call can be optimized, if at all.
  Operator::Properties properties() const { return properties_; }

  // Get the callee-saved registers, if any, across this call.
  RegList CalleeSavedRegisters() const { return callee_saved_registers_; }

  // Get the callee-saved FP registers, if any, across this call.
  DoubleRegList CalleeSavedFPRegisters() const {
    return callee_saved_fp_registers_;
  }

  const char* debug_name() const { return debug_name_; }

  // Difference between the number of parameter slots of *this* and
  // *tail_caller* (callee minus caller).
  int GetStackParameterDelta(const CallDescriptor* tail_caller) const;

  // Returns the offset to the area below the parameter slots on the stack,
  // relative to callee slot 0, the return address. If there are no parameter
  // slots, returns +1.
  int GetOffsetToFirstUnusedStackSlot() const;

  // Returns the offset to the area above the return slots on the stack,
  // relative to callee slot 0, the return address. If there are no return
  // slots, returns the offset to the lowest slot of the parameter area.
  // If there are no parameter slots, returns 0.
  int GetOffsetToReturns() const;

  // Returns two 16-bit numbers packed together: (first slot << 16) | num_slots.
  uint32_t GetTaggedParameterSlots() const;

  bool CanTailCall(const CallDescriptor* callee) const;

  int CalculateFixedFrameSize(CodeKind code_kind) const;

  RegList AllocatableRegisters() const { return allocatable_registers_; }

  bool HasRestrictedAllocatableRegisters() const {
    return !allocatable_registers_.is_empty();
  }

  EncodedCSignature ToEncodedCSignature() const;

 private:
  void ComputeParamCounts() const;

  friend class Linkage;

  const Kind kind_;
  const CodeEntrypointTag tag_;
  const MachineType target_type_;
  const LinkageLocation target_loc_;
  const LocationSignature* const location_sig_;
  const size_t param_slot_count_;
  const size_t return_slot_count_;
  const Operator::Properties properties_;
  const RegList callee_saved_registers_;
  const DoubleRegList callee_saved_fp_registers_;
  // Non-zero value means restricting the set of allocatable registers for
  // register allocator to use.
  const RegList allocatable_registers_;
  const Flags flags_;
  const StackArgumentOrder stack_order_;
  const char* const debug_name_;

  mutable base::Optional<size_t> gp_param_count_;
  mutable base::Optional<size_t> fp_param_count_;
};

DEFINE_OPERATORS_FOR_FLAGS(CallDescriptor::Flags)

std::ostream& operator<<(std::ostream& os, const CallDescriptor& d);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const CallDescriptor::Kind& k);

// Defines the linkage for a compilation, including the calling conventions
// for incoming parameters and return value(s) as well as the outgoing calling
// convention for any kind of call. Linkage is generally architecture-specific.
//
// Can be used to translate {arg_index} (i.e. index of the call node input) as
// well as {param_index} (i.e. as stored in parameter nodes) into an operator
// representing the architecture-specific location. The following call node
// layouts are supported (where {n} is the number of value inputs):
//
//                        #0          #1     #2     [...]             #n
// Call[CodeStub]         code,       arg 1, arg 2, [...],            context
// Call[JSFunction]       function,   rcvr,  arg 1, [...], new, #arg, context
// Call[Runtime]          CEntry,     arg 1, arg 2, [...], fun, #arg, context
// Call[BytecodeDispatch] address,    arg 1, arg 2, [...]
class V8_EXPORT_PRIVATE Linkage : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit Linkage(CallDescriptor* incoming) : incoming_(incoming) {}
  Linkage(const Linkage&) = delete;
  Linkage& operator=(const Linkage&) = delete;

  static CallDescriptor* ComputeIncoming(Zone* zone,
                                         OptimizedCompilationInfo* info);

  // The call descriptor for this compilation unit describes the locations
  // of incoming parameters and the outgoing return value(s).
  CallDescriptor* GetIncomingDescriptor() const { return incoming_; }
  // Calls to JSFunctions should never overwrite the {properties}, but calls to
  // known builtins might.
  static CallDescriptor* GetJSCallDescriptor(
      Zone* zone, bool is_osr, int parameter_count, CallDescriptor::Flags flags,
      Operator::Properties properties =
          Operator::kNoProperties /* use with care! */);

  static CallDescriptor* GetRuntimeCallDescriptor(
      Zone* zone, Runtime::FunctionId function, int js_parameter_count,
      Operator::Properties properties, CallDescriptor::Flags flags);

  static CallDescriptor* GetCEntryStubCallDescriptor(
      Zone* zone, int return_count, int js_parameter_count,
      const char* debug_name, Operator::Properties properties,
      CallDescriptor::Flags flags,
      StackArgumentOrder stack_order = StackArgumentOrder::kDefault);

  static CallDescriptor* GetStubCallDescriptor(
      Zone* zone, const CallInterfaceDescriptor& descriptor,
      int stack_parameter_count, CallDescriptor::Flags flags,
      Operator::Properties properties = Operator::kNoProperties,
      StubCallMode stub_mode = StubCallMode::kCallCodeObject);

  static CallDescriptor* GetBytecodeDispatchCallDescriptor(
      Zone* zone, const CallInterfaceDescriptor& descriptor,
      int stack_parameter_count);

  // Creates a call descriptor for simplified C calls that is appropriate
  // for the host platform. This simplified calling convention only supports
  // integers and pointers of one word size each, i.e. no floating point,
  // structs, pointers to members, etc.
  static CallDescriptor* GetSimplifiedCDescriptor(
      Zone* zone, const MachineSignature* sig,
      CallDescriptor::Flags flags = CallDescriptor::kNoFlags);

  // Get the location of an (incoming) parameter to this function.
  LinkageLocation GetParameterLocation(int index) const {
    return incoming_->GetInputLocation(index + 1);  // + 1 to skip target.
  }

  // Get the machine type of an (incoming) parameter to this function.
  MachineType GetParameterType(int index) const {
    return incoming_->GetInputType(index + 1);  // + 1 to skip target.
  }

  // Get the location where this function should place its return value.
  LinkageLocation GetReturnLocation(size_t index = 0) const {
    return incoming_->GetReturnLocation(index);
  }

  // Get the machine type of this function's return value.
  MachineType GetReturnType(size_t index = 0) const {
    return incoming_->GetReturnType(index);
  }

  bool ParameterHasSecondaryLocation(int index) const;
  LinkageLocation GetParameterSecondaryLocation(int index) const;

  static bool NeedsFrameStateInput(Runtime::FunctionId function);

  // Get the location where an incoming OSR value is stored.
  LinkageLocation GetOsrValueLocation(int index) const;

  // A special {Parameter} index for Stub Calls that represents context.
  static int GetStubCallContextParamIndex(int parameter_count) {
    return parameter_count + 0;  // Parameter (arity + 0) is special.
  }

  // A special {Parameter} index for JSCalls that represents the new target.
  static constexpr int GetJSCallNewTargetParamIndex(int parameter_count) {
    return parameter_count + 0;  // Parameter (arity + 0) is special.
  }

  // A special {Parameter} index for JSCalls that represents the argument count.
  static constexpr int GetJSCallArgCountParamIndex(int parameter_count) {
    return parameter_count + 1;  // Parameter (arity + 1) is special.
  }

  // A special {Parameter} index for JSCalls that represents the context.
  static constexpr int GetJSCallContextParamIndex(int parameter_count) {
    return parameter_count + 2;  // Parameter (arity + 2) is special.
  }

  // A special {Parameter} index for JSCalls that represents the closure.
  static constexpr int kJSCallClosureParamIndex = kJSCallClosureParameterIndex;
  static_assert(kJSCallClosureParamIndex == -1);

  // A special {OsrValue} index to indicate the context spill slot.
  static const int kOsrContextSpillSlotIndex = -1;

  // A special {OsrValue} index to indicate the accumulator register.
  static const int kOsrAccumulatorRegisterIndex = -1;

 private:
  CallDescriptor* const incoming_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8
#undef NO_INLINE_FOR_ARM64_MSVC

#endif  // V8_COMPILER_LINKAGE_H_
