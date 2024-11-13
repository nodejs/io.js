// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function MemoryGrowBoundsCheck() {
  print("MemoryGrowBoundsCheck");
  const initial_size = 1; // 64KB
  const grow_size = 16384-1; // 1GB
  const final_size = initial_size + grow_size;

  let memory = new WebAssembly.Memory({ initial: initial_size });
  assertEquals(kPageSize, memory.buffer.byteLength);
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("foo", "mem");
  builder.addFunction("load", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
    .exportFunc();
  builder.addFunction("store", kSig_i_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
      kExprLocalGet, 1])
    .exportFunc();
  var offset;
  let instance = builder.instantiate({ foo: { mem: memory } });
  function load() { return instance.exports.load(offset); }
  function store(value) { return instance.exports.store(offset, value); }

  for (offset = 0; offset < kPageSize - 3; offset += 4) {
    store(offset);
  }
  for (offset = 0; offset < kPageSize - 3; offset += 4) {
    assertEquals(offset, load());
  }
  offset = kPageSize - 3;
  assertTraps(kTrapMemOutOfBounds, load);

  // Grow memory. The engine will try to grow the memory array, but it might
  // also allocate a new buffer.
  memory.grow(grow_size);
  offset = final_size * kPageSize - 7;
  store(offset);
  assertEquals(offset, load());
  offset = final_size * kPageSize - 3;
  assertTraps(kTrapMemOutOfBounds, load);
})();
