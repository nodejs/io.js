// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-compaction

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

var initialMemoryPages = 1;
var maximumMemoryPages = 5;

// Grow memory in directly called functions.
print('=== grow_memory in direct calls ===');

// This test verifies that the current_memory instruction returns the correct
// value after returning from a function (direct call) that grew memory.
(function TestMemoryGrowInFunction() {
  print('TestMemoryGrowInFunction ...');
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,                  // get number of new pages
        kExprCallFunction, kGrowFunction,  // call the grow function
        kExprDrop,                         // drop the result of grow
        kExprMemorySize, kMemoryZero       // get the memory size
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // The caller should be aware that the memory was grown by the callee.
  var deltaPages = 1;
  assertEquals(
      initialMemoryPages + deltaPages, instance.exports.main(deltaPages));
})();

// This test verifies that accessing a memory page that has been created inside
// a function (direct call) does not trap in the caller.
(function TestMemoryGrowAndAccessInFunction() {
  print('TestMemoryGrowAndAccessInFunction ...');
  let index = 2 * kPageSize - 4;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('load', kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction('main', kSig_v_iii)
      .addBody([
        kExprLocalGet, 0,                  // get number of new pages
        kExprCallFunction, kGrowFunction,  // call the grow function
        kExprDrop,                         // drop the result of grow
        kExprLocalGet, 1,                  // get index
        kExprLocalGet, 2,                  // get value
        kExprI32StoreMem, 0, 0             // store
      ])
      .exportFunc();
  var instance = builder.instantiate();
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.load(index));
  var deltaPages = 1;
  instance.exports.main(deltaPages, index, 1234);
  // The caller should be able to access memory that was grown by the callee.
  assertEquals(1234, instance.exports.load(index));
})();

// This test verifies that when a function (direct call) grows and store
// something in the grown memory, the caller always reads from the grown
// memory. This checks that the memory start address gets updated in the caller.
(function TestMemoryGrowAndStoreInFunction() {
  print('TestMemoryGrowAndStoreInFunction ...');
  let index = 0;
  let oldValue = 21;
  let newValue = 42;
  let deltaPages = 1;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_v_v)
          .addBody([
            kExprI32Const, deltaPages,     // always grow memory by deltaPages
            kExprMemoryGrow, kMemoryZero,  // grow memory
            kExprDrop,                     // drop the result of grow
            kExprI32Const, index,          // put index on stack
            kExprI32Const, newValue,       // put new value on stack
            kExprI32StoreMem, 0, 0         // store
          ])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprI32Const, index,              // put index on stack
        kExprI32Const, oldValue,           // put old value on stack
        kExprI32StoreMem, 0, 0,            // store
        kExprCallFunction, kGrowFunction,  // call grow_and_store
        kExprI32Const, index,              // put index on stack
        kExprI32LoadMem, 0, 0              // load from grown memory
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // The caller should always read from grown memory.
  assertEquals(newValue, instance.exports.main());
})();

// This test verifies that the effects of growing memory in an directly
// called function inside a loop affect the result of current_memory when
// the loop is over.
(function TestMemoryGrowInFunctionInsideLoop() {
  print('TestMemoryGrowInFunctionInsideLoop ...');
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_ii)
      .addBody([
        // clang-format off
        kExprLoop, kWasmVoid,                   // while
          kExprLocalGet, 0,                     // -
          kExprIf, kWasmVoid,                   // if <param0> != 0
            // Grow memory.
            kExprLocalGet, 1,                   // get number of new pages
            kExprCallFunction, kGrowFunction,   // call the grow function
            kExprDrop,                          // drop the result of grow
            // Decrease loop variable.
            kExprLocalGet, 0,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprLocalSet, 0,                   // decrease <param0>
            kExprBr, 1,                         // continue
          kExprEnd,                             // end if
        kExprEnd,                               // end loop
        // Return the memory size.
        kExprMemorySize, kMemoryZero            // put memory size on stack
        // clang-format on
      ])
      .exportFunc();
  // The caller should be aware that the memory was grown by the callee.
  let instance = builder.instantiate();
  let iterations = 4;
  let deltaPages = 1;
  assertEquals(
      initialMemoryPages + iterations * deltaPages,
      instance.exports.main(iterations, deltaPages));
})();

