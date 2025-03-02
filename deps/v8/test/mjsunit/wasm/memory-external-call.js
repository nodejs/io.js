// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let initialMemoryPages = 1;
let maximumMemoryPages = 5;
let other_fn_idx = 0;

// This builder can be used to generate a module with memory + load/store
// functions and/or an additional imported function.
function generateBuilder(add_memory, import_sig) {
  let builder = new WasmModuleBuilder();
  if (import_sig) {
    // Add the import if we expect a module builder with imported functions.
    let idx = builder.addImport('import_module', 'other_module_fn', import_sig);
    // The imported function should always have index 0. With this assertion we
    // verify that we can use other_fn_idx to refer to this function.
    assertEquals(idx, other_fn_idx)
  }
  if (add_memory) {
    // Add the memory if we expect a module builder with memory and load/store.
    builder.addMemory(initialMemoryPages, maximumMemoryPages);
    builder.addFunction('load', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
        .exportFunc();
    builder.addFunction('store', kSig_i_ii)
        .addBody([
          kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
          kExprLocalGet, 1
        ])
        .exportFunc();
  }
  return builder;
}

function assertMemoryIndependence(load_a, store_a, load_b, store_b) {

  assertEquals(0, load_a(0));
  assertEquals(0, load_b(0));
  assertEquals(0, load_a(4));
  assertEquals(0, load_b(4));

  store_a(0, 101);
  assertEquals(101, load_a(0));
  assertEquals(0,   load_b(0));
  assertEquals(0, load_a(4));
  assertEquals(0, load_b(4));

  store_a(4, 102);
  assertEquals(101, load_a(0));
  assertEquals(0,   load_b(0));
  assertEquals(102, load_a(4));
  assertEquals(0,   load_b(4));

  store_b(0, 103);
  assertEquals(101, load_a(0));
  assertEquals(103, load_b(0));
  assertEquals(102, load_a(4));
  assertEquals(0,   load_b(4));

  store_b(4, 107);
  assertEquals(101, load_a(0));
  assertEquals(103, load_b(0));
  assertEquals(102, load_a(4));
  assertEquals(107, load_b(4));

  store_a(0, 0);
  store_a(4, 0);
  store_b(0, 0);
  store_b(4, 0);
}

// A simple test for memory-independence between modules.
(function SimpleMemoryIndependenceTest() {
  print("SimpleMemoryIndependenceTest");
  let kPages = 1;
  let builder = new WasmModuleBuilder();

  builder.addMemory(kPages, kPages);
  builder.addFunction("store", kSig_v_ii)
    .addBody([
      kExprLocalGet, 0,     // --
      kExprLocalGet, 1,     // --
      kExprI32StoreMem, 0, 0, // --
    ])                      // --
    .exportFunc();
  builder.addFunction("load", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,     // --
      kExprI32LoadMem, 0, 0, // --
    ])                      // --
    .exportFunc();

  var a = builder.instantiate();

  // The {b} instance forwards all {store} calls to the imported function.
  builder = new WasmModuleBuilder();
  builder.addImport("mod", "store", kSig_v_ii);
  builder.addMemory(kPages, kPages);
  builder.addFunction("store", kSig_v_ii)
    .addBody([
      kExprLocalGet, 0,     // --
      kExprLocalGet, 1,     // --
      kExprCallFunction, 0, // --
    ])                      // --
    .exportFunc();
  builder.addFunction("load", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,     // --
      kExprI32LoadMem, 0, 0, // --
    ])                      // --
    .exportFunc();

  var b = builder.instantiate({mod: {store: a.exports.store}});

  assertEquals(0, a.exports.load(0));
  assertEquals(0, b.exports.load(0));
  assertEquals(0, a.exports.load(4));
  assertEquals(0, b.exports.load(4));

  a.exports.store(0, 101);
  assertEquals(101, a.exports.load(0));
  assertEquals(0,   b.exports.load(0));
  assertEquals(0, a.exports.load(4));
  assertEquals(0, b.exports.load(4));

  a.exports.store(4, 102);
  assertEquals(101, a.exports.load(0));
  assertEquals(0,   b.exports.load(0));
  assertEquals(102, a.exports.load(4));
  assertEquals(0,   b.exports.load(4));

  b.exports.store(4, 107);  // should forward to {a}.
  assertEquals(101, a.exports.load(0));
  assertEquals(0,   b.exports.load(0));
  assertEquals(107, a.exports.load(4));
  assertEquals(0,   b.exports.load(4));

})();

