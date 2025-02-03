// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-imported-strings

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $array = builder.addArray(kWasmI16, true, kNoSuperType, true);
let $sig_import = builder.addType(makeSig(
    [wasmRefNullType($array), kWasmI32, kWasmI32],
    [wasmRefType(kWasmExternRef)]));
let $fromCharCodeArray =
    builder.addImport('wasm:js-string', 'fromCharCodeArray', $sig_import);

builder.addFunction('main', kSig_i_iii)
    .addBody([
      kExprI32Const, 10,
      kExprI32Const, 41,
      kGCPrefix, kExprArrayNew, $array,
      kExprI32Const, 5,  // start
      kExprI32Const, 100,  // end
      // The `fromCharCodeArray` tries converting the array range [start, end)
      // to a String, but traps, as the end is outside of the bounds of the
      // array.
      kExprCallFunction,
      $fromCharCodeArray,
      // We should not reach here, as we should have trapped.
      kExprUnreachable,
    ]).exportFunc();

let kBuiltins = {builtins: ['js-string', 'text-decoder', 'text-encoder']};
const instance = builder.instantiate({}, kBuiltins);
assertTraps(kTrapArrayOutOfBounds, () => instance.exports.main(1, 2, 3));
