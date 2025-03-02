// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

InspectorTest.log("Tests how wasm scripts are reported with name");

let contextGroup = new InspectorTest.ContextGroup();
let sessions = [
  // Main session.
  trackScripts(),
];

var builder = new WasmModuleBuilder();
builder.addFunction('nopFunction', kSig_v_v).addBody([kExprNop]);
var module_bytes = builder.toArray();
builder.setName('moduleName');
var module_bytes_with_name = builder.toArray();

function testFunction(bytes) {
  // Compilation triggers registration of wasm scripts.
  new WebAssembly.Module(new Uint8Array(bytes));
}

contextGroup.addScript(testFunction.toString(), 0, 0, 'v8://test/testFunction');
contextGroup.addScript('var module_bytes = ' + JSON.stringify(module_bytes));
contextGroup.addScript('var module_bytes_with_name = ' + JSON.stringify(module_bytes_with_name));

InspectorTest.log(
    'Check that the inspector gets four wasm scripts at module creation time.');

sessions[0].Protocol.Runtime
    .evaluate({
      'expression': '//# sourceURL=v8://test/runTestRunction\n' +
          'testFunction(module_bytes); testFunction(module_bytes_with_name);'
    })
    .then(() => (
      // At this point all scripts were parsed.
      // Stop tracking and wait for script sources in each session.
      Promise.all(sessions.map(session => session.getScripts()))
    ))
    .catch(err => {
      InspectorTest.log(err.stack);
    })
    .then(() => InspectorTest.completeTest());

function trackScripts(debuggerParams) {
  let {id: sessionId, Protocol} = contextGroup.connect();
  let scripts = [];

  Protocol.Debugger.enable(debuggerParams);
  Protocol.Debugger.onScriptParsed(handleScriptParsed);

  async function loadScript({url, scriptId}) {
    InspectorTest.log(`Session #${sessionId}: Script #${scripts.length} parsed. URL: ${url}.`);
    let scriptSource;
    ({result: {scriptSource}} = await Protocol.Debugger.getScriptSource({scriptId}));
    InspectorTest.log(`Session #${sessionId}: Source for ${url}:`);
    InspectorTest.log(scriptSource);
  }

  function handleScriptParsed({params}) {
    if (params.url.startsWith("wasm://")) {
      scripts.push(loadScript(params));
    }
  }

  return {
    Protocol,
    getScripts: () => Promise.all(scripts),
  };
}
