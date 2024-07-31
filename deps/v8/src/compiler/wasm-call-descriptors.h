// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_CALL_DESCRIPTORS_H_
#define V8_COMPILER_WASM_CALL_DESCRIPTORS_H_

#include <memory>

#include "src/common/globals.h"

namespace v8::internal {

class AccountingAllocator;
class Zone;

namespace compiler {
class CallDescriptor;

class WasmCallDescriptors {
 public:
  explicit WasmCallDescriptors(AccountingAllocator* allocator);

  compiler::CallDescriptor* GetBigIntToI64Descriptor(StubCallMode mode,
                                                     bool needs_frame_state) {
    if (needs_frame_state) {
      DCHECK_EQ(mode, StubCallMode::kCallBuiltinPointer);
      return bigint_to_i64_descriptor_with_framestate_;
    }
    return bigint_to_i64_descriptors_[static_cast<size_t>(mode)];
  }

#if V8_TARGET_ARCH_32_BIT
  V8_EXPORT_PRIVATE compiler::CallDescriptor* GetLoweredCallDescriptor(
      const compiler::CallDescriptor* original);
#else
  V8_EXPORT_PRIVATE compiler::CallDescriptor* GetLoweredCallDescriptor(
      const compiler::CallDescriptor* original) {
    UNREACHABLE();
  }
#endif  // V8_TARGET_ARCH_32_BIT

 private:
  static_assert(static_cast<int>(StubCallMode::kCallCodeObject) == 0);
  static_assert(static_cast<int>(StubCallMode::kCallWasmRuntimeStub) == 1);
  static_assert(static_cast<int>(StubCallMode::kCallBuiltinPointer) == 2);
  static constexpr int kNumCallModes = 3;

  std::unique_ptr<Zone> zone_;

  compiler::CallDescriptor* bigint_to_i64_descriptors_[kNumCallModes];
  compiler::CallDescriptor* bigint_to_i64_descriptor_with_framestate_;

#if V8_TARGET_ARCH_32_BIT
  compiler::CallDescriptor* bigint_to_i32pair_descriptors_[kNumCallModes];
  compiler::CallDescriptor* bigint_to_i32pair_descriptor_with_framestate_;
#endif  // V8_TARGET_ARCH_32_BIT
};

}  // namespace compiler
}  // namespace v8::internal

#endif  // V8_COMPILER_WASM_CALL_DESCRIPTORS_H_
