// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory-control

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const descriptor = d8.file.create_wasm_memory_map_descriptor(
    "test/mjsunit/wasm/wasm-module-builder.js");

const file_content = d8.file.read('test/mjsunit/wasm/wasm-module-builder.js');

const memory = new WebAssembly.Memory({initial: 10});
console.log(memory.map(descriptor, 0x10000));

const view = new Uint8Array(memory.buffer);

for (let i = 0; i < file_content.length; ++i) {
  assertEquals(view[i + 0x10000], file_content.charCodeAt(i));
}

const empty_descriptor =
    new WebAssembly.MemoryMapDescriptor(file_content.length);
memory.map(empty_descriptor, 0x10000);

for (let i = 0; i < file_content.length; ++i) {
  assertEquals(view[i + 0x10000], 0);
}
