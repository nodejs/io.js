// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_
#define V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_

#include <ostream>

#include "src/base/functional.h"
#include "src/base/vector.h"
#include "src/codegen/linkage-location.h"
#include "src/codegen/register.h"
#include "src/codegen/signature.h"
#include "src/wasm/signature-hashing.h"
#include "src/wasm/value-type.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

namespace wasm {
struct WasmModule;
class WireBytesStorage;
struct ModuleWireBytes;
}  // namespace wasm

namespace compiler {
class CallDescriptor;

// If {to} is nullable, it means that null passes the check.
// {from} may change in compiler optimization passes as the object's type gets
// narrowed.
// TODO(12166): Add modules if we have cross-module inlining.
struct WasmTypeCheckConfig {
  wasm::ValueType from;
  const wasm::ValueType to;
};

V8_INLINE std::ostream& operator<<(std::ostream& os,
                                   WasmTypeCheckConfig const& p) {
  return os << p.from.name() << " -> " << p.to.name();
}

V8_INLINE size_t hash_value(WasmTypeCheckConfig const& p) {
  return base::hash_combine(p.from.raw_bit_field(), p.to.raw_bit_field());
}

V8_INLINE bool operator==(const WasmTypeCheckConfig& p1,
                          const WasmTypeCheckConfig& p2) {
  return p1.from == p2.from && p1.to == p2.to;
}

static constexpr int kCharWidthBailoutSentinel = 3;

enum class NullCheckStrategy { kExplicit, kTrapHandler };

enum class EnforceBoundsCheck : bool {  // --
  kNeedsBoundsCheck = true,
  kCanOmitBoundsCheck = false
};

enum class AlignmentCheck : bool {  // --
  kYes = true,
  kNo = false,
};

enum class BoundsCheckResult {
  // Dynamically checked (using 1-2 conditional branches).
  kDynamicallyChecked,
  // OOB handled via the trap handler.
  kTrapHandler,
  // Statically known to be in bounds.
  kInBounds
};

// Static knowledge about whether a wasm-gc operation, such as struct.get, needs
// a null check.
enum CheckForNull : bool { kWithoutNullCheck, kWithNullCheck };
std::ostream& operator<<(std::ostream& os, CheckForNull null_check);

base::Vector<const char> GetDebugName(Zone* zone,
                                      const wasm::WasmModule* module,
                                      const wasm::WireBytesStorage* wire_bytes,
                                      int index);
enum WasmCallKind { kWasmFunction, kWasmImportWrapper, kWasmCapiFunction };

V8_EXPORT_PRIVATE CallDescriptor* GetWasmCallDescriptor(
    Zone* zone, const wasm::FunctionSig* signature,
    WasmCallKind kind = kWasmFunction, bool need_frame_state = false);

template <typename T>
LocationSignature* BuildLocations(Zone* zone, const Signature<T>* sig,
                                  bool extra_callable_param,
                                  int* parameter_slots, int* return_slots) {
  int extra_params = extra_callable_param ? 2 : 1;
  LocationSignature::Builder locations(zone, sig->return_count(),
                                       sig->parameter_count() + extra_params);
  int untagged_parameter_slots;  // Unused.
  int untagged_return_slots;     // Unused.
  wasm::IterateSignatureImpl(sig, extra_callable_param, locations,
                             &untagged_parameter_slots, parameter_slots,
                             &untagged_return_slots, return_slots);
  return locations.Get();
}
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_
