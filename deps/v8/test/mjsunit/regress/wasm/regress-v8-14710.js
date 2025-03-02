// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function Trap_StringFromCodePoint() {
  const builder = new WasmModuleBuilder();
  builder.addFunction('call_StringFromCodePoint', kSig_v_v)
      .exportFunc()
      .addBody([
        ...wasmI32Const(0x110000),  // too large code_point
        ...GCInstr(kExprStringFromCodePoint),
        kExprDrop,
      ]);

  const instance = builder.instantiate({}, {builtins: ['js-string']});

  assertTraps(
      /Invalid code point [0-9]+/,
      () => instance.exports.call_StringFromCodePoint());
})();

(function Trap_StringEncodeWtf16() {
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, undefined);
  builder.exportMemoryAs('memory');

  let kSig_v_wi = makeSig([kWasmStringRef, kWasmI32], []);
  builder.addFunction('call_StringEncodeWtf16', kSig_v_wi)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        ...GCInstr(kExprStringEncodeWtf16), 0,
        kExprDrop,
      ]);

  const instance = builder.instantiate({}, {builtins: ['js-string']});
  let memory = new Uint8Array(instance.exports.memory.buffer);

  assertTraps(
      kTrapMemOutOfBounds,
      () => instance.exports.call_StringEncodeWtf16(
          'test string', memory.length));
})();

(function Trap_StringViewWtf8Encode() {
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, undefined);
  builder.exportMemoryAs('memory');
  let kSig_v_wiii = makeSig(
      [kWasmStringRef, kWasmI32, kWasmI32, kWasmI32], []);
  builder.addFunction('call_StringViewWtf8Encode', kSig_v_wiii)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        ...GCInstr(kExprStringAsWtf8),
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kExprLocalGet, 3,
        ...GCInstr(kExprStringViewWtf8EncodeWtf8), 0,
        kExprDrop,
        kExprDrop,
      ]);

  const instance = builder.instantiate({}, {builtins: ['js-string']});
  let memory = new Uint8Array(instance.exports.memory.buffer);

  assertTraps(
      kTrapMemOutOfBounds,
      () => instance.exports.call_StringViewWtf8Encode(
          'test string', memory.length, 0, 10));
})();
