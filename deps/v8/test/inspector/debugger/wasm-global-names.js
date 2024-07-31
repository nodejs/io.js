// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} =
    InspectorTest.start('Test wasm global names');

let builder = new WasmModuleBuilder();
builder.addImportedGlobal('module_name', 'imported_global', kWasmI32, false);
let func = builder.addFunction('func', kSig_v_i)
               .addBody([
                 kExprGlobalGet, 0,  //
                 kExprDrop,          //
               ])
               .exportAs('main');
var o = builder.addGlobal(kWasmI32, true, false).exportAs('exported_global');
builder.addGlobal(kWasmI32, false, false);  // global2
let moduleBytes = builder.toArray();

InspectorTest.runAsyncTestSuite([
  async function test() {
    Protocol.Debugger.enable();
    Protocol.Runtime.evaluate({
      expression: `var instance = (${WasmInspectorTest.instantiateFromBuffer})(${JSON.stringify(moduleBytes)}, {module_name: {imported_global: 123}});`
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
    await Protocol.Debugger.setBreakpoint(
        {location: {scriptId, lineNumber: 0, columnNumber: func.body_offset}});

    InspectorTest.log('Running main.');
    Protocol.Runtime.evaluate({expression: 'instance.exports.main()'});

    let msg = await Protocol.Debugger.oncePaused();
    let callFrames = msg.params.callFrames;
    InspectorTest.log('Paused in debugger.');
    let scopeChain = callFrames[0].scopeChain;
    for (let scope of scopeChain) {
      if (scope.type != 'module') continue;
      let moduleObjectProps = (await Protocol.Runtime.getProperties({
                                'objectId': scope.object.objectId
                              })).result.result;

      for (let prop of moduleObjectProps) {
        if (prop.name != 'globals') continue;
        let subProps = (await Protocol.Runtime.getProperties({
                        objectId: prop.value.objectId
                      })).result.result;
        let values = subProps.map((value) => `"${value.name}"`).join(', ');
        InspectorTest.log(`   ${prop.name}: {${values}}`);
      }
    }
  }
]);
