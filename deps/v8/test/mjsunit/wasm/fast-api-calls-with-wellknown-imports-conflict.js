// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --no-liftoff --wasm-fast-api
// Flags: --wasm-lazy-compilation
// Flags: --no-wasm-native-module-cache
// Flags: --fast-api-allow-float-in-sim

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestI64AsNumber() {
  const fast_c_api = new d8.test.FastCAPI();

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [kWasmExternRef, kWasmF64, kWasmF64],
      [kWasmF64],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param
        kExprLocalGet, 2, // param
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const wasmModule = builder.toModule();

  const boundImport1 =
      Function.prototype.call.bind(fast_c_api.sum_int64_as_number);
  const instance1 =
      new WebAssembly.Instance(wasmModule, {'mod': {'foo': boundImport1}});

  fast_c_api.reset_counts();
  assertEquals(5, instance1.exports.main(fast_c_api, 2.5, 3.5));
  // With just one instantiation, the well-known imports optimization is valid,
  // and fast API calls should be used.
  assertEquals(1, fast_c_api.fast_call_count());

  const boundImport2 =
      Function.prototype.call.bind(fast_c_api.sum_uint64_as_number);
  const instance2 =
      new WebAssembly.Instance(wasmModule, {'mod': {'foo': boundImport2}});

  fast_c_api.reset_counts();
  assertEquals(5, instance2.exports.main(fast_c_api, 2.5, 3.5));
  // With a second instantiation that imports an API function with a different
  // signature, the well-known imports optimization is not valid anymore, and
  // regular API calls should be used.
  assertEquals(1, fast_c_api.slow_call_count());

  assertEquals(5, instance1.exports.main(fast_c_api, 2.5, 3.5));
  // Make sure that also the code of the first instance got flushed, and also
  // the first instance now uses regular calls.
  assertEquals(2, fast_c_api.slow_call_count());
  assertEquals(0, fast_c_api.fast_call_count());

  const instance3 =
      new WebAssembly.Instance(wasmModule, {'mod': {'foo': boundImport1}});

  fast_c_api.reset_counts();
  assertEquals(5, instance3.exports.main(fast_c_api, 2.5, 3.5));
  // Even when there is another instantiation with the same imports as the first
  // one, we still don't use fast API calls anymore.
  assertEquals(1, fast_c_api.slow_call_count());
  assertEquals(0, fast_c_api.fast_call_count());
})();
