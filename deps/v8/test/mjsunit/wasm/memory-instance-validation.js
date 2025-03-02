// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// This test verifies that when instances are exported, Gc'ed, the other
// instances in the chain still maintain a consistent view of the memory.
(function ValidateSharedInstanceMemory() {
  print("ValidateSharedInstanceMemory");
  let memory = new WebAssembly.Memory({initial: 5, maximum: 100});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("mod", "imported_mem");
  builder.addFunction("mem_size", kSig_i_v)
    .addBody([kExprMemorySize, kMemoryZero])
    .exportFunc();
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  var instances = [];
  for (var i = 0; i < 5; i++) {
    instances.push(builder.instantiate({mod: {imported_mem: memory}}));
  }
  function grow_instance(index, pages) {
    return instances[index].exports.grow(pages);
  }

  function verify_mem_size(expected_pages) {
    print("  checking size = " + expected_pages + " pages");
    assertEquals(expected_pages*kPageSize, memory.buffer.byteLength);
    for (let i = 0; i < instances.length; i++) {
      if (instances[i] == null) continue;
      assertEquals(expected_pages, instances[i].exports.mem_size());
    }
  }

  // Verify initial memory size of all instances, grow and verify that all
  // instances are updated correctly.
  verify_mem_size(5);
  assertEquals(5, memory.grow(6));
  verify_mem_size(11);

  print("  instances[1] = null, GC");
  instances[1] = null;
  gc();

  // i[0] - i[2] - i[3] - i[4]
  verify_mem_size(11);
  assertEquals(11, grow_instance(2, 10));
  verify_mem_size(21);

  print("  instances[4] = null, GC");
  instances[4] = null;
  gc();
  verify_mem_size(21);

  assertEquals(21, memory.grow(2));
  verify_mem_size(23);

  print("  instances[0] = null, GC");
  instances[0] = null;
  gc();
  gc();
  verify_mem_size(23);

  assertEquals(23, grow_instance(3, 5));
  verify_mem_size(28);

  print("  new instance, GC");
  // Instantiate a new instance and verify that it can be grown correctly.
  instances.push(builder.instantiate({mod: {imported_mem: memory}}));
  gc();
  gc();

  verify_mem_size(28);
  assertEquals(28, grow_instance(5, 2));
  verify_mem_size(30);
  assertEquals(30, memory.grow(5));
  verify_mem_size(35);

  for (let i = 0; i < instances.length; i++) {
    print("  instances[" + i + "] = null, GC");
    instances[i] = null;
    gc();
    verify_mem_size(35);
  }

})();
