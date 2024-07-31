// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let instance = (() => {
  let builder = new WasmModuleBuilder();
  let structI32 = builder.addStruct([makeField(kWasmI32, true)]);
  let structI64 = builder.addStruct([makeField(kWasmI64, true)]);

    builder.addFunction('refCastNullUnrelated',
                        makeSig([kWasmExternRef], []))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kExprLocalTee, 1,
      // After this cast the local[1] can only be a subtype of
      // (ref null structI32).
      kGCPrefix, kExprRefCastNull, structI32,
      kExprDrop,
      kExprLocalGet, 1,
      // This cast can only succeed if local[1] is null as (ref null structI64)
      // is not a subtype of (ref null structI32).
      kGCPrefix, kExprRefCastNull, structI64,
      kExprDrop,
    ])
    .exportFunc();

  return builder.instantiate({});
})();

instance.exports.refCastNullUnrelated(null);
assertTraps(kTrapIllegalCast, () => instance.exports.refCastNullUnrelated({}));
