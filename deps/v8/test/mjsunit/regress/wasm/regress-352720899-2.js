// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-wasm-lazy-compilation
d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig1 = builder.addType(kSig_v_v);
let $sig115 = builder.addType(makeSig([kWasmI32], []));
let $sig201 = builder.addType(makeSig([kWasmAnyRef], [kWasmAnyRef]));

let $func74 = builder.addFunction(undefined, $sig201).exportAs("func74");
let $func224 = builder.addFunction(undefined, $sig115).exportAs("func224");

// func $func74: [kWasmAnyRef] -> [kWasmAnyRef]
$func74.addBody([
  kExprLocalGet, 0,
  kGCPrefix, kExprBrOnCastFail, 0b11, 0, kAnyRefCode, kStructRefCode,
  kGCPrefix, kExprRefCastNull, kNullRefCode,
]);

$func224.addBody([
    kExprLocalGet, 0,
    kExprIf, kWasmVoid,
      kExprLoop, kWasmRef, kFuncRefCode,
        kExprRefNull, kNullFuncRefCode,
        kExprRefAsNonNull,
      kExprEnd,
      kExprDrop,
      ...wasmI32Const(42),
      kGCPrefix, kExprRefI31,
      kExprCallFunction, $func74.index,
      kExprUnreachable,
    kExprEnd,
]);

let instance = builder.instantiate({});
