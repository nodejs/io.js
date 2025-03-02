// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $array1 = builder.addArray(kWasmI16, true, kNoSuperType, true);
let $sig4 = builder.addType(kSig_i_i);
let main19 = builder.addFunction(undefined, $sig4);

main19.addBody([
  kExprRefNull, kAnyRefCode,
  kGCPrefix, kExprRefCastNull, $array1,
  kExprI32Const, 42,
  kExprI32Const, 42,
  kGCPrefix, kExprArraySet, $array1,
  kExprRefNull, kAnyRefCode,
  kGCPrefix, kExprRefTestNull, kI31RefCode,
]);

builder.addExport('main', main19.index);

const instance = builder.instantiate({});
assertTraps(kTrapNullDereference, () => instance.exports.main(0));
%WasmTierUpFunction(instance.exports.main);
assertTraps(kTrapNullDereference, () => instance.exports.main(0));
