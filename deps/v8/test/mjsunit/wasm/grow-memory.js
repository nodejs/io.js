// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-compaction
// This test does not behave predictably, since growing memory is allowed to
// fail nondeterministically.
// Flags: --no-verify-predictable

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");


function genMemoryGrowBuilder() {
  var builder = new WasmModuleBuilder();
  builder.addFunction("grow_memory", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
      .exportFunc();
  builder.addFunction("load", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction("store", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
                kExprLocalGet, 1])
      .exportFunc();
  builder.addFunction("load16", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem16U, 0, 0])
      .exportFunc();
  builder.addFunction("store16", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem16, 0, 0,
                kExprLocalGet, 1])
      .exportFunc();
  builder.addFunction("load8", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem8U, 0, 0])
      .exportFunc();
  builder.addFunction("store8", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem8, 0, 0,
                kExprLocalGet, 1])
      .exportFunc();
  return builder;
}

// V8 internal memory size limit.
var kV8MaxPages = 65536;


function testMemoryGrowReadWriteBase(size, load_fn, store_fn) {
  // size is the number of bytes for load and stores.
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, undefined);
  var module = builder.instantiate();
  var offset;
  var load = module.exports[load_fn];
  var store = module.exports[store_fn];
  function peek() { return load(offset); }
  function poke(value) { return store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }

  // Instead of checking every n-th offset, check the first 5.
  for(offset = 0; offset <= (4*size); offset+=size) {
    poke(20);
    assertEquals(20, peek());
  }
  for (offset = kPageSize - (size - 1); offset < kPageSize + size; offset++) {
    assertTraps(kTrapMemOutOfBounds, poke);
    assertTraps(kTrapMemOutOfBounds, peek);
  }

  assertEquals(1, growMem(3));

  for (let n = 1; n <= 3; n++) {
    for (offset = n * kPageSize - 5 * size; offset <= n * kPageSize + 4 * size;
         offset += size) {
      // Check the 5 offsets to the before and after the n-th page.
      //    page n-1              page n          page n+1
      //    +---- ... ------------+---------- ... +------ ...
      //    | | | ... | | | | | | | | | | | | ... | | | | ...
      //      <+>       ^                 ^
      //       |        first offset      last offset
      //       +-> size bytes
      poke(20);
      assertEquals(20, peek());
    }
  }

  // Check the last 5 valid offsets of the last page.
  for (offset = 4*kPageSize-size-(4*size); offset <= 4*kPageSize -size; offset+=size) {
    poke(20);
    assertEquals(20, peek());
  }

  for (offset = 4*kPageSize - (size-1); offset < 4*kPageSize + size; offset++) {
    assertTraps(kTrapMemOutOfBounds, poke);
    assertTraps(kTrapMemOutOfBounds, peek);
  }

  assertEquals(4, growMem(15));

  for (offset = 4*kPageSize - (size-1); offset <= 4*kPageSize + size; offset+=size) {
    poke(20);
    assertEquals(20, peek());
  }
  for (offset = 19*kPageSize - 10; offset <= 19*kPageSize - size; offset+=size) {
    poke(20);
    assertEquals(20, peek());
  }
  for (offset = 19*kPageSize - (size-1); offset < 19*kPageSize + 5; offset++) {
    assertTraps(kTrapMemOutOfBounds, poke);
    assertTraps(kTrapMemOutOfBounds, peek);
  }
}

(function testMemoryGrowReadWrite32() {
  print(arguments.callee.name);
  testMemoryGrowReadWriteBase(4, "load", "store");
})();

(function testMemoryGrowReadWrite16() {
  print(arguments.callee.name);
  testMemoryGrowReadWriteBase(2, "load16", "store16");
})();

(function testMemoryGrowReadWrite8() {
  print(arguments.callee.name);
  testMemoryGrowReadWriteBase(1, "load8", "store8");
})();

(function testMemoryGrowZeroInitialSize() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(0, undefined);
  var module = builder.instantiate();
  var offset;
  function peek() { return module.exports.load(offset); }
  function poke(value) { return module.exports.store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }

  assertTraps(kTrapMemOutOfBounds, peek);
  assertTraps(kTrapMemOutOfBounds, poke);

  assertEquals(0, growMem(1));

  // Check first 5 offsets.
  for(offset = 0; offset <= 5; offset++) {
    poke(20);
    assertEquals(20, peek());
  }

  // Check last 5 offsets.
  for(offset = kPageSize - 5*4; offset <= kPageSize - 4; offset++) {
    poke(20);
    assertEquals(20, peek());
  }

  for(offset = kPageSize - 3; offset <= kPageSize + 5; offset++) {
    assertTraps(kTrapMemOutOfBounds, peek);
  }

  offset = 3*kPageSize;
  for (var i = 1; i < 4; i++) {
    assertTraps(kTrapMemOutOfBounds, poke);
    assertEquals(i, growMem(1));
  }
  poke(20);
  assertEquals(20, peek());
})();

