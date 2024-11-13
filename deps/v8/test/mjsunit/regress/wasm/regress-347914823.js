// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig0 = builder.addType(kSig_v_v);
let $sig1 = builder.addType(kSig_v_i, kNoSuperType, false);

let $func0 = builder.addFunction("func0", $sig1).addBody([]);
let $func1 = builder.addFunction("func1", $sig0).exportFunc();

let $table0 = builder.addTable(kWasmFuncRef, 1, 1);
builder.addActiveElementSegment($table0.index, wasmI32Const(0), [$func0.index]);

$func1
  .addLocals(kWasmI32, 11)
  .addBody([
    // Shuffle some values around to waste cache registers.
    kExprLocalGet, 0,
    kExprLocalSet, 1,
    kExprLocalGet, 2,
    kExprLocalSet, 3,
    kExprLocalGet, 4,
    kExprLocalSet, 5,
    kExprLocalGet, 6,
    kExprLocalSet, 7,
    kExprLocalGet, 8,
    kExprLocalSet, 9,
    // Perform an indirect call with full subtype check.
    kExprLocalGet, 1,  // parameter
    kExprLocalGet, 0,  // index in table
    kExprCallIndirect, ...wasmSignedLeb($sig1), $table0.index,
  ]);


var instance = builder.instantiate({});
instance.exports.func1();
