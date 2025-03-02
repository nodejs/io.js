// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

const { session, contextGroup, Protocol } =
  InspectorTest.start('Test buildId from Wasm modules.');

function generateBuildIdSection(bytes) {
  const size = bytes.length;
  let section = [...wasmUnsignedLeb(size)].concat(bytes);
  return new Uint8Array(section);
}

InspectorTest.runAsyncTestSuite([
  async function testWasmModuleWithBuildId() {
    const builder = new WasmModuleBuilder();
    builder.addFunction('foo', kSig_v_v).addBody([kExprNop]);
    builder.addCustomSection('build_id', generateBuildIdSection([
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f]));
    const module_bytes = builder.toArray();

    await Protocol.Runtime.enable();
    await Protocol.Debugger.enable();
    InspectorTest.log('Instantiate wasm module.');
    WasmInspectorTest.instantiate(module_bytes);
    InspectorTest.log(
      'Waiting for wasm script (ignoring first non-wasm script).');
    const [, { params: wasm_script }] = await Protocol.Debugger.onceScriptParsed(2);
    InspectorTest.log('done');
    InspectorTest.logMessage(wasm_script);
    InspectorTest.log('Done.');
    await Protocol.Debugger.disable();
    await Protocol.Runtime.disable();
  },
  async function testWasmModuleWithoutBuildId() {
    const builder = new WasmModuleBuilder();
    builder.addFunction('foo', kSig_v_v).addBody([kExprNop]);
    const module_bytes = builder.toArray();

    await Protocol.Runtime.enable();
    await Protocol.Debugger.enable();
    InspectorTest.log('Instantiate wasm module.');
    WasmInspectorTest.instantiate(module_bytes);
    InspectorTest.log(
      'Waiting for wasm script (ignoring first non-wasm script).');
    const [, { params: wasm_script }] = await Protocol.Debugger.onceScriptParsed(2);
    InspectorTest.log('done');
    InspectorTest.logMessage(wasm_script);
    InspectorTest.log('Done.');
    await Protocol.Debugger.disable();
    await Protocol.Runtime.disable();
  },

]);