// This test verifies that the effects of writing to memory grown in an
// directly called function inside a loop are retained when the loop is over.
(function TestMemoryGrowAndStoreInFunctionInsideLoop() {
  print('TestMemoryGrowAndStoreInFunctionInsideLoop ...');
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  builder.addFunction('store', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
        kExprLocalGet, 1
      ])
      .exportFunc();
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  // parameters:  iterations, deltaPages, index
  builder.addFunction('main', kSig_i_iii)
      .addBody([
        // clang-format off
        kExprLoop, kWasmVoid,                   // while
          kExprLocalGet, 0,                     // -
          kExprIf, kWasmVoid,                   // if <param0> != 0
            // Grow memory.
            kExprLocalGet, 1,                   // get number of new pages
            kExprCallFunction, kGrowFunction,   // call the grow function
            kExprDrop,                          // drop the result of grow
            // Increase counter in memory.
            kExprLocalGet, 2,                   // put index (for store)
                kExprLocalGet, 2,               // put index (for load)
                kExprI32LoadMem, 0, 0,          // load from grown memory
              kExprI32Const, 1,                 // -
              kExprI32Add,                      // increase counter
            kExprI32StoreMem, 0, 0,             // store counter in memory
            // Decrease loop variable.
            kExprLocalGet, 0,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprLocalSet, 0,                   // decrease <param0>
            kExprBr, 1,                         // continue
          kExprEnd,                             // end if
        kExprEnd,                               // end loop
        // Return the value
        kExprLocalGet, 2,                       // -
        kExprI32LoadMem, 0, 0                   // load from grown memory
        // clang-format on
      ])
      .exportFunc();
  // The caller should always read the correct memory
  let instance = builder.instantiate();
  let iterations = 4;
  let deltaPages = 1;
  let index = 0;
  let initialValue = 1;
  let expectedValue = initialValue + iterations;
  instance.exports.store(index, initialValue);
  assertEquals(
      expectedValue, instance.exports.main(iterations, deltaPages, index));
})();

// Grow memory in indirectly called functions.
print('\n=== grow_memory in indirect calls ===');

// This test verifies that the current_memory instruction returns the correct
// value after returning from a function (indirect call) that grew memory.
(function TestMemoryGrowInIndirectCall() {
  print('TestMemoryGrowInIndirectCall ...');
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_ii)
      .addBody([
        kExprLocalGet, 1,                  // get number of new pages
        kExprLocalGet, 0,                  // get index of the function
        kExprCallIndirect, 0, kTableZero,  // call the function
        kExprDrop,                         // drop the result of grow
        kExprMemorySize, kMemoryZero       // get the memory size
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  var instance = builder.instantiate();
  // The caller should be aware that the memory was grown by the callee.
  var deltaPages = 1;
  assertEquals(
      initialMemoryPages + deltaPages,
      instance.exports.main(kGrowFunction, deltaPages));
})();

// This test verifies that accessing a memory page that has been created inside
// a function (indirect call) does not trap in the caller.
(function TestMemoryGrowAndAccessInIndirectCall() {
  print('TestMemoryGrowAndAccessInIndirectCall ...');
  let index = 2 * kPageSize - 4;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('load', kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  let sig = makeSig([kWasmI32, kWasmI32, kWasmI32, kWasmI32], []);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 1,                  // get number of new pages
        kExprLocalGet, 0,                  // get index of the function
        kExprCallIndirect, 0, kTableZero,  // call the function
        kExprDrop,                         // drop the result of grow
        kExprLocalGet, 2,                  // get index
        kExprLocalGet, 3,                  // get value
        kExprI32StoreMem, 0, 0             // store
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  var instance = builder.instantiate();
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.load(index));
  let deltaPages = 1;
  let value = 1234;
  instance.exports.main(kGrowFunction, deltaPages, index, value);
  // The caller should be able to access memory that was grown by the callee.
  assertEquals(value, instance.exports.load(index));
})();

// This test verifies that when a function (indirect call) grows and store
// something in the grown memory, the caller always reads from the grown
// memory. This checks that the memory start address gets updated in the caller.
(function TestMemoryGrowAndStoreInIndirectCall() {
  print('TestMemoryGrowAndStoreInIndirectCall ...');
  let index = 0;
  let oldValue = 21;
  let newValue = 42;
  let deltaPages = 1;
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_v_v)
          .addBody([
            kExprI32Const, deltaPages,     // always grow memory by deltaPages
            kExprMemoryGrow, kMemoryZero,  // grow memory
            kExprDrop,                     // drop the result of grow
            kExprI32Const, index,          // put index on stack
            kExprI32Const, newValue,       // put new value on stack
            kExprI32StoreMem, 0, 0         // store
          ])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprI32Const, index,              // put index on stack
        kExprI32Const, oldValue,           // put old value on stack
        kExprI32StoreMem, 0, 0,            // store
        kExprLocalGet, 0,                  // get index of the function
        kExprCallIndirect, 0, kTableZero,  // call the function
        kExprI32Const, index,              // put index on stack
        kExprI32LoadMem, 0, 0              // load from grown memory
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  var instance = builder.instantiate();
  // The caller should always read from grown memory.
  assertEquals(42, instance.exports.main(kGrowFunction));
})();