// This test verifies that when a Wasm module without memory invokes a function
// imported from another module that has memory, the second module reads its own
// memory and returns the expected value.
(function TestExternalCallBetweenTwoWasmModulesWithoutAndWithMemory() {
  print('TestExternalCallBetweenTwoWasmModulesWithoutAndWithMemory');

  let first_module = generateBuilder(add_memory = false, import_sig = kSig_i_i);
  // Function to invoke the imported function and add 1 to the result.
  first_module.addFunction('plus_one', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,                   // -
        kExprCallFunction, other_fn_idx,    // call the imported function
        kExprI32Const, 1,                   // -
        kExprI32Add,                        // add 1 to the result
        kExprReturn                         // -
      ])
      .exportFunc();
  let second_module =
      generateBuilder(add_memory = true, import_sig = undefined);

  let index = kPageSize - 4;
  let second_value = 2222;
  // Instantiate the instances.
  let second_instance = second_module.instantiate();
  let first_instance = first_module.instantiate(
      {import_module: {other_module_fn: second_instance.exports.load}});
  // Write the values in the second instance.
  second_instance.exports.store(index, second_value);
  assertEquals(second_value, second_instance.exports.load(index));
  // Verify that the value is correct when passing from the imported function.
  assertEquals(second_value + 1, first_instance.exports.plus_one(index));
})();

// This test verifies that when a Wasm module with memory invokes a function
// imported from another module that also has memory, the second module reads
// its own memory and returns the expected value.
(function TestExternalCallBetweenTwoWasmModulesWithMemory() {
  print('TestExternalCallBetweenTwoWasmModulesWithMemory');

  let first_module = generateBuilder(add_memory = true, import_sig = kSig_i_i);
  // Function to invoke the imported function and add 1 to the result.
  first_module.addFunction('plus_one', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,                   // -
        kExprCallFunction, other_fn_idx,    // call the imported function
        kExprI32Const, 1,                   // -
        kExprI32Add,                        // add 1 to the result
        kExprReturn                         // -
      ])
      .exportFunc();
  let second_module =
      generateBuilder(add_memory = true, import_sig = undefined);

  let index = kPageSize - 4;
  let first_value = 1111;
  let second_value = 2222;
  // Instantiate the instances.
  let second_instance = second_module.instantiate();
  let first_instance = first_module.instantiate(
      {import_module: {other_module_fn: second_instance.exports.load}});
  // Write the values in the two instances.
  first_instance.exports.store(index, first_value);
  second_instance.exports.store(index, second_value);
  // Verify that the values were stored to memory.
  assertEquals(first_value, first_instance.exports.load(index));
  assertEquals(second_value, second_instance.exports.load(index));
  // Verify that the value is correct when passing from the imported function.
  assertEquals(second_value + 1, first_instance.exports.plus_one(index));
})();

