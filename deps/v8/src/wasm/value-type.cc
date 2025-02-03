// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/value-type.h"

#include "src/codegen/signature.h"
#include "src/utils/utils.h"

namespace v8::internal::wasm {

std::optional<wasm::ValueKind> WasmReturnTypeFromSignature(
    const FunctionSig* wasm_signature) {
  if (wasm_signature->return_count() == 0) return {};

  DCHECK_EQ(wasm_signature->return_count(), 1);
  ValueType return_type = wasm_signature->GetReturn(0);
  return {return_type.kind()};
}

#if DEBUG
V8_EXPORT_PRIVATE extern void PrintFunctionSig(const wasm::FunctionSig* sig) {
  std::ostringstream os;
  os << sig->parameter_count() << " parameters:\n";
  for (size_t i = 0; i < sig->parameter_count(); i++) {
    os << "  " << i << ": " << sig->GetParam(i) << "\n";
  }
  os << sig->return_count() << " returns:\n";
  for (size_t i = 0; i < sig->return_count(); i++) {
    os << "  " << i << ": " << sig->GetReturn() << "\n";
  }
  PrintF("%s", os.str().c_str());
}
#endif

namespace {
const wasm::FunctionSig* ReplaceTypeInSig(Zone* zone,
                                          const wasm::FunctionSig* sig,
                                          wasm::ValueType from,
                                          wasm::ValueType to,
                                          size_t num_replacements) {
  size_t param_occurences =
      std::count(sig->parameters().begin(), sig->parameters().end(), from);
  size_t return_occurences =
      std::count(sig->returns().begin(), sig->returns().end(), from);
  if (param_occurences == 0 && return_occurences == 0) return sig;

  wasm::FunctionSig::Builder builder(
      zone, sig->return_count() + return_occurences * (num_replacements - 1),
      sig->parameter_count() + param_occurences * (num_replacements - 1));

  for (wasm::ValueType ret : sig->returns()) {
    if (ret == from) {
      for (size_t i = 0; i < num_replacements; i++) builder.AddReturn(to);
    } else {
      builder.AddReturn(ret);
    }
  }

  for (wasm::ValueType param : sig->parameters()) {
    if (param == from) {
      for (size_t i = 0; i < num_replacements; i++) builder.AddParam(to);
    } else {
      builder.AddParam(param);
    }
  }

  return builder.Build();
}
}  // namespace

const wasm::FunctionSig* GetI32Sig(Zone* zone, const wasm::FunctionSig* sig) {
  return ReplaceTypeInSig(zone, sig, wasm::kWasmI64, wasm::kWasmI32, 2);
}

}  // namespace v8::internal::wasm