// This test verifies that the effects of growing memory in an indirectly
// called function inside a loop affect the result of current_memory when
// the loop is over.
(function TestMemoryGrowInIndirectCallInsideLoop() {
  print('TestMemoryGrowInIndirectCallInsideLoop ...');
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('main', kSig_i_iii)
      .addBody([
        // clang-format off
        kExprLoop, kWasmVoid,                   // while
          kExprLocalGet, 1,                     // -
          kExprIf, kWasmVoid,                   // if <param1> != 0
            // Grow memory.
            kExprLocalGet, 2,                   // get number of new pages
            kExprLocalGet, 0,                   // get index of the function
            kExprCallIndirect, 0, kTableZero,   // call the function
            kExprDrop,                          // drop the result of grow
            // Decrease loop variable.
            kExprLocalGet, 1,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprLocalSet, 1,                   // decrease <param1>
            kExprBr, 1,                         // continue
          kExprEnd,                             // end if
        kExprEnd,                               // end loop
        // Return the memory size.
        kExprMemorySize, kMemoryZero            // put memory size on stack
        // clang-format on
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  // The caller should be aware that the memory was grown by the callee.
  let instance = builder.instantiate();
  let deltaPages = 1;
  let iterations = 4;
  assertEquals(
      initialMemoryPages + iterations * deltaPages,
      instance.exports.main(kGrowFunction, iterations, deltaPages));
})();

// This test verifies that the effects of writing to memory grown in an
// indirectly called function inside a loop are retained when the loop is over.
(function TestMemoryGrowAndStoreInIndirectCallInsideLoop() {
  print('TestMemoryGrowAndStoreInIndirectCallInsideLoop ...');
  let builder = new WasmModuleBuilder();
  let deltaPages = 1;
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  let kGrowFunction =
      builder.addFunction('grow', kSig_i_i)
          .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
          .exportFunc()
          .index;
  builder.addFunction('store', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
        kExprLocalGet, 1
      ])
      .exportFunc();
  builder
      .addFunction(
          // parameters:  function_index, iterations, deltaPages, index
          'main', makeSig([kWasmI32, kWasmI32, kWasmI32, kWasmI32], [kWasmI32]))
      .addBody([
        // clang-format off
        kExprLoop, kWasmVoid,                   // while
          kExprLocalGet, 1,                     // -
          kExprIf, kWasmVoid,                   // if <param1> != 0
            // Grow memory.
            kExprLocalGet, 2,                   // get number of new pages
            kExprLocalGet, 0,                   // get index of the function
            kExprCallIndirect, 0, kTableZero,   // call the function
            kExprDrop,                          // drop the result of grow
            // Increase counter in memory.
            kExprLocalGet, 3,                   // put index (for store)
                kExprLocalGet, 3,               // put index (for load)
                kExprI32LoadMem, 0, 0,          // load from grown memory
              kExprI32Const, 1,                 // -
              kExprI32Add,                      // increase counter
            kExprI32StoreMem, 0, 0,             // store counter in memory
            // Decrease loop variable.
            kExprLocalGet, 1,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprLocalSet, 1,                   // decrease <param1>
            kExprBr, 1,                         // continue
          kExprEnd,                             // end if
        kExprEnd,                               // end loop
        // Return the value
        kExprLocalGet, 3,                       // -
        kExprI32LoadMem, 0, 0                   // load from grown memory
        // clang-format on
      ])
      .exportFunc();
  builder.appendToTable([kGrowFunction]);
  // The caller should be aware that the memory was grown by the callee.
  let instance = builder.instantiate();
  let iterations = 4;
  let index = 0;
  let initialValue = 1;
  let expectedValue = initialValue + iterations;
  instance.exports.store(index, initialValue);
  assertEquals(
      expectedValue,
      instance.exports.main(kGrowFunction, iterations, deltaPages, index));
})();
