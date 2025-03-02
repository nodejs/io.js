// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// This test verifies that when instances are exported, Gc'ed, the other
// instances in the chain still maintain a consistent view of the memory.
(function InstanceMemoryGcStress() {
  print("InstanceMemoryGcStress");
  let memory = new WebAssembly.Memory({initial: 100, maximum: 1500});
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
    gc();
    instances.push(builder.instantiate({mod: {imported_mem: memory}}));
  }
  function grow_instance_0(pages) { return instances[0].exports.grow(pages); }
  function grow_instance_1(pages) { return instances[1].exports.grow(pages); }
  function grow_instance_2(pages) { return instances[2].exports.grow(pages); }
  function grow_instance_3(pages) { return instances[3].exports.grow(pages); }
  function grow_instance_4(pages) { return instances[4].exports.grow(pages); }

  var start_index = 0;
  var end_index = 5;
  function verify_mem_size(expected_pages) {
    assertEquals(expected_pages*kPageSize, memory.buffer.byteLength);
    for (var i = start_index; i < end_index; i++) {
      assertEquals(expected_pages, instances[i].exports.mem_size());
    }
  }

  // Verify initial memory size of all instances, grow and verify that all
  // instances are updated correctly.
  verify_mem_size(100);
  assertEquals(100, memory.grow(500));
  verify_mem_size(600);

  instances[1] = null;
  gc();
  gc();

  // i[0] - i[2] - i[3] - i[4]
  start_index = 2;
  verify_mem_size(600);
  assertEquals(600, instances[0].exports.mem_size());
  assertEquals(600, grow_instance_2(200));
  assertEquals(800*kPageSize, memory.buffer.byteLength);
  verify_mem_size(800);
  assertEquals(800, instances[0].exports.mem_size());

  // Instantiate a new instance and verify that it can be grown correctly.
  instances.push(builder.instantiate({mod: {imported_mem: memory}}));
  function grow_instance_5(pages) { return instances[5].exports.grow(pages); }
  gc();
  gc();

  // i[0] - i[2] - i[3] - i[4] - i[5]
  start_index = 2;
  end_index = 6;
  verify_mem_size(800);
  assertEquals(800, instances[0].exports.mem_size());
  assertEquals(800, grow_instance_2(100));
  assertEquals(900*kPageSize, memory.buffer.byteLength);
  verify_mem_size(900);
  assertEquals(900, instances[0].exports.mem_size());

  instances[4] = null;

  gc();
  gc();

  // i[0] - i[2] - i[3] - i[5]
  assertEquals(900, instances[0].exports.mem_size());
  assertEquals(900, instances[2].exports.mem_size());
  assertEquals(900, instances[3].exports.mem_size());
  assertEquals(900, instances[5].exports.mem_size());
  assertEquals(900, memory.grow(100));
  assertEquals(1000*kPageSize, memory.buffer.byteLength);
  assertEquals(1000, instances[0].exports.mem_size());
  assertEquals(1000, instances[2].exports.mem_size());
  assertEquals(1000, instances[3].exports.mem_size());
  assertEquals(1000, instances[5].exports.mem_size());

  gc();
  gc();

  instances[3] = null;

  // i[0] - i[2] - i[5]
  assertEquals(1000, instances[0].exports.mem_size());
  assertEquals(1000, instances[2].exports.mem_size());
  assertEquals(1000, instances[5].exports.mem_size());
  assertEquals(1000, memory.grow(100));
  assertEquals(1100*kPageSize, memory.buffer.byteLength);
  assertEquals(1100, instances[0].exports.mem_size());
  assertEquals(1100, instances[2].exports.mem_size());
  assertEquals(1100, instances[5].exports.mem_size());

  instances[0] = null;
  gc();
  gc();

  // i[2] - i[5]
  assertEquals(1100, instances[2].exports.mem_size());
  assertEquals(1100, instances[5].exports.mem_size());
  assertEquals(1100, grow_instance_5(1));
  gc();
  gc();

  assertEquals(1101*kPageSize, memory.buffer.byteLength);
  assertEquals(1101, instances[2].exports.mem_size());
  assertEquals(1101, instances[5].exports.mem_size());
})();
