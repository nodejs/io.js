// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Test inlining of a non-trivial type check (i.e. the decoder can't remove it
// directly) that becomes trivial after inlining.
// This covers a bug in the optimizing compiler treating null as a test failure
// for the "ref.test null" instruction.
(function TestRefTestNonTrivialTypeCheckInlinedTrivial() {
  var builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);


  let refTestFromAny =  builder.addFunction(`refTestFromAny`,
                        makeSig([kWasmAnyRef], [kWasmI32, kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTest, struct,
      kExprLocalGet, 0,
      kGCPrefix, kExprRefTestNull, struct,
    ]);

  builder.addFunction(`main`,
      makeSig([], [kWasmI32, kWasmI32, kWasmI32, kWasmI32]))
    .addBody([
      kExprI32Const, 1,
      kGCPrefix, kExprStructNew, struct,
      kExprCallFunction, refTestFromAny.index,
      kExprRefNull, kNullRefCode,
      kExprCallFunction, refTestFromAny.index,
    ]).exportFunc();

  var instance = builder.instantiate();
  let expected = [
    1,  // ref.test <struct> (struct)
    1,  // ref.test null <struct> (struct)
    0,  // ref.test <struct> (null)
    1   // ref.test null <struct> (null)
  ]
  assertEquals(expected, instance.exports.main());
})();
