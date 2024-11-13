// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_SIGNATURE_HASHING_H_
#define V8_WASM_SIGNATURE_HASHING_H_

#include "src/codegen/linkage-location.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/signature.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-linkage.h"

namespace v8::internal::wasm {

inline MachineRepresentation GetMachineRepresentation(ValueType type) {
  return type.machine_representation();
}

inline MachineRepresentation GetMachineRepresentation(MachineType type) {
  return type.representation();
}

// This shared helper ensures that {GetWasmCallDescriptor} and
// {SignatureHasher::Hash} remain in sync.
// The {SigType} type must match the {Signature} class, i.e. must support:
//    size_t parameter_count();
//    size_t return_count();
//    T GetParam(size_t index);   for T in {ValueType, MachineType}
//    T GetReturn(size_t index);  for T in {ValueType, MachineType}
// The {ResultCollector} type must match the {LocationSignature::Builder}
// class, i.e. must support:
//    void AddParamAt(size_t index, LinkageLocation location);
//    void AddReturnAt(size_t index, LinkageLocation location);
// {extra_callable_param} configures adding the implicit "callable" parameter
// that import call wrappers have, hard-coded to use the kJSFunctionRegister.
template <class ResultCollector, class SigType>
void IterateSignatureImpl(const SigType* sig, bool extra_callable_param,
                          ResultCollector& locations,
                          int* untagged_parameter_slots,
                          int* total_parameter_slots,
                          int* untagged_return_slots, int* total_return_slots) {
  constexpr int kParamsSlotOffset = 0;
  LinkageLocationAllocator params(kGpParamRegisters, kFpParamRegisters,
                                  kParamsSlotOffset);
  // The instance object.
  locations.AddParamAt(0, params.Next(MachineRepresentation::kTaggedPointer));
  const size_t param_offset = 1;  // Actual params start here.

  // Parameters are separated into two groups (first all untagged, then all
  // tagged parameters). This allows for easy iteration of tagged parameters
  // during frame iteration. It also allows for easy signature verification
  // based on counts.
  const size_t parameter_count = sig->parameter_count();
  bool has_tagged_param = false;
  for (size_t i = 0; i < parameter_count; i++) {
    MachineRepresentation param = GetMachineRepresentation(sig->GetParam(i));
    // Skip tagged parameters (e.g. any-ref).
    if (IsAnyTagged(param)) {
      has_tagged_param = true;
      continue;
    }
    locations.AddParamAt(i + param_offset, params.Next(param));
  }
  params.EndSlotArea();  // End the untagged area. Tagged slots come after.
  *untagged_parameter_slots = params.NumStackSlots();
  if (has_tagged_param) {
    for (size_t i = 0; i < parameter_count; i++) {
      MachineRepresentation param = GetMachineRepresentation(sig->GetParam(i));
      if (!IsAnyTagged(param)) continue;  // Skip untagged parameters.
      locations.AddParamAt(i + param_offset, params.Next(param));
    }
  }
  // Import call wrappers have an additional (implicit) parameter, the callable.
  // For consistency with JS, we use the JSFunction register.
  if (extra_callable_param) {
    locations.AddParamAt(
        parameter_count + param_offset,
        LinkageLocation::ForRegister(kJSFunctionRegister.code(),
                                     MachineType::TaggedPointer()));
  }
  int params_stack_height = AddArgumentPaddingSlots(params.NumStackSlots());
  *total_parameter_slots = params_stack_height;

  // Add return location(s).
  // For efficient signature verification, order results by taggedness, such
  // that all untagged results appear first in registers and on the stack,
  // followed by tagged results. That way, we can simply check the size of
  // each section, rather than needing a bit map.
  LinkageLocationAllocator rets(kGpReturnRegisters, kFpReturnRegisters,
                                params_stack_height);

  const size_t return_count = sig->return_count();
  bool has_tagged_result = false;
  for (size_t i = 0; i < return_count; i++) {
    MachineRepresentation ret = GetMachineRepresentation(sig->GetReturn(i));
    if (IsAnyTagged(ret)) {
      has_tagged_result = true;
      continue;
    }
    locations.AddReturnAt(i, rets.Next(ret));
  }
  rets.EndSlotArea();  // End the untagged area.
  *untagged_return_slots = rets.NumStackSlots();
  if (has_tagged_result) {
    for (size_t i = 0; i < return_count; i++) {
      MachineRepresentation ret = GetMachineRepresentation(sig->GetReturn(i));
      if (!IsAnyTagged(ret)) continue;
      locations.AddReturnAt(i, rets.Next(ret));
    }
  }
  *total_return_slots = rets.NumStackSlots();
}

#if V8_ENABLE_SANDBOX

// Computes a "signature hash" for sandbox hardening: two functions should have
// the same "signature hash" iff mixing them up (due to in-sandbox corruption)
// cannot possibly lead to a sandbox escape. That means in particular that we
// must ensure the following properties:
// - there must be no tagged/untagged mixups among parameters passed in GP
//   registers.
// - there must be no tagged/untagged mixups among parameters passed on the
//   stack.
// - there must be no mismatch in the sizes of the stack regions used for
//   passing parameters.
// - these same properties must hold for return values.
// To achieve this, we simulate the linkage locations that
// {GetWasmCallDescriptor} would assign, and collect the counts of
// tagged/untagged parameters in registers and on the stack, respectively.
class SignatureHasher {
 public:
  template <typename SigType>
  static uint64_t Hash(const SigType* sig) {
    SignatureHasher hasher;
    int total_param_stack_slots;
    int total_return_stack_slots;
    IterateSignatureImpl(
        sig, false /* no extra callable parameter */, hasher,
        &hasher.params_.untagged_on_stack_, &total_param_stack_slots,
        &hasher.rets_.untagged_on_stack_, &total_return_stack_slots);

    hasher.params_.tagged_on_stack_ =
        total_param_stack_slots - hasher.params_.untagged_on_stack_;
    hasher.rets_.tagged_on_stack_ =
        total_return_stack_slots - hasher.rets_.untagged_on_stack_;

    return hasher.GetHash();
  }

