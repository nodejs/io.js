// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function bar() { return 123; }

const builder = new WasmModuleBuilder();
let imported = builder.addImport("foo", "bar", kSig_i_v);
builder.addExport("jsFunc", imported);
builder.addFunction("main", kSig_i_v).addBody(wasmI32Const(42)).exportFunc();
builder.addExport
let wasm = builder.instantiate({foo: {bar}}).exports;
%WasmTierUpFunction(wasm.main);
assertTrue(%IsTurboFanFunction(wasm.main));
// Calling WasmTierUpFunction on non-imported function doesn't crash when
// fuzzing.
%WasmTierUpFunction(wasm.jsFunc);
assertEquals(undefined, %IsTurboFanFunction(wasm.jsFunc));
assertEquals(undefined, %IsWasmDebugFunction(wasm.jsFunc));
assertEquals(undefined, %IsLiftoffFunction(wasm.jsFunc));
assertEquals(undefined, %IsUncompiledWasmFunction(wasm.jsFunc));
assertEquals(undefined, %WasmDeoptsExecutedForFunction(wasm.jsFunc));
