// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --no-liftoff --wasm-fast-api
// Flags: --wasm-lazy-compilation
// Flags: --fast-api-allow-float-in-sim --allow-natives-syntax


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestFastApiCallFromWasm() {
  const fast_c_api = new d8.test.FastCAPI();

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [
        kWasmI32, kWasmI32, kWasmI64, kWasmI64,
        kWasmF32, kWasmF64
      ],
      [kWasmF64],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // param int32
        kExprLocalGet, 1, // param uint32
        kExprLocalGet, 2, // param int64
        kExprLocalGet, 3, // param uint64
        kExprLocalGet, 4, // param float32
        kExprLocalGet, 5, // param float64
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  {
    const boundImport = fast_c_api.add_all_no_options.bind(0);
    const instance = builder.instantiate({ 'mod': { 'foo': boundImport } });
    instance.exports.main(1, 2, 3n, 4n, 5, 6);
  }
  {
    const boundImport = fast_c_api.add_all_no_options.bind(%GetUndetectable());
    const instance = builder.instantiate({ 'mod': { 'foo': boundImport } });
    instance.exports.main(1, 2, 3n, 4n, 5, 6);
  }
})();
