// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-compaction

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

var initialMemoryPages = 1;
var maximumMemoryPages = 5;

function generateBuilder() {
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages);
  builder.addFunction('load', kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction('store', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1,
        kExprI32StoreMem, 0, 0, kExprLocalGet, 1
      ])
      .exportFunc();
  return builder;
}

// This test verifies that the effects of growing memory in an if branch
// affect the result of current_memory when the branch is merged.
(function TestMemoryGrowInIfBranchNoElse() {
  print('TestMemoryGrowInIfBranchNoElse ...');
  let deltaPages = 4;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprI32Const, deltaPages,            // put deltaPages on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
        kExprEnd,
        kExprMemorySize, kMemoryZero            // get the memory size
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // Avoid the if branch (not growing memory).
  assertEquals(initialMemoryPages, instance.exports.main(0));
  // Enter the if branch (growing memory).
  assertEquals(initialMemoryPages + deltaPages, instance.exports.main(1));
})();

// This test verifies that the effects of growing memory in an if branch are
// retained when the branch is merged even when an else branch exists.
(function TestMemoryGrowInIfBranchWithElse() {
  print('TestMemoryGrowInIfBranchWithElse ...');
  let index = 0;
  let oldValue = 21;
  let newValue = 42;
  let deltaPages = 4;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprI32Const, deltaPages,            // put deltaPages on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
        kExprElse,
          kExprI32Const, index,                 // put index on stack
          kExprI32Const, newValue,              // put the value on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprEnd,
        kExprMemorySize, kMemoryZero            // get the memory size
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // Initialize the memory location with oldValue.
  instance.exports.store(index, oldValue);
  assertEquals(oldValue, instance.exports.load(index));
  // Verify that the else branch (not growing) is reachable.
  assertEquals(initialMemoryPages, instance.exports.main(0));
  assertEquals(newValue, instance.exports.load(index));
  // Enter the if branch (growing memory).
  assertEquals(initialMemoryPages + deltaPages, instance.exports.main(1));
})();

// This test verifies that the effects of growing memory in an else branch
// affect the result of current_memory when the branch is merged.
(function TestMemoryGrowInElseBranch() {
  print('TestMemoryGrowInElseBranch ...');
  let index = 0;
  let oldValue = 21;
  let newValue = 42;
  let deltaPages = 4;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprI32Const, index,                 // put index on stack
          kExprI32Const, newValue,              // put the value on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprElse,
          kExprI32Const, deltaPages,            // put deltaPages on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
        kExprEnd,
        kExprMemorySize, kMemoryZero            // get the memory size
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // Initialize the memory location with oldValue.
  instance.exports.store(index, oldValue);
  assertEquals(oldValue, instance.exports.load(index));
  // Verify that the if branch (not growing) is reachable.
  assertEquals(initialMemoryPages, instance.exports.main(1));
  assertEquals(newValue, instance.exports.load(index));
  // Enter the else branch (growing memory).
  assertEquals(initialMemoryPages + deltaPages, instance.exports.main(0));
})();

// This test verifies that the effects of growing memory in an if/else
// branch affect the result of current_memory when the branches are merged.
(function TestMemoryGrowInBothIfAndElse() {
  print('TestMemoryGrowInBothIfAndElse ...');
  let deltaPagesIf = 1;
  let deltaPagesElse = 2;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprI32Const, deltaPagesIf,          // put deltaPagesIf on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
        kExprElse,
          kExprI32Const, deltaPagesElse,        // put deltaPagesElse on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
        kExprEnd,
        kExprMemorySize, kMemoryZero            // get the memory size
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // Enter the if branch (growing memory by 1 page).
  assertEquals(initialMemoryPages + deltaPagesIf, instance.exports.main(1));
  // Create a new instance for the testing the else branch.
  var instance = builder.instantiate();
  // Enter the else branch (growing memory by 2 pages).
  assertEquals(initialMemoryPages + deltaPagesElse, instance.exports.main(0));
})();

