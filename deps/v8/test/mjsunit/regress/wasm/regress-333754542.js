// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation --no-wasm-dynamic-tiering

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig0 = builder.addType(kSig_v_v);
let $w0 = builder.addFunction(undefined, $sig0).exportAs('w0');

// func $w0: [] -> []
$w0.addLocals(kWasmI32, 5)  // $var0 - $var4
  .addBody([
      kExprI32Const, 10,
      kExprLocalSet, 0,  // $var0
      kExprLoop, kWasmVoid,
        ...wasmI32Const(256),
        kExprLocalSet, 1,  // $var1
        kExprLocalGet, 0,  // $var0
        kExprLocalGet, 1,  // $var1
        kExprI32RemS,
        kExprLocalSet, 0,  // $var0
        kExprI32Const, 0,
        kExprLocalSet, 3,  // $var3
        kExprLocalGet, 0,  // $var0
        kExprBrIf, 1,
        kExprLocalGet, 0,  // $var0
        kExprLocalGet, 3,  // $var3
        kExprI32Ne,
        kExprBrIf, 0,
      kExprEnd,
  ]);

const instance = builder.instantiate();
// Note: Not running the function as it is an infinite loop.
