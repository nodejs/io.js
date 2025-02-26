// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --no-liftoff --wasm-fast-api
// Flags: --wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestTaggedParam() {
  const fast_c_api = new d8.test.FastCAPI();
  const boundImport =
      Function.prototype.call.bind(fast_c_api.is_fast_c_api_object);

  const builder = new WasmModuleBuilder();
  const sig = makeSig(
      [kWasmExternRef, kWasmExternRef],
      [kWasmI32],
  );
  const imp_index = builder.addImport('mod', 'foo', sig);
  builder.addFunction('main', sig)
      .addBody([
        kExprLocalGet, 0, // receiver
        kExprLocalGet, 1, // param
        kExprCallFunction, imp_index
      ])
      .exportFunc();

  const instance = builder.instantiate({'mod': {'foo': boundImport}});

  fast_c_api.reset_counts();
  assertEquals(1, instance.exports.main(fast_c_api, fast_c_api));
  assertEquals(0, instance.exports.main(fast_c_api, {}));
  assertEquals(0, instance.exports.main(fast_c_api, 16));
  assertEquals(3, fast_c_api.fast_call_count());
})();