function testMemoryGrowZeroInitialSizeBase(size, load_fn, store_fn) {
  var builder = genMemoryGrowBuilder();
  builder.addMemory(0, undefined);
  var module = builder.instantiate();
  var offset;
  var load = module.exports[load_fn];
  var store = module.exports[store_fn];
  function peek() { return load(offset); }
  function poke(value) { return store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }

  assertTraps(kTrapMemOutOfBounds, peek);
  assertTraps(kTrapMemOutOfBounds, poke);

  assertEquals(0, growMem(1));

  // Instead of checking every offset, check the first 5.
  for(offset = 0; offset <= 4; offset++) {
    poke(20);
    assertEquals(20, peek());
  }

  // Check the last 5 valid ones.
  for(offset = kPageSize - (size * 4); offset <= kPageSize - size; offset++) {
    poke(20);
    assertEquals(20, peek());
  }

  for(offset = kPageSize - (size - 1); offset <= kPageSize + 5; offset++) {
    assertTraps(kTrapMemOutOfBounds, peek);
  }
}

(function testMemoryGrowZeroInitialSize32() {
  print(arguments.callee.name);
  testMemoryGrowZeroInitialSizeBase(4, "load", "store");
})();

(function testMemoryGrowZeroInitialSize16() {
  print(arguments.callee.name);
  testMemoryGrowZeroInitialSizeBase(2, "load16", "store16");
})();

(function testMemoryGrowZeroInitialSize8() {
  print(arguments.callee.name);
  testMemoryGrowZeroInitialSizeBase(1, "load8", "store8");
})();

(function testMemoryGrowTrapMaxPagesZeroInitialMemory() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(0, undefined);
  var module = builder.instantiate();
  function growMem(pages) { return module.exports.grow_memory(pages); }
  assertEquals(-1, growMem(kV8MaxPages + 1));
})();

(function testMemoryGrowTrapMaxPages() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, 1);
  var module = builder.instantiate();
  function growMem(pages) { return module.exports.grow_memory(pages); }
  assertEquals(-1, growMem(kV8MaxPages));
})();

(function testMemoryGrowTrapsWithNonSmiInput() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(0, undefined);
  var module = builder.instantiate();
  function growMem(pages) { return module.exports.grow_memory(pages); }
  // The parameter of grow_memory is unsigned. Therefore -1 stands for
  // UINT32_MIN, which cannot be represented as SMI.
  assertEquals(-1, growMem(-1));
})();

(function testMemoryGrowCurrentMemory() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, undefined);
  builder.addFunction("memory_size", kSig_i_v)
      .addBody([kExprMemorySize, kMemoryZero])
      .exportFunc();
  var module = builder.instantiate();
  function growMem(pages) { return module.exports.grow_memory(pages); }
  function MemSize() { return module.exports.memory_size(); }
  assertEquals(1, MemSize());
  assertEquals(1, growMem(1));
  assertEquals(2, MemSize());
})();

function testMemoryGrowPreservesDataMemOpBase(size, load_fn, store_fn) {
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, undefined);
  var module = builder.instantiate();
  var offset;
  var load = module.exports[load_fn];
  var store = module.exports[store_fn];
  function peek() { return load(offset); }
  function poke(value) { return store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }
  // Maximum unsigned integer of size bits.
  const max = Math.pow(2, (size * 8)) - 1;

  // Check the first 5 offsets.
  for(offset = 0; offset <= (4*size); offset+=size) {
    poke(offset % max);
    assertEquals(offset % max, peek());
  }

  // Check the last 5 valid offsets.
  for(offset = kPageSize - 5*size; offset <= (kPageSize - size); offset+=size) {
    poke(offset % max);
    assertEquals(offset % max, peek());
  }

  assertEquals(1, growMem(3));

  // Check the first 5 offsets are preserved by growMem.
  for(offset = 0; offset <= (4*size); offset+=size) {
    assertEquals(offset % max, peek());
  }

  // Check the last 5 valid offsets are preserved by growMem.
  for(offset = kPageSize - 5*size; offset <= (kPageSize - size); offset+=size) {
    assertEquals(offset % max, peek());
  }
}

(function testMemoryGrowPreservesDataMemOp32() {
  print(arguments.callee.name);
  testMemoryGrowPreservesDataMemOpBase(4, "load", "store");
})();

(function testMemoryGrowPreservesDataMemOp16() {
  print(arguments.callee.name);
  testMemoryGrowPreservesDataMemOpBase(2, "load16", "store16");
})();

(function testMemoryGrowPreservesDataMemOp8() {
  print(arguments.callee.name);
  testMemoryGrowPreservesDataMemOpBase(1, "load8", "store8");
})();

(function testMemoryGrowOutOfBoundsOffset() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, undefined);
  var module = builder.instantiate();
  var offset, val;
  function peek() { return module.exports.load(offset); }
  function poke(value) { return module.exports.store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }

  offset = 3*kPageSize + 4;
  assertTraps(kTrapMemOutOfBounds, poke);

  assertEquals(1, growMem(1));
  assertTraps(kTrapMemOutOfBounds, poke);

  assertEquals(2, growMem(1));
  assertTraps(kTrapMemOutOfBounds, poke);

  assertEquals(3, growMem(1));

  for (offset = 3*kPageSize; offset <= 3*kPageSize + 4; offset++) {
    poke(0xaced);
    assertEquals(0xaced, peek());
  }

  for (offset = 4*kPageSize-8; offset <= 4*kPageSize - 4; offset++) {
    poke(0xaced);
    assertEquals(0xaced, peek());
  }

  for (offset = 4*kPageSize - 3; offset <= 4*kPageSize + 4; offset++) {
    assertTraps(kTrapMemOutOfBounds, poke);
  }
})();

