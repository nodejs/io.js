// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-compiler-definitions.h"

#include "src/base/strings.h"
#include "src/codegen/signature.h"
#include "src/compiler/linkage.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-module.h"

namespace v8::internal::compiler {

base::Vector<const char> GetDebugName(Zone* zone,
                                      const wasm::WasmModule* module,
                                      const wasm::WireBytesStorage* wire_bytes,
                                      int index) {
  base::Optional<wasm::ModuleWireBytes> module_bytes =
      wire_bytes->GetModuleBytes();
  if (module_bytes.has_value() &&
      (v8_flags.trace_turbo || v8_flags.trace_turbo_scheduled ||
       v8_flags.trace_turbo_graph || v8_flags.print_wasm_code
#ifdef V8_ENABLE_WASM_SIMD256_REVEC
       || v8_flags.trace_wasm_revectorize
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
       )) {
    wasm::WireBytesRef name = module->lazily_generated_names.LookupFunctionName(
        module_bytes.value(), index);
    if (!name.is_empty()) {
      int name_len = name.length();
      char* index_name = zone->AllocateArray<char>(name_len);
      memcpy(index_name, module_bytes->start() + name.offset(), name_len);
      return base::Vector<const char>(index_name, name_len);
    }
  }

  constexpr int kBufferLength = 24;

  base::EmbeddedVector<char, kBufferLength> name_vector;
  int name_len = SNPrintF(name_vector, "wasm-function#%d", index);
  DCHECK(name_len > 0 && name_len < name_vector.length());

  char* index_name = zone->AllocateArray<char>(name_len);
  memcpy(index_name, name_vector.begin(), name_len);
  return base::Vector<const char>(index_name, name_len);
}

MachineRepresentation GetMachineRepresentation(wasm::ValueType type) {
  return type.machine_representation();
}

MachineRepresentation GetMachineRepresentation(MachineType type) {
  return type.representation();
}

// General code uses the above configuration data.
CallDescriptor* GetWasmCallDescriptor(Zone* zone, const wasm::FunctionSig* fsig,
                                      WasmCallKind call_kind,
                                      bool need_frame_state) {
  // The extra here is to accomodate the instance object as first parameter
  // and, when specified, the additional callable.
  bool extra_callable_param =
      call_kind == kWasmImportWrapper || call_kind == kWasmCapiFunction;

  int parameter_slots;
  int return_slots;
  LocationSignature* location_sig = BuildLocations(
      zone, fsig, extra_callable_param, &parameter_slots, &return_slots);

  const RegList kCalleeSaveRegisters;
  const DoubleRegList kCalleeSaveFPRegisters;

  // The target for wasm calls is always a code object.
  MachineType target_type = MachineType::Pointer();
  LinkageLocation target_loc = LinkageLocation::ForAnyRegister(target_type);

  CallDescriptor::Kind descriptor_kind;
  if (call_kind == kWasmFunction) {
    descriptor_kind = CallDescriptor::kCallWasmFunction;
  } else if (call_kind == kWasmImportWrapper) {
    descriptor_kind = CallDescriptor::kCallWasmImportWrapper;
  } else {
    DCHECK_EQ(call_kind, kWasmCapiFunction);
    descriptor_kind = CallDescriptor::kCallWasmCapiFunction;
  }

  CallDescriptor::Flags flags = need_frame_state
                                    ? CallDescriptor::kNeedsFrameState
                                    : CallDescriptor::kNoFlags;
  return zone->New<CallDescriptor>(       // --
      descriptor_kind,                    // kind
      kWasmEntrypointTag,                 // tag
      target_type,                        // target MachineType
      target_loc,                         // target location
      location_sig,                       // location_sig
      parameter_slots,                    // parameter slot count
      compiler::Operator::kNoProperties,  // properties
      kCalleeSaveRegisters,               // callee-saved registers
      kCalleeSaveFPRegisters,             // callee-saved fp regs
      flags,                              // flags
      "wasm-call",                        // debug name
      StackArgumentOrder::kDefault,       // order of the arguments in the stack
      RegList{},                          // allocatable registers
      return_slots);                      // return slot count
}

std::ostream& operator<<(std::ostream& os, CheckForNull null_check) {
  return os << (null_check == kWithoutNullCheck ? "no null check"
                                                : "null check");
}

}  // namespace v8::internal::compiler
