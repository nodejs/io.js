// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-max-mem-pages=16384

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const k1MiB = 1 * 1024 * 1024;
const k1GiB = 1 * 1024 * 1024 * 1024;
const k2GiB = 2 * k1GiB;
const k3GiB = 3 * k1GiB;
const k4GiB = 4 * k1GiB;
const kMaxMemory = k1GiB;

// Indexes (and offsets) used to systematically probe the memory.
const indexes = (() => {
  const a = k1GiB, b = k2GiB, c = k3GiB, d = k4GiB;
  return [
    0,   1,   2,   3,   4,   5,   7,   8,   9,            // near 0
  a-8, a-4, a+0, a+1, a+2, a+3, a+4, a+5, a+7, a+8, a+9,  // near 1GiB
  b-8, b-4, b+0, b+1, b+2, b+3, b+4, b+5, b+7, b+8, b+9,  // near 2GiB
  c-8, c-4, c+0, c+1, c+2, c+3, c+4, c+5, c+7, c+8, c+9,  // near 3GiB
  d-9, d-8, d-7, d-5, d-4, d-3, d-2, d-1                  // near 4GiB
];
})();

(function Test() {
  var memory;

  function BuildAccessors(type, load_opcode, store_opcode, offset) {
    builder = new WasmModuleBuilder();
    builder.addImportedMemory("i", "mem");
    builder.addFunction('load', makeSig([kWasmI32], [type]))
      .addBody([
        kExprLocalGet, 0,                            // --
        load_opcode, 0, ...wasmUnsignedLeb(offset),  // --
      ])                                             // --
      .exportFunc();
    builder.addFunction('store', makeSig([kWasmI32, type], []))
      .addBody([
        kExprLocalGet, 0,                             // --
        kExprLocalGet, 1,                             // --
        store_opcode, 0, ...wasmUnsignedLeb(offset),  // --
      ])                                              // --
      .exportFunc();
    let i = builder.instantiate({i: {mem: memory}});
    return {offset: offset, load: i.exports.load, store: i.exports.store};
  }

  function probe(a, size, offset, f) {
    print(`size=${size} offset=${offset}`);
    for (let i of indexes) {
      let oob = (i + size + offset) > kMaxMemory;
      if (oob) {
//        print(`  ${i} + ${offset} OOB`);
        assertThrows(() => a.store(i, f(i)));
        assertThrows(() => a.load(i));
      } else {
//        print(`  ${i} = ${f(i)}`);
        a.store(i, f(i));
        assertEquals(f(i), a.load(i));
      }
    }
  }

  try {
    const kPages = kMaxMemory / kPageSize;
    memory = new WebAssembly.Memory({initial: kPages, maximum: kPages});
  } catch (e) {
    print("OOM: sorry, best effort max memory size test.");
    return;
  }

  assertEquals(kMaxMemory, memory.buffer.byteLength);

  for (let offset of indexes) {
    let a = BuildAccessors(kWasmI32, kExprI32LoadMem, kExprI32StoreMem, offset);
    probe(a, 4, offset, i => (0xaabbccee ^ ((i >> 11) * 0x110005)) | 0);
  }

  for (let offset of indexes) {
    let a = BuildAccessors(kWasmI32, kExprI32LoadMem8U, kExprI32StoreMem8, offset);
    probe(a, 1, offset, i => (0xee ^ ((i >> 11) * 0x05)) & 0xFF);
  }

  for (let offset of indexes) {
    let a = BuildAccessors(kWasmF64, kExprF64LoadMem, kExprF64StoreMem, offset);
    probe(a, 8, offset, i => 0xaabbccee ^ ((i >> 11) * 0x110005));
  }
})();