  void AddParamAt(size_t index, LinkageLocation location) {
    if (index == 0) return;  // Skip the instance object.
    CountIfRegister(location, params_);
  }
  void AddReturnAt(size_t index, LinkageLocation location) {
    CountIfRegister(location, rets_);
  }

 private:
  static constexpr int kUntaggedInRegBits = 3;
  static constexpr int kTaggedInRegBits = 3;
  static constexpr int kUntaggedOnStackBits = 11;
  static constexpr int kTaggedOnStackBits = 10;

  using UntaggedInReg =
      base::BitField<uint32_t, 0, kUntaggedInRegBits, uint64_t>;
  using TaggedInReg = UntaggedInReg::Next<uint32_t, kTaggedInRegBits>;
  using UntaggedOnStack = TaggedInReg::Next<uint32_t, kUntaggedOnStackBits>;
  using TaggedOnStack = UntaggedOnStack::Next<uint32_t, kTaggedOnStackBits>;

  static constexpr int kTotalWidth = TaggedOnStack::kLastUsedBit + 1;
  // Make sure we can return the full result (params + results) in a uint64_t.
  static_assert(kTotalWidth * 2 <= 64);

  // Make sure we chose the bit fields large enough.
  static_assert(arraysize(wasm::kGpParamRegisters) <=
                UntaggedInReg::kNumValues);
  static_assert(arraysize(wasm::kGpParamRegisters) <= TaggedInReg::kNumValues);
  static_assert(arraysize(wasm::kGpReturnRegisters) <=
                UntaggedInReg::kNumValues);
  static_assert(arraysize(wasm::kGpReturnRegisters) <= TaggedInReg::kNumValues);
  static constexpr int kMaxValueSizeInPointers =
      kMaxValueTypeSize / kSystemPointerSize;
  static_assert(wasm::kV8MaxWasmFunctionParams * kMaxValueSizeInPointers <=
                UntaggedOnStack::kNumValues);
  static_assert(wasm::kV8MaxWasmFunctionParams <= TaggedOnStack::kNumValues);
  static_assert(wasm::kV8MaxWasmFunctionReturns * kMaxValueSizeInPointers <=
                UntaggedOnStack::kNumValues);
  static_assert(wasm::kV8MaxWasmFunctionReturns <= TaggedOnStack::kNumValues);

  struct Counts {
    int tagged_in_reg_{0};
    int untagged_in_reg_{0};
    int tagged_on_stack_{0};
    int untagged_on_stack_{0};

    uint64_t GetHash() const {
      return UntaggedInReg::encode(untagged_in_reg_) |
             TaggedInReg::encode(tagged_in_reg_) |
             UntaggedOnStack::encode(untagged_on_stack_) |
             TaggedOnStack::encode(tagged_on_stack_);
    }
  };

  uint64_t GetHash() const {
    return (rets_.GetHash() << kTotalWidth) | params_.GetHash();
  }

  void CountIfRegister(LinkageLocation loc, Counts& counts) {
    if (!loc.IsRegister()) {
      DCHECK(loc.IsCallerFrameSlot());
      return;
    }
    MachineType type = loc.GetType();
    if (type.IsTagged()) {
      counts.tagged_in_reg_++;
    } else if (IsIntegral(type.representation())) {
      counts.untagged_in_reg_++;
    } else {
      DCHECK(IsFloatingPoint(type.representation()));
      // No need to count FP registers.
    }
  }

  Counts params_{};
  Counts rets_{};
};

#endif  // V8_ENABLE_SANDBOX

}  // namespace v8::internal::wasm

#endif  // V8_WASM_SIGNATURE_HASHING_H_
