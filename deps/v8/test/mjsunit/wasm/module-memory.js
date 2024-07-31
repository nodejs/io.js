// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --expose-gc --stress-compaction --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var kMemSize = 65536;

function genModule(memory) {
  var builder = new WasmModuleBuilder();

  builder.addImportedMemory("", "memory", 1);
  builder.exportMemoryAs("memory");
  builder.addFunction("main", kSig_i_i)
    .addBody([
      // main body: while(i) { if(mem[i]) return -1; i -= 4; } return 0;
      // TODO(titzer): this manual bytecode has a copy of test-run-wasm.cc
      /**/ kExprLoop, kWasmVoid,           // --
      /*  */ kExprLocalGet, 0,             // --
      /*  */ kExprIf, kWasmVoid,           // --
      /*    */ kExprLocalGet, 0,           // --
      /*    */ kExprI32LoadMem, 0, 0,      // --
      /*    */ kExprIf, kWasmVoid,         // --
      /*      */ kExprI32Const, 127,       // --
      /*      */ kExprReturn,              // --
      /*      */ kExprEnd,                 // --
      /*    */ kExprLocalGet, 0,           // --
      /*    */ kExprI32Const, 4,           // --
      /*    */ kExprI32Sub,                // --
      /*    */ kExprLocalSet, 0,           // --
      /*    */ kExprBr, 1,                 // --
      /*    */ kExprEnd,                   // --
      /*  */ kExprEnd,                     // --
      /**/ kExprI32Const, 0                // --
    ])
    .exportFunc();
  var module = builder.instantiate({"": {memory:memory}});
  assertTrue(module.exports.memory instanceof WebAssembly.Memory);
  if (memory != null) assertEquals(memory.buffer, module.exports.memory.buffer);
  return module;
}

function testPokeMemory() {
  print("testPokeMemory");
  var module = genModule(new WebAssembly.Memory({initial: 1}));
  var buffer = module.exports.memory.buffer;
  var main = module.exports.main;
  assertEquals(kMemSize, buffer.byteLength);

  var array = new Int8Array(buffer);
  assertEquals(kMemSize, array.length);

  assertTrue(array.every((e => e === 0)));

  for (var i = 0; i < 10; i++) {
    assertEquals(0, main(kMemSize - 4));

    array[kMemSize/2 + i] = 1;
    assertEquals(0, main(kMemSize/2 - 4));
    assertEquals(-1, main(kMemSize - 4));

    array[kMemSize/2 + i] = 0;
    assertEquals(0, main(kMemSize - 4));
  }
}

testPokeMemory();

function genAndGetMain(buffer) {
  return genModule(buffer).exports.main;  // to prevent intermediates living
}

function testSurvivalAcrossGc() {
  var checker = genAndGetMain(new WebAssembly.Memory({initial: 1}));
  for (var i = 0; i < 3; i++) {
    print("gc run ", i);
    assertEquals(0, checker(kMemSize - 4));
    gc();
  }
}

testSurvivalAcrossGc();
testSurvivalAcrossGc();
testSurvivalAcrossGc();
testSurvivalAcrossGc();


function testPokeOuterMemory() {
  print("testPokeOuterMemory");
  var buffer = new WebAssembly.Memory({initial: kMemSize / kPageSize});
  var module = genModule(buffer);
  var main = module.exports.main;
  assertEquals(kMemSize, buffer.buffer.byteLength);

  var array = new Int8Array(buffer.buffer);
  assertEquals(kMemSize, array.length);

  assertTrue(array.every((e => e === 0)));

  for (var i = 0; i < 10; i++) {
    assertEquals(0, main(kMemSize - 4));

    array[kMemSize/2 + i] = 1;
    assertEquals(0, main(kMemSize/2 - 4));
    assertEquals(-1, main(kMemSize - 4));

    array[kMemSize/2 + i] = 0;
    assertEquals(0, main(kMemSize - 4));
  }
}

testPokeOuterMemory();

function testOuterMemorySurvivalAcrossGc() {
  var buffer = new WebAssembly.Memory({initial: kMemSize / kPageSize});
  var checker = genAndGetMain(buffer);
  for (var i = 0; i < 3; i++) {
    print("gc run ", i);
    assertEquals(0, checker(kMemSize - 4));
    gc();
  }
}

testOuterMemorySurvivalAcrossGc();
testOuterMemorySurvivalAcrossGc();
testOuterMemorySurvivalAcrossGc();
testOuterMemorySurvivalAcrossGc();


function testOOBThrows() {
  var builder = new WasmModuleBuilder();

  builder.addMemory(1, 1);
  builder.addFunction("geti", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32LoadMem, 0, 0,
      kExprI32StoreMem, 0, 0,
      kExprLocalGet, 1,
      kExprI32LoadMem, 0, 0,
    ])
    .exportFunc();

  var module = builder.instantiate();

  let read = offset => module.exports.geti(0, offset);
  let write = offset =>  module.exports.geti(offset, 0);

  assertEquals(0, read(65532));
  assertEquals(0, write(65532));

  // Note that this test might be run concurrently in multiple Isolates, which
  // makes an exact comparison of the expected trap count unreliable. But is is
  // still possible to check the lower bound for the expected trap count.
  for (let offset = kMemSize - 3; offset <= kMemSize; offset++) {
    const trap_count = %GetWasmRecoveredTrapCount();
    assertTraps(kTrapMemOutOfBounds, () => read(offset));
    assertTraps(kTrapMemOutOfBounds, () => write(offset));
    if (%IsWasmTrapHandlerEnabled()) {
      if (%IsWasmPartialOOBWriteNoop()) {
        assertTrue(trap_count + 2 <= %GetWasmRecoveredTrapCount());
      } else {
        assertTrue(trap_count + 1 <= %GetWasmRecoveredTrapCount());
      }
    }
  }
}

testOOBThrows();
