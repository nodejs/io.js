// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestI32AtomicAddAlignmentCheck() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory(32, 128);

  builder.addFunction(`main`,
                      makeSig([kWasmI32, kWasmI32], [kWasmI32]))
  .addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kAtomicPrefix, kExprI32AtomicAdd, 1, 0,
  ]).exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
    /Compiling function #0:"main" failed: invalid alignment for atomic operation; expected alignment is 2, actual alignment is 1/);
})();
