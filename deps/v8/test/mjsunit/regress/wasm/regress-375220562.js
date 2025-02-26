// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --fuzzing --wasm-max-module-size=1

// %WasmStruct() and %WasmArray() need to create a wasm module to instantiate
// the object. This doesn't work if the --wasm-max-module-size is set to
// arbitrarily small values when fuzzing.
assertEquals(undefined, %WasmArray());
assertEquals(undefined, %WasmStruct());