// This test verifies that the effects of growing memory in an if branch are
// retained when the branch is merged.
(function TestMemoryGrowAndStoreInIfBranchNoElse() {
  print('TestMemoryGrowAndStoreInIfBranchNoElse ...');
  let index = 2 * kPageSize - 4;
  let value = 42;
  let deltaPages = 1;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprI32Const, deltaPages,            // put deltaPages on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
          kExprLocalGet, 1,                     // get index parameter
          kExprI32Const, value,                 // put the value on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprEnd,
        kExprLocalGet, 1,                       // get index parameter
        kExprI32LoadMem, 0, 0                   // load from grown memory
      ])
      .exportFunc();
  var instance = builder.instantiate();

  // Avoid the if branch (not growing memory). This should trap when executing
  // the kExprI32LoadMem instruction at the end of main.
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0, index));
  // Enter the if branch (growing memory).
  assertEquals(value, instance.exports.main(1, index));
})();

// This test verifies that the effects of growing memory in an if branch are
// retained when the branch is merged even when  an else branch exists.
(function TestMemoryGrowAndStoreInIfBranchWithElse() {
  print('TestMemoryGrowAndStoreInIfBranchWithElse ...');
  let index = 2 * kPageSize - 4;
  let value = 42;
  let deltaPages = 1;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprI32Const, deltaPages,            // put deltaPages on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
          kExprLocalGet, 1,                     // get index parameter
          kExprI32Const, value,                 // put the value on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprElse,
          kExprLocalGet, 1,                     // get index parameter
          kExprI32Const, value,                 // put the value on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprEnd,
        kExprLocalGet, 1,                       // get index parameter
        kExprI32LoadMem, 0, 0                   // load from grown memory
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // Avoid the if branch (not growing memory). This should trap when executing
  // the kExprI32StoreMem instruction in the if branch.
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0, index));
  // Enter the if branch (growing memory).
  assertEquals(value, instance.exports.main(1, index));
})();

// This test verifies that the effects of growing memory in an else branch are
// retained when the branch is merged.
(function TestMemoryGrowAndStoreInElseBranch() {
  print('TestMemoryGrowAndStoreInElseBranch ...');
  let index = 2 * kPageSize - 4;
  let value = 42;
  let deltaPages = 1;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprLocalGet, 1,                     // get index parameter
          kExprI32Const, value,                 // put the value on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprElse,
          kExprI32Const, deltaPages,            // put deltaPages on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
          kExprLocalGet, 1,                     // get index parameter
          kExprI32Const, value,                 // put the value on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprEnd,
        kExprLocalGet, 1,                       // get index parameter
        kExprI32LoadMem, 0, 0                   // load from grown memory
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // Avoid the else branch (not growing memory). This should trap when executing
  // the kExprI32StoreMem instruction in the else branch.
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(1, index));
  // Enter the else branch (growing memory).
  assertEquals(value, instance.exports.main(0, index));
})();

// This test verifies that the effects of growing memory in an if/else branch
// are retained when the branch is merged.
(function TestMemoryGrowAndStoreInBothIfAndElse() {
  print('TestMemoryGrowAndStoreInBothIfAndElse ...');
  let index = 0;
  let valueIf = 21;
  let valueElse = 42;
  let deltaPagesIf = 1;
  let deltaPagesElse = 2;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_ii)
      .addBody([
        kExprLocalGet, 0,                       // get condition parameter
        kExprIf, kWasmVoid,                     // if it's 1 then enter if
          kExprI32Const, deltaPagesIf,          // put deltaPagesIf on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
          kExprLocalGet, 1,                     // get index parameter
          kExprI32Const, valueIf,               // put valueIf on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprElse,
          kExprI32Const, deltaPagesElse,        // put deltaPagesElse on stack
          kExprMemoryGrow, kMemoryZero,         // grow memory
          kExprDrop,                            // drop the result of grow
          kExprLocalGet, 1,                     // get index parameter
          kExprI32Const, valueElse,             // put valueElse on stack
          kExprI32StoreMem, 0, 0,               // store
        kExprEnd,
        kExprLocalGet, 1,                       // get index parameter
        kExprI32LoadMem, 0, 0                   // load from grown memory
      ])
      .exportFunc();
  var instance = builder.instantiate();
  // Enter the if branch (growing memory by 1 page).
  assertEquals(valueIf, instance.exports.main(1, index));
  // Enter the else branch (growing memory by 2 pages).
  assertEquals(valueElse, instance.exports.main(0, index));
})();
