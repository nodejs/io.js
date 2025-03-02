// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-testing-opcode-in-wasm

// Test that tiering up and tiering down works even if functions cannot be
// compiled with Liftoff.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Create a simple Wasm module.
function create_builder(i) {
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0, kExprNopForTestingUnsupportedInLiftoff,
        ...wasmI32Const(i), kExprI32Add
      ])
      .exportFunc();
  return builder;
}

const instance = create_builder(0).instantiate();

// Test recompilation.
const Debug = new DebugWrapper();
Debug.enable();
assertFalse(%IsLiftoffFunction(instance.exports.main));
const newInstance = create_builder(1).instantiate();
assertFalse(%IsLiftoffFunction(newInstance.exports.main));

// Async.
async function testTierDownToLiftoffAsync() {
  Debug.disable();
  const asyncInstance = await create_builder(2).asyncInstantiate();

  // Test recompilation.
  Debug.enable();
  assertFalse(%IsLiftoffFunction(asyncInstance.exports.main));
  const newAsyncInstance = await create_builder(3).asyncInstantiate();
  assertFalse(%IsLiftoffFunction(newAsyncInstance.exports.main));
}

assertPromiseResult(testTierDownToLiftoffAsync());
