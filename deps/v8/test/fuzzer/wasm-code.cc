// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "src/execution/isolate.h"
#include "src/wasm/wasm-module-builder.h"
#include "test/common/wasm/test-signatures.h"
#include "test/fuzzer/wasm-fuzzer-common.h"

namespace v8::internal::wasm::fuzzing {

class WasmCodeFuzzer : public WasmExecutionFuzzer {
  bool GenerateModule(Isolate* isolate, Zone* zone,
                      base::Vector<const uint8_t> data,
                      ZoneBuffer* buffer) override {
    TestSignatures sigs;
    WasmModuleBuilder builder(zone);
    WasmFunctionBuilder* f = builder.AddFunction(sigs.i_iii());
    f->EmitCode(data.begin(), static_cast<uint32_t>(data.size()));
    uint8_t end_opcode = kExprEnd;
    f->EmitCode(&end_opcode, 1);
    builder.AddExport(base::CStrVector("main"), f);

    builder.AddMemory(0, 32);
    builder.WriteTo(buffer);
    return true;
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  WasmCodeFuzzer().FuzzWasmModule({data, size});
  return 0;
}

}  // namespace v8::internal::wasm::fuzzing
