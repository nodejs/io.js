// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-test-streaming --turbofan --no-always-turbofan
// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/gc-js-interop-helpers.js');

let {struct, array} = CreateWasmObjects();
for (const wasm_obj of [struct, array]) {
  testThrowsRepeated(() => new WebAssembly.Global(wasm_obj), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Global({value: wasm_obj}), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Global({value: 'i32'}, wasm_obj), TypeError);
  repeated(
      () => assertSame(
          wasm_obj,
          (new WebAssembly.Global({value: 'anyref'}, wasm_obj)).value));

  testThrowsRepeated(() => new WebAssembly.Module(wasm_obj), TypeError);
  let module = () => {
    let buffer = (new Uint8Array((new WasmModuleBuilder()).toArray())).buffer;
    return new WebAssembly.Module(buffer);
  };
  testThrowsRepeated(
      () => WebAssembly.Module.customSections(wasm_obj), TypeError);
  testThrowsRepeated(
      () => WebAssembly.Module.customSections(module, wasm_obj), TypeError);
  testThrowsRepeated(() => WebAssembly.Module.exports(wasm_obj), TypeError);
  testThrowsRepeated(() => WebAssembly.Module.imports(wasm_obj), TypeError);

  testThrowsRepeated(() => new WebAssembly.Instance(wasm_obj), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Instance(module, wasm_obj), TypeError);

  repeated(() => assertThrowsAsync(WebAssembly.compile(wasm_obj), TypeError));
  repeated(
      () =>
          assertThrowsAsync(WebAssembly.compileStreaming(wasm_obj), TypeError));
  repeated(
      () => assertThrowsAsync(WebAssembly.instantiate(wasm_obj), TypeError));
  repeated(
      () => assertThrowsAsync(
          WebAssembly.instantiateStreaming(wasm_obj), TypeError));
  testThrowsRepeated(() => WebAssembly.validate(wasm_obj), TypeError);

  testThrowsRepeated(() => new WebAssembly.Memory(wasm_obj), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Memory({initial: wasm_obj}), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Memory({initial: 1, shared: wasm_obj}), TypeError);
  let memory = new WebAssembly.Memory({initial: 1});
  testThrowsRepeated(() => memory.grow(wasm_obj), TypeError);

  testThrowsRepeated(() => new WebAssembly.Table(wasm_obj), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Table({element: wasm_obj, initial: wasm_obj}),
      TypeError);
  let table = new WebAssembly.Table({initial: 1, element: 'externref'});
  testThrowsRepeated(() => table.get(wasm_obj), TypeError);
  testThrowsRepeated(() => table.grow(wasm_obj), TypeError);
  testThrowsRepeated(() => table.set(wasm_obj, null), TypeError);
  repeated(() => table.set(0, wasm_obj));

  testThrowsRepeated(() => new WebAssembly.Tag(wasm_obj), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Tag({parameters: wasm_obj}), TypeError);
  testThrowsRepeated(
      () => new WebAssembly.Tag({parameters: [wasm_obj]}), TypeError);

  let tag = new WebAssembly.Tag({
    parameters: [wasm_obj == struct ? 'structref' : 'arrayref']});
  testThrowsRepeated(() => new WebAssembly.Exception(wasm_obj), TypeError);
  testThrowsRepeated(() => new WebAssembly.Exception(tag, wasm_obj), TypeError);
  repeated(() => new WebAssembly.Exception(tag, [wasm_obj]));
  let exception = new WebAssembly.Exception(tag, [wasm_obj]);
  testThrowsRepeated(() => exception.is(wasm_obj), TypeError);
  testThrowsRepeated(() => exception.getArg(wasm_obj), TypeError);
  testThrowsRepeated(() => exception.getArg(tag, wasm_obj), TypeError);
  testThrowsRepeated(() => new WebAssembly.CompileError(wasm_obj), TypeError);
  testThrowsRepeated(() => new WebAssembly.LinkError(wasm_obj), TypeError);
  testThrowsRepeated(() => new WebAssembly.RuntimeError(wasm_obj), TypeError);

  // Ensure no statement re-assigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}
