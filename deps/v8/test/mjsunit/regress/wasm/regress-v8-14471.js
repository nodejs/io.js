// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-stack-switching
// Flags: --wasm-generic-wrapper

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function Regress14471() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let export_params = [kWasmExternRef, kWasmF32];
  let import_params = [kWasmExternRef, kWasmF32];
  const export_sig = makeSig(export_params, [kWasmExternRef]);
  const import_sig = makeSig(import_params, [kWasmExternRef]);
  const import_index = builder.addImport('m', 'import_js', import_sig);
  const fill_newspace_index = builder.addImport('m', 'fill_newspace', kSig_v_v);
  builder.addFunction("test", export_sig)
      .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      // After the params are converted in the generic wasm-to-js wrapper, the
      // signature slot is overwritten with a Smi which signals that parameter
      // scanning can be skipped for this frame.
      // To ensure that the parameters are scanned, we must trigger a GC during
      // parameter conversion. We do this by simulating a full newspace and
      // passing an f32.
      kExprCallFunction, fill_newspace_index,
      kExprCallFunction, import_index]).exportFunc();
  function import_js(arg0, arg1) {
    return [arg0, arg1];
  };
  function fill_newspace() { %SimulateNewspaceFull(); }
  let instance = builder.instantiate({m: {import_js, fill_newspace}});
  let wrapper = WebAssembly.promising(instance.exports.test);
  let args = [{}, 34];
  assertPromiseResult(
      wrapper(...args),
      results => { assertEquals(args, results); });
})();
