// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --no-liftoff --wasm-fast-api

load('test/mjsunit/wasm/wasm-module-builder.js');

(function TestFastApiCallFromWasm() {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport = Function.prototype.call.bind(fast_c_api.add_all);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [
        kWasmExternRef, kWasmI32, kWasmI32, kWasmI32, kWasmI64, kWasmI64,
        kWasmF32, kWasmF64
      ],
      [kWasmF64],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // fallback
        kExprLocalGet, 2, // param int32
        kExprLocalGet, 3, // param uint32
        kExprLocalGet, 4, // param int64
        kExprLocalGet, 5, // param uint64
        kExprLocalGet, 6, // param float32
        kExprLocalGet, 7, // param float64
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  const fallback = true;
  instance.exports.main(fast_c_api, !fallback, 1, 2, 3n, 4n, 5, 6);
  instance.exports.main(fast_c_api, fallback, 1, 2, 3n, 4n, 5, 6);
  assertThrows(
      _ => instance.exports.main(12, false, 1, 2, 3n, 4n, 5, 6), TypeError);
  assertThrows(
      _ => instance.exports.main({}, false, 1, 2, 3n, 4n, 5, 6), TypeError);
})();

(function TestTaggedParam() {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport =
      Function.prototype.call.bind(fast_c_api.is_fast_c_api_object);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [kWasmExternRef, kWasmI32, kWasmExternRef],
      [kWasmI32],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // fallback
        kExprLocalGet, 2, // param
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  const fallback = true;
  assertEquals(1, instance.exports.main(fast_c_api, !fallback, fast_c_api));
  assertEquals(1, instance.exports.main(fast_c_api, fallback, fast_c_api));
  assertEquals(0, instance.exports.main(fast_c_api, !fallback, {}));
  assertEquals(0, instance.exports.main(fast_c_api, fallback, {}));
  assertEquals(0, instance.exports.main(fast_c_api, !fallback, 16));
  assertEquals(0, instance.exports.main(fast_c_api, fallback, 16));
})();
