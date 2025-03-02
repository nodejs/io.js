// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --no-liftoff --wasm-fast-api
// Flags: --wasm-lazy-compilation
// Flags: --fast-api-allow-float-in-sim

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestFastApiCallFromWasm() {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport = Function.prototype.call.bind(fast_c_api.add_all_no_options);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [
        kWasmExternRef, kWasmI32, kWasmI32, kWasmI64, kWasmI64,
        kWasmF32, kWasmF64
      ],
      [kWasmF64],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param int32
        kExprLocalGet, 2, // param uint32
        kExprLocalGet, 3, // param int64
        kExprLocalGet, 4, // param uint64
        kExprLocalGet, 5, // param float32
        kExprLocalGet, 6, // param float64
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  const fallback = true;
  fast_c_api.reset_counts();
  instance.exports.main(fast_c_api, 1, 2, 3n, 4n, 5, 6);
  assertEquals(1, fast_c_api.fast_call_count());
  instance.exports.main(fast_c_api, 1, 2, 3n, 4n, 5, 6);
  assertEquals(2, fast_c_api.fast_call_count());
  assertThrows(
      _ => instance.exports.main(12, 1, 2, 3n, 4n, 5, 6), TypeError);
  assertThrows(
      _ => instance.exports.main({}, 1, 2, 3n, 4n, 5, 6), TypeError);
})();

(function TestFastApiCallWithOverloadFromWasm() {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport = Function.prototype.call.bind(fast_c_api.add_all_overload);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [
        kWasmExternRef, kWasmI32, kWasmI32, kWasmI64, kWasmI64,
        kWasmF32, kWasmF64
      ],
      [kWasmF64],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param int32
        kExprLocalGet, 2, // param uint32
        kExprLocalGet, 3, // param int64
        kExprLocalGet, 4, // param uint64
        kExprLocalGet, 5, // param float32
        kExprLocalGet, 6, // param float64
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  fast_c_api.reset_counts();
  instance.exports.main(fast_c_api, 1, 2, 3n, 4n, 5, 6);
  assertEquals(1, fast_c_api.fast_call_count());
  instance.exports.main(fast_c_api, 1, 2, 3n, 4n, 5, 6);
  assertEquals(2, fast_c_api.fast_call_count());
  assertThrows(
      _ => instance.exports.main(12, 1, 2, 3n, 4n, 5, 6), TypeError);
  assertThrows(
      _ => instance.exports.main({}, 1, 2, 3n, 4n, 5, 6), TypeError);
})();
