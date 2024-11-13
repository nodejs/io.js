// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_DISASSEMBLER_H_
#define V8_WASM_WASM_DISASSEMBLER_H_

#include "src/wasm/wasm-module.h"

namespace v8 {

namespace debug {
class DisassemblyCollector;
}  // namespace debug

namespace internal {
namespace wasm {

class NamesProvider;

void Disassemble(const WasmModule* module, ModuleWireBytes wire_bytes,
                 NamesProvider* names,
                 v8::debug::DisassemblyCollector* collector,
                 std::vector<int>* function_body_offsets);

void Disassemble(base::Vector<const uint8_t> wire_bytes,
                 v8::debug::DisassemblyCollector* collector,
                 std::vector<int>* function_body_offsets);

// Prefer this version if you have the required inputs.
void DisassembleFunction(const WasmModule* module, int func_index,
                         base::Vector<const uint8_t> wire_bytes,
                         NamesProvider* names, std::ostream& os);

// Use this version when you don't have ModuleWireBytes or a NamesProvider,
// i.e. during streaming compilation.
void DisassembleFunction(const WasmModule* module, int func_index,
                         base::Vector<const uint8_t> function_body,
                         base::Vector<const uint8_t> maybe_wire_bytes,
                         uint32_t function_body_offset, std::ostream& os,
                         std::vector<uint32_t>* offsets = nullptr);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_DISASSEMBLER_H_
