// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-opcodes.h"

#include <array>

#include "src/codegen/signature.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

std::ostream& operator<<(std::ostream& os, const FunctionSig& sig) {
  if (sig.return_count() == 0) os << "v";
  for (auto ret : sig.returns()) {
    os << ret.short_name();
  }
  os << "_";
  if (sig.parameter_count() == 0) os << "v";
  for (auto param : sig.parameters()) {
    os << param.short_name();
  }
  return os;
}

bool IsJSCompatibleSignature(const FunctionSig* sig) {
  for (auto type : sig->all()) {
    // Rtts are internal-only. They should never be part of a signature.
    DCHECK(!type.is_rtt());
    if (type == kWasmS128) return false;
    if (type == kWasmExnRef) return false;
    if (type.is_object_reference()) {
      switch (type.heap_representation_non_shared()) {
        case HeapType::kStringViewWtf8:
        case HeapType::kStringViewWtf16:
        case HeapType::kStringViewIter:
          return false;
        default:
          break;
      }
    }
  }
  return true;
}

// Define constexpr arrays.
constexpr uint8_t LoadType::kLoadSizeLog2[];
constexpr ValueType LoadType::kValueType[];
constexpr MachineType LoadType::kMemType[];
constexpr uint8_t StoreType::kStoreSizeLog2[];
constexpr ValueType StoreType::kValueType[];
constexpr MachineRepresentation StoreType::kMemRep[];

}  // namespace wasm
}  // namespace internal
}  // namespace v8
