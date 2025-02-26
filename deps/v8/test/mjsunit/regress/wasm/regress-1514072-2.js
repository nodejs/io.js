// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-loop-unrolling --no-wasm-loop-peeling

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let structA = builder.addStruct([makeField(kWasmI32, true)]);
let structB = builder.addStruct([makeField(kWasmI64, true)]);
builder.addFunction('main', makeSig([kWasmI32], [kWasmI32]))
  .addLocals(kWasmAnyRef, 1)
  .addLocals(kWasmI32, 1)
  .addBody([
    // Initialize local[1] with structA.
    kGCPrefix, kExprStructNewDefault, structA,
    kExprLocalSet, 1,
    kExprLoop, 0x7f,
      // Just a useless branch to move the ref.test out of the loop header.
      kExprI32Const, 1,
      kExprIf, kWasmVoid,
      kExprEnd,
      // ref.test if local[1] is of type structA.
      // This is guaranteed to succeed on the first iteration.
      // The else branch is marked as unreachable.
      kExprLocalGet, 1,
      kGCPrefix, kExprRefTest, structA,
      kExprIf, kWasmVoid,
        // This branch is taken on the first iteration.
      kExprElse,
        // This branch is taken on subsequent iterations.
      kExprEnd,

      // Push local[1] onto the value stack.
      kExprLocalGet, 1,

      // Update local[1] to a different type.
      // This will cause the analyzer having to revisit the loop a second time
      // as the LoopPhi for local[1] has now a different type.
      ...wasmI64Const(0x1234567812345678n),
      kGCPrefix, kExprStructNew, structB,
      kExprLocalSet, 1,

      // Cast the old local[1] value on the stack to structA.
      // This is guaranteed to succeed on the first iteration.
      // It is guaranteed to fail on subsequent iterations, however the
      // WasmGCTypeReducer ignores this as the else branch of the if following
      // the ref.test is still marked as unreachable when revisiting the loop
      // a second time.
      kGCPrefix, kExprRefCast, structA,
      // <-- The value here is structB in the second iteration on which we have
      //     a type confusion as the static type is structA.
      // Read the i32 field from the structA.
      // On the second iteration this is going to read one half of the i64 of
      // structB.
      kGCPrefix, kExprStructGet, structA, 0,
      kExprLocalTee, 2,
      kExprLocalGet, 2,
      // Compare the field with 0. If it is zero, repeat the loop, if it isn't
      // zero return the value.
      kExprI32Eqz,
      kExprBrIf, 0,
      kExprReturn,
    kExprEnd,
  ]).exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapIllegalCast, () => instance.exports.main(1));