// This test verifies that the correct memory is accessed after returning
// from a function imported from another module that also has memory.
(function TestCorrectMemoryAccessedAfterReturningFromExternalCall() {
  print('TestCorrectMemoryAccessedAfterReturningFromExternalCall');

  let first_module = generateBuilder(add_memory = true, import_sig = kSig_i_ii);
  // Function to invoke the imported function and add 1 to the result.
  first_module.addFunction('sandwich', kSig_i_iii)
      .addBody([
        kExprLocalGet, 0,                   // param0 (index)
        kExprLocalGet, 1,                   // param1 (first_value)
        kExprI32StoreMem, 0, 0,             // store value in first_instance
        kExprLocalGet, 0,                   // param0 (index)
        kExprLocalGet, 2,                   // param2 (second_value)
        kExprCallFunction, other_fn_idx,    // call the imported function
        kExprDrop,                          // drop the return value
        kExprLocalGet, 0,                   // param0 (index)
        kExprI32LoadMem, 0, 0,              // load from first_instance
        kExprReturn                         // -
      ])
      .exportFunc();
  let second_module =
      generateBuilder(add_memory = true, import_sig = undefined);

  let index = kPageSize - 4;
  let first_value = 1111;
  let second_value = 2222;
  // Instantiate the instances.
  let second_instance = second_module.instantiate();
  let first_instance = first_module.instantiate(
      {import_module: {other_module_fn: second_instance.exports.store}});
  // Call the sandwich function and check that it returns the correct value.
  assertEquals(
      first_value,
      first_instance.exports.sandwich(index, first_value, second_value));
  // Verify that the values are correct in both memories.
  assertEquals(first_value, first_instance.exports.load(index));
  assertEquals(second_value, second_instance.exports.load(index));
})();

// A test for memory-independence between modules when calling through
// imported tables.
(function CallThroughTableMemoryIndependenceTest() {
  print("CallThroughTableIndependenceTest");
  let kTableSize = 2;
  let kPages = 1;
  let builder = new WasmModuleBuilder();

  builder.addMemory(kPages, kPages);
  builder.addFunction("store", kSig_v_ii)
    .addBody([
      kExprLocalGet, 0,     // --
      kExprLocalGet, 1,     // --
      kExprI32StoreMem, 0, 0, // --
    ])                      // --
    .exportFunc();
  builder.addFunction("load", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,     // --
      kExprI32LoadMem, 0, 0, // --
    ])                      // --
    .exportFunc();

  {
    // Create two instances.
    let module = builder.toModule();
    var a = new WebAssembly.Instance(module);
    var b = new WebAssembly.Instance(module);
    // Check that the memories are initially independent.
    assertMemoryIndependence(a.exports.load, a.exports.store,
                             b.exports.load, b.exports.store);
  }

  let table = new WebAssembly.Table({element: "anyfunc",
                                     initial: kTableSize,
                                     maximum: kTableSize});

  table.set(0, a.exports.store);
  table.set(1, b.exports.store);
  // Check that calling (from JS) through the table maintains independence.
  assertMemoryIndependence(a.exports.load, table.get(0),
                           b.exports.load, table.get(1));

  table.set(1, a.exports.store);
  table.set(0, b.exports.store);
  // Check that calling (from JS) through the table maintains independence,
  // even after reorganizing the table.
  assertMemoryIndependence(a.exports.load, table.get(1),
                           b.exports.load, table.get(0));

  // Check that calling (from Wasm) through the table maintains independence.
  builder = new WasmModuleBuilder();
  builder.addImportedTable("m", "table", kTableSize, kTableSize);
  var sig_index = builder.addType(kSig_v_ii);
  builder.addFunction("store", kSig_v_iii)
    .addBody([
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprLocalGet, 0,
      kExprCallIndirect, sig_index, kTableZero,
    ]).exportFunc();

  let c = builder.instantiate({m: {table: table}});

  let a_index = 1;
  let b_index = 0;
  let store_a = (index, val) => c.exports.store(a_index, index, val)
  let store_b = (index, val) => c.exports.store(b_index, index, val);

  assertMemoryIndependence(a.exports.load, store_a,
                           b.exports.load, store_b);

  // Flip the order in the table and do it again.
  table.set(0, a.exports.store);
  table.set(1, b.exports.store);

  a_index = 0;
  b_index = 1;

  assertMemoryIndependence(a.exports.load, store_a,
                           b.exports.load, store_b);

})();
