// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-memory64-trap-handling --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function testMemory64TrapHandling() {
    const builder = new WasmModuleBuilder();
    let memory = builder.addMemory64(1, 1);

    builder.addFunction('load64', kSig_i_l).addBody([
      kExprLocalGet, 0,
      kExprI32LoadMem, 0x40, memory, 0
    ]).exportFunc();

    builder.addFunction('store64', kSig_v_li).addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32StoreMem, 0x40, memory, 0
    ]).exportFunc();

    const instance = builder.instantiate();
    const {load64, store64} = instance.exports;
    let memory_size = 65536n;
    const partial_oob = memory_size - 2n;
    const oob = 1n << 18n;
    const huge_address = 1n << 50n;
    let value1 = 0x42424242;
    let value2 = 0xDEADBEEF;

    store64(0n, value1);
    store64(memory_size - 4n, value1);
    assertTraps(kTrapMemOutOfBounds, () => store64(partial_oob, value2));
    assertTraps(kTrapMemOutOfBounds, () => store64(oob, value2));
    assertTraps(kTrapMemOutOfBounds, () => store64(huge_address, value2));

    assertEquals(value1, load64(0n));
    // The partial oob write did not change the memory.
    assertEquals(value1, load64(memory_size - 4n));
    assertTraps(kTrapMemOutOfBounds, () => load64(partial_oob));
    assertTraps(kTrapMemOutOfBounds, () => load64(oob));
    assertTraps(kTrapMemOutOfBounds, () => load64(huge_address));
})();
