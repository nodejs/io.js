// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_MEMORY_TRACING_H_
#define V8_WASM_MEMORY_TRACING_H_

#include <cstdint>
#include <optional>

#include "src/codegen/machine-type.h"
#include "src/wasm/wasm-tier.h"

namespace v8::internal::wasm {

// This struct is create in generated code, hence use low-level types.
struct MemoryTracingInfo {
  uintptr_t offset;
  uint8_t is_store;  // 0 or 1
  uint8_t mem_rep;
  static_assert(
      std::is_same<decltype(mem_rep),
                   std::underlying_type<MachineRepresentation>::type>::value,
      "MachineRepresentation uses uint8_t");

  MemoryTracingInfo(uintptr_t offset, bool is_store, MachineRepresentation rep)
      : offset(offset),
        is_store(is_store),
        mem_rep(static_cast<uint8_t>(rep)) {}
};

// Callback for tracing a memory operation for debugging.
// Triggered by --wasm-trace-memory.
V8_EXPORT_PRIVATE void TraceMemoryOperation(std::optional<ExecutionTier>,
                                            const MemoryTracingInfo* info,
                                            int func_index, int position,
                                            uint8_t* mem_start);

}  // namespace v8::internal::wasm

#endif  // V8_WASM_MEMORY_TRACING_H_
