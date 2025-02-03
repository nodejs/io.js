// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_LIMITS_H_
#define V8_WASM_WASM_LIMITS_H_

#include <cstddef>
#include <cstdint>
#include <limits>

#include "src/base/macros.h"
#include "src/wasm/wasm-constants.h"

namespace v8::internal::wasm {

// These constants limit the amount of *declared* memory. At runtime, memory can
// only grow up to kV8MaxWasmMemory{32,64}Pages.
constexpr size_t kSpecMaxMemory32Pages = 65'536;  // 4GB
// TODO(clemensb): Adapt once the spec defines a limit here. For now, use 16GB.
constexpr size_t kSpecMaxMemory64Pages = 262'144;  // 16GB

// The following limits are imposed by V8 on WebAssembly modules.
// The limits are agreed upon with other engines for consistency.
constexpr size_t kV8MaxWasmTypes = 1'000'000;
constexpr size_t kV8MaxWasmDefinedFunctions = 1'000'000;
constexpr size_t kV8MaxWasmImports = 100'000;
constexpr size_t kV8MaxWasmExports = 100'000;
constexpr size_t kV8MaxWasmGlobals = 1'000'000;
constexpr size_t kV8MaxWasmTags = 1'000'000;
constexpr size_t kV8MaxWasmExceptionTypes = 1'000'000;
constexpr size_t kV8MaxWasmDataSegments = 100'000;
// This indicates the maximum memory size our implementation supports.
// Do not use this limit directly; use {max_mem{32,64}_pages()} instead to take
// the spec'ed limit as well as command line flag into account.
// Also, do not use this limit to validate declared memory, use
// kSpecMaxMemory{32,64}Pages for that.
constexpr size_t kV8MaxWasmMemory32Pages = kSystemPointerSize == 4
                                               ? 32'767   // = 2 GiB - 64Kib
                                               : 65'536;  // = 4 GiB
constexpr size_t kV8MaxWasmMemory64Pages = kSystemPointerSize == 4
                                               ? 32'767    // = 2 GiB - 64Kib
                                               : 262'144;  // = 16 GiB
constexpr size_t kV8MaxWasmStringSize = 100'000;
constexpr size_t kV8MaxWasmModuleSize = 1024 * 1024 * 1024;  // = 1 GiB
constexpr size_t kV8MaxWasmFunctionSize = 7'654'321;
constexpr size_t kV8MaxWasmFunctionLocals = 50'000;
constexpr size_t kV8MaxWasmFunctionParams = 1'000;
constexpr size_t kV8MaxWasmFunctionReturns = 1'000;
constexpr size_t kV8MaxWasmFunctionBrTableSize = 65'520;
// Don't use this limit directly, but use the value of
// v8_flags.wasm_max_table_size.
constexpr size_t kV8MaxWasmTableSize = 10'000'000;
constexpr size_t kV8MaxWasmTableInitEntries = 10'000'000;
constexpr size_t kV8MaxWasmTables = 100'000;
constexpr size_t kV8MaxWasmMemories = 100'000;

// GC proposal.
constexpr size_t kV8MaxWasmStructFields = 10'000;
constexpr uint32_t kV8MaxRttSubtypingDepth = 63;
constexpr size_t kV8MaxWasmArrayNewFixedLength = 10'000;

// Stringref proposal. This limit is not standardized yet.
constexpr size_t kV8MaxWasmStringLiterals = 1'000'000;

static_assert(kV8MaxWasmTableSize <= 4294967295,  // 2^32 - 1
              "v8 should not exceed WebAssembly's non-web embedding limits");
static_assert(kV8MaxWasmTableInitEntries <= kV8MaxWasmTableSize,
              "JS-API should not exceed v8's limit");

constexpr uint64_t kWasmMaxHeapOffset =
    static_cast<uint64_t>(
        std::numeric_limits<uint32_t>::max())  // maximum base value
    + std::numeric_limits<uint32_t>::max();    // maximum index value

// This limit is a result of the limits for defined functions and the maximum of
// imported functions.
constexpr size_t kV8MaxWasmTotalFunctions =
    kV8MaxWasmDefinedFunctions + kV8MaxWasmImports;

// The following functions are defined in wasm-engine.cc.

// Maximum number of pages we can allocate, for memory32 and memory64. This
// might be lower than the number of pages that can be declared (e.g. as
// maximum): kSpecMaxMemory{32,64}Pages.
// Even for 64-bit memory, the number of pages is still a 32-bit number for now,
// which allows for up to 128 TB memories (2**31 * 64k).
static_assert(kV8MaxWasmMemory64Pages <= kMaxUInt32);
V8_EXPORT_PRIVATE uint32_t max_mem32_pages();
V8_EXPORT_PRIVATE uint32_t max_mem64_pages();

inline uint64_t max_mem32_bytes() {
  return uint64_t{max_mem32_pages()} * kWasmPageSize;
}

inline uint64_t max_mem64_bytes() {
  return uint64_t{max_mem64_pages()} * kWasmPageSize;
}

V8_EXPORT_PRIVATE uint32_t max_table_init_entries();
V8_EXPORT_PRIVATE size_t max_module_size();

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WASM_LIMITS_H_