(function testMemoryGrowOutOfBoundsOffset2() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(16, 128);
  builder.addFunction("main", kSig_v_v)
      .addBody([
          kExprI32Const, 20,
          kExprI32Const, 29,
          kExprMemoryGrow, kMemoryZero,
          kExprI32StoreMem, 0, 0xFF, 0xFF, 0xFF, 0x3a
          ])
      .exportAs("main");
  var module = builder.instantiate();
  assertTraps(kTrapMemOutOfBounds, module.exports.main);
})();

(function testMemoryGrowDeclaredMaxTraps() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, 16);
  var module = builder.instantiate();
  function growMem(pages) { return module.exports.grow_memory(pages); }
  assertEquals(1, growMem(5));
  assertEquals(6, growMem(5));
  assertEquals(-1, growMem(6));
})();

(function testMemoryGrowInternalMaxTraps() {
  print(arguments.callee.name);
  // This test checks that grow_memory does not grow past the internally
  // defined maximum memory size.
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, kSpecMaxPages);
  var module = builder.instantiate();
  function growMem(pages) { return module.exports.grow_memory(pages); }
  assertEquals(1, growMem(20));
  assertEquals(-1, growMem(kV8MaxPages - 20));
})();

(function testMemoryGrow4Gb() {
  print(arguments.callee.name);
  var builder = genMemoryGrowBuilder();
  builder.addMemory(1, undefined);
  var module = builder.instantiate();
  var offset, val;
  function peek() { return module.exports.load(offset); }
  function poke(value) { return module.exports.store(offset, value); }
  function growMem(pages) { return module.exports.grow_memory(pages); }

  // Check first 5 offsets.
  for (offset = 0; offset <= 4 * 4; offset += 4) {
    poke(100000 - offset);
    assertEquals(100000 - offset, peek());
  }

  // Check last 5 offsets.
  for (offset = (kPageSize - 5 * 4); offset <= (kPageSize - 4); offset += 4) {
    poke(100000 - offset);
    assertEquals(100000 - offset, peek());
  }

  let result = growMem(kV8MaxPages - 1);
  if (result == 1) {
    // Check first 5 offsets.
    for (offset = 0; offset <= 4 * 4; offset += 4) {
      assertEquals(100000 - offset, peek());
    }

    // Check last 5 offsets.
    for (offset = (kPageSize - 5 * 4); offset <= (kPageSize - 4); offset += 4) {
      assertEquals(100000 - offset, peek());
    }

    // Bounds check for large mem size.
    let kMemSize = (kV8MaxPages * kPageSize);
    let kLastValidOffset = kMemSize - 4;  // Accommodate a 4-byte read/write.
    // Check first 5 offsets of last page.
    for (offset = kMemSize - kPageSize; offset <= kMemSize - kPageSize + 4 * 4;
         offset += 4) {
      poke(0xaced);
      assertEquals(0xaced, peek());
    }
    for (offset = kLastValidOffset - 5 * 4; offset <= kLastValidOffset;
         offset += 4) {
      poke(0xaced);
      assertEquals(0xaced, peek());
    }

    for (offset = kLastValidOffset + 1; offset < kMemSize; offset++) {
      assertTraps(kTrapMemOutOfBounds, poke);
    }
  } else {
    // Allocating big chunks of memory can fail on gc_stress, especially on 32
    // bit platforms. When grow_memory fails, expected result is -1.
    assertEquals(-1, result);
  }
})();

(function testGrowFromNearlyMaximum() {
  print(arguments.callee.name);
  // Regression test for https://crbug.com/1347668.
  const builder = genMemoryGrowBuilder();
  // The maximum needs to be >1GB, so we do not reserve everything upfront.
  const GB = 1024 * 1024 * 1024;
  const max_pages = 1 * GB / kPageSize + 10;

  builder.addMemory(0, max_pages);
  let module;
  const is_oom = e =>
      (e instanceof RangeError) && e.message.includes('Out of memory');
  // Allow instantiation to fail with OOM.
  try {
    module = builder.instantiate();
  } catch (e) {
    if (is_oom(e)) return;
    // Everything else is a bug.
    throw e;
  }
  const grow = module.exports.grow_memory;

  // First, grow close to the limit.
  // Growing can always fail if the system runs out of resources.
  let grow_result = grow(max_pages - 1);
  if (grow_result == -1) return;
  assertEquals(0, grow_result);

  // Then, grow by another page (this triggered the error in
  // https://crbug.com/1347668).
  grow_result = grow(1);
  if (grow_result == -1) return;
  assertEquals(max_pages - 1, grow_result);
  assertEquals(max_pages, grow(0));
  assertEquals(-1, grow(1));  // Fails.
})();
