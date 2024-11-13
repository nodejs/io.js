// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-call-descriptors.h"

#include "src/common/globals.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler {

WasmCallDescriptors::WasmCallDescriptors(AccountingAllocator* allocator)
    : zone_(new Zone(allocator, "wasm_call_descriptors")) {
  bigint_to_i64_descriptor_ = compiler::GetBuiltinCallDescriptor(
      Builtin::kBigIntToI64, zone_.get(), StubCallMode::kCallBuiltinPointer);
  bigint_to_i64_descriptor_with_framestate_ =
      compiler::GetBuiltinCallDescriptor(Builtin::kBigIntToI64, zone_.get(),
                                         StubCallMode::kCallBuiltinPointer,
                                         true);
#if V8_TARGET_ARCH_32_BIT
  bigint_to_i32pair_descriptor_ =
      compiler::GetBuiltinCallDescriptor(Builtin::kBigIntToI32Pair, zone_.get(),
                                         StubCallMode::kCallBuiltinPointer);
  bigint_to_i32pair_descriptor_with_framestate_ =
      compiler::GetBuiltinCallDescriptor(Builtin::kBigIntToI32Pair, zone_.get(),
                                         StubCallMode::kCallBuiltinPointer,
                                         true);
#endif  // V8_TARGET_ARCH_32_BIT
}

#if V8_TARGET_ARCH_32_BIT
compiler::CallDescriptor* WasmCallDescriptors::GetLoweredCallDescriptor(
    const compiler::CallDescriptor* original) {
  if (original == bigint_to_i64_descriptor_) {
    return bigint_to_i32pair_descriptor_;
  }
  if (original == bigint_to_i64_descriptor_with_framestate_) {
    return bigint_to_i32pair_descriptor_with_framestate_;
  }
  return nullptr;
}
#endif  // V8_TARGET_ARCH_32_BIT

}  // namespace v8::internal::compiler
