// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Adapted test from 'inspector/debugger/wasm-externref-global.js'

utils.load('test/inspector/wasm-inspector-test.js');

const {session, contextGroup, Protocol} =
  InspectorTest.start('Don\'t crash when using non-zero line number to set WASM breakpoint');

(async () => {
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal('m', 'global', kWasmExternRef, false);
  let func = builder.addFunction('func', kSig_v_v)
    .addBody([
      kExprGlobalGet, 0,  //
      kExprDrop,          //
    ])
    .exportAs('main');
  let moduleBytes = JSON.stringify(builder.toArray());

  function test(moduleBytes) {
    let module = new WebAssembly.Module((new Uint8Array(moduleBytes)).buffer);
    let global = 'hello, world';
    instance = new WebAssembly.Instance(module, { m: { global } });
  }

  Protocol.Debugger.enable();
  Protocol.Runtime.evaluate({
    expression: `
      let instance;
      ${test.toString()}
      test(${moduleBytes});`
  });

  InspectorTest.log('Waiting for wasm script to be parsed.');
  let scriptId;
  while (true) {
    let msg = await Protocol.Debugger.onceScriptParsed();
    if (msg.params.url.startsWith('wasm://')) {
      scriptId = msg.params.scriptId;
      break;
    }
  }

  InspectorTest.log('Setting breakpoint in wasm.');
  InspectorTest.logMessage(await Protocol.Debugger.setBreakpoint(
    { location: { scriptId, lineNumber: 42, columnNumber: func.body_offset } }));

  InspectorTest.completeTest();
})();
