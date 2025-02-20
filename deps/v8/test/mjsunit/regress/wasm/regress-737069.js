// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let binary = new Binary;

binary.emit_header();
binary.emit_section(kTypeSectionCode, section => {
  section.emit_u32v(1); // number of types
  section.emit_u8(kWasmFunctionTypeForm);
  section.emit_u32v(0); // number of parameters
  section.emit_u32v(0); // number of returns
});
binary.emit_section(kFunctionSectionCode, section => {
  section.emit_u32v(1); // number of functions
  section.emit_u32v(0); // type index
});

binary.emit_u8(kCodeSectionCode);
binary.emit_u8(0x02); // section length
binary.emit_u8(0x01); // number of functions
binary.emit_u8(0x40); // function body size
// Function body is missing here.

let buffer = new ArrayBuffer(binary.length);
let view = new Uint8Array(buffer);
for (let i = 0; i < binary.length; i++) {
  view[i] = binary[i] | 0;
}
WebAssembly.validate(buffer);
