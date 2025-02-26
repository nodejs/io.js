// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Collect the Callsite objects instead of just a string:
Error.prepareStackTrace = function(error, frames) {
  return frames;
};

function testTrapLocations(instance, expected_stack_length) {
  function testWasmTrap(value, reason, position) {
    let function_name = arguments.callee.name;
    try {
      instance.exports.main(value);
      fail('expected wasm exception');
    } catch (e) {
      assertEquals(kTrapMsgs[reason], e.message, 'trap reason');
      // Check that the trapping function is the one which was called from this
      // function.
      assertTrue(
          e.stack[1].toString().startsWith(function_name), 'stack depth');
      assertEquals(1, e.stack[0].getLineNumber(), 'wasmFunctionIndex');
      assertEquals(position, e.stack[0].getPosition(), 'position');
    }
  }

  // The actual tests:
  testWasmTrap(0, kTrapDivByZero, 73);
  testWasmTrap(1, kTrapMemOutOfBounds, 74);
  testWasmTrap(2, kTrapUnreachable, 87);
  testWasmTrap(3, kTrapTableOutOfBounds, 91);
}

var builder = new WasmModuleBuilder();
builder.addMemory(0, 1);
var sig_index = builder.addType(kSig_i_v)

// Build a function to resemble this code:
//   if (idx < 2) {
//     return load(-2 / idx);
//   } else if (idx == 2) {
//     unreachable;
//   } else {
//     return call_indirect(idx);
//   }
// There are four different traps which are triggered by different input values:
// (0) division by zero; (1) mem oob; (2) unreachable; (3) invalid call target
// Each of them also has a different location where it traps.
builder.addFunction("main", kSig_i_i)
  .addBody([
      // offset 1
        kExprBlock, kWasmI32,
            kExprLocalGet, 0,
            kExprI32Const, 2,
          kExprI32LtU,
        kExprIf, kWasmVoid,
        // offset 9
              kExprI32Const, 0x7e /* -2 */,
              kExprLocalGet, 0,
            kExprI32DivU,
          // offset 15
          kExprI32LoadMem, 0, 0,
          kExprBr, 1,
        kExprEnd,
        // offset 21
            kExprLocalGet, 0,
            kExprI32Const, 2,
          kExprI32Eq,
        kExprIf, kWasmVoid,
          kExprUnreachable,
        kExprEnd,
        // offset 30
        kExprLocalGet, 0,
        kExprCallIndirect, sig_index, kTableZero,
      kExprEnd,
  ])
  .exportAs("main");
builder.appendToTable([0]);

let buffer = builder.toBuffer();

// Test async compilation and instantiation.
assertPromiseResult(WebAssembly.instantiate(buffer), pair => {
  testTrapLocations(pair.instance, 5);
});

// Test sync compilation and instantiation.
testTrapLocations(builder.instantiate(), 4);
