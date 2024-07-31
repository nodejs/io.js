// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation --no-liftoff

// Tests the following scenario:
// - Wasm load elimination puts an immutable struct.get in its state.
// - In a dead control path, we manage to store to the same field after casting
//   it to a type where this field is mutable.
// - In this case, load elimination replaces the struct.set with an unreachable
//   value.
// - The control flow graph should be valid after this replacement.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();

let super_struct = builder.addStruct([]);
let sub_struct_1 =  builder.addStruct([makeField(kWasmI32, false)],
                                       super_struct);
let sub_struct_2 =  builder.addStruct([makeField(kWasmI32, true)],
                                       super_struct);

builder.addFunction("tester", makeSig([wasmRefNullType(sub_struct_1)], []))
  .addLocals(kWasmI32, 1)
  .addBody([
    kExprLocalGet, 0,
    kExprRefIsNull,
    kExprIf, kWasmVoid,

    kExprLocalGet, 0,
    kGCPrefix, kExprStructGet, sub_struct_1, 0,
    kExprLocalSet, 1,

    kExprLocalGet, 0, kGCPrefix, kExprRefCastNull, sub_struct_2,
    kExprLocalGet, 1,
    kGCPrefix, kExprStructSet, sub_struct_2, 0,

    kExprEnd])

  .exportFunc();

let instance = builder.instantiate();

assertTraps(kTrapNullDereference, () => instance.exports.tester(null));
