// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

let sig1 = builder.addType(makeSig(
    [
      kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32,
      kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmAnyRef,
    ],
    []));
let sig2 = builder.addType(makeSig(
    [
      kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmS128,
      kWasmS128, kWasmS128, kWasmS128, kWasmS128, kWasmS128, kWasmS128
    ],
    []));
let sig3 = builder.addType(makeSig([kWasmS128, kWasmS128], []));

let main = builder.addFunction('main', kSig_v_i).exportFunc();
let func1 = builder.addFunction('func1', sig1);
let func2 = builder.addFunction('func2', sig2).addBody([]);
let func3 = builder.addFunction('func3', sig3).addBody([]);

let table = builder.addTable(kWasmFuncRef, 4, 4);

builder.addActiveElementSegment(
    table.index, wasmI32Const(0),
    [
      [kExprRefFunc, main.index], [kExprRefFunc, func1.index],
      [kExprRefFunc, func2.index], [kExprRefFunc, func3.index]
    ],
    kWasmFuncRef);

let main_body = [
  // Recursion breaker.
  kExprLocalGet, 0,
  kExprI32Eqz,
  kExprBrIf, 0
];
// Useless calls, just to create stack values.
for (let j = 0; j < 2; j++) {
  for (let i = 0; i < 13; i++) {
    main_body.push(...wasmF32Const(i));
  }
  main_body.push(kExprRefNull, kAnyRefCode);
  main_body.push(kExprCallFunction, func1.index);
}
main_body.push(...[
  // Recursive call.
  kExprLocalGet, 0,
  kExprI32Const, 1,
  kExprI32Sub,
  kExprCallFunction, main.index,
]);
main.addBody(main_body);

let func1_body = [];
for (let i = 0; i < 2; i++) {
  func1_body.push(kExprI32Const, 0, kSimdPrefix, kExprI8x16Splat);
}
func1_body.push(kExprI32Const, 3);
func1_body.push(kExprCallIndirect, sig3, table.index);

for (let i = 0; i < 6; i++) {
  func1_body.push(...wasmF32Const(i));
}
for (let i = 0; i < 7; i++) {
  func1_body.push(kExprI32Const, 0, kSimdPrefix, kExprI8x16Splat);
}
func1_body.push(kExprI32Const, 2);
func1_body.push(kExprReturnCallIndirect, sig2, table.index);
func1.addBody(func1_body);

let instance = builder.instantiate();
instance.exports.main(3);
