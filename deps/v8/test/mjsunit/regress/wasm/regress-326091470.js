// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-imported-strings --experimental-wasm-inlining
// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let kRefExtern = wasmRefType(kWasmExternRef);
let kSig_e_i = makeSig([kWasmI32], [kRefExtern]);

const builder = new WasmModuleBuilder();

let kStringFromCharCode =
    builder.addImport('wasm:js-string', 'fromCharCode', kSig_e_i);

let callee = builder.addFunction('callee', makeSig([kRefExtern], [kRefExtern]));
callee.addBody([kExprLocalGet, 0]);

builder.addFunction('main', makeSig([], [kRefExtern])).exportFunc().addBody([
  ...wasmI32Const(65),
  kExprCallFunction, kStringFromCharCode,
  kExprCallFunction, callee.index,
]);

let instance = builder.instantiate({}, {builtins: ["js-string"]});
let main = instance.exports.main;

for (let i = 0; i < 10; i++) main();
%WasmTierUpFunction(main);
assertEquals("A", main());
