// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

InspectorTest.log("Tests how wasm scripts are reported");

let contextGroup = new InspectorTest.ContextGroup();
let sessions = [
  // Main session.
  trackScripts(),
  // Extra session to verify that all inspectors get same messages.
  // See https://bugs.chromium.org/p/v8/issues/detail?id=9725.
  trackScripts(),
];

// Create module with given custom sections.
function createModule(...customSections) {
  var builder = new WasmModuleBuilder();
  builder.addFunction('nopFunction', kSig_v_v).addBody([kExprNop]);
  builder.addFunction('main', kSig_v_v)
      .addBody([kExprBlock, kWasmVoid, kExprI32Const, 2, kExprDrop, kExprEnd])
      .exportAs('main');
  for (var { name, value } of customSections) {
    builder.addCustomSection(name, value);
  }
  return builder.toArray();
}

function testFunction(bytes) {
  // Compilation triggers registration of wasm scripts.
  new WebAssembly.Module(new Uint8Array(bytes));
}

// Generate stable IDs.
let scriptIds = {};
function nextStableId(id) {
  if (!(id in scriptIds)) {
    scriptIds[id] = Object.keys(scriptIds).length;
  }
  return scriptIds[id];
}

contextGroup.addScript(testFunction.toString(), 0, 0, 'v8://test/testFunction');

InspectorTest.log(
    'Check that each inspector gets a wasm script at module creation time.');

// Sample .debug_info section.
// Content doesn't matter, as we don't try to parse it in V8,
// but should be non-empty to check that we're skipping it correctly.
const embeddedDWARFSection = {
  name: '.debug_info',
  value: [1, 2, 3, 4, 5]
};

// Sample external_debug_info section set to "abc".
const externalDWARFSection = {
  name: 'external_debug_info',
  value: [3, 97, 98, 99]
};

// Sample sourceMappingURL section set to "abc".
const sourceMapSection = {
  name: 'sourceMappingURL',
  value: [3, 97, 98, 99]
};

sessions[0]
    .Protocol.Runtime
    .evaluate({
      'expression': `//# sourceURL=v8://test/runTestRunction

      // no debug info
      testFunction([${createModule()}]);

      // shared script for identical modules
      testFunction([${createModule()}]);

      // External DWARF
      testFunction([${createModule(externalDWARFSection)}]);

      // Embedded DWARF
      testFunction([${createModule(embeddedDWARFSection)}]);

      // Source map
      testFunction([${createModule(sourceMapSection)}]);

      // SourceMap + External DWARF
      testFunction([${createModule(sourceMapSection, externalDWARFSection)}]);

      // External DWARF + SourceMap (different order)
      testFunction([${createModule(externalDWARFSection, sourceMapSection)}]);

      // Embedded DWARF + External DWARF
      testFunction([${
          createModule(embeddedDWARFSection, externalDWARFSection)}]);

      // External + Embedded DWARF (different order)
      testFunction([${
          createModule(externalDWARFSection, embeddedDWARFSection)}]);

      // Embedded DWARF + source map
      testFunction([${createModule(embeddedDWARFSection, sourceMapSection)}]);

      // Source map + Embedded DWARF (different order)
      testFunction([${createModule(sourceMapSection, embeddedDWARFSection)}]);

      // Source map + Embedded DWARF + External DWARF
      testFunction([${createModule(sourceMapSection, embeddedDWARFSection, externalDWARFSection)}]);
      `
    })
    .then(
        () => (
            // At this point all scripts were parsed.
            // Stop tracking and wait for script sources in each session.
            Promise.all(sessions.map(session => session.getScripts()))))
    .catch(err => {
      InspectorTest.log(err.stack);
    })
    .then(() => InspectorTest.completeTest());

function trackScripts(debuggerParams) {
  let {id: sessionId, Protocol} = contextGroup.connect();
  let scripts = [];

  Protocol.Debugger.enable(debuggerParams);
  Protocol.Debugger.onScriptParsed(handleScriptParsed);

  function printDebugSymbols(symbols) {
    const symbolsLog = symbols.map(symbol => `${symbol.type}:${symbol.externalURL}`);
    return `debug symbols: [${symbolsLog}]`;
  }

  async function loadScript({
    url,
    scriptId,
    sourceMapURL,
    startColumn,
    endColumn,
    codeOffset,
    debugSymbols
  }) {
    let stableId = nextStableId(scriptId);
    InspectorTest.log(`Session #${sessionId}: Script #${
        scripts.length} parsed. URL: ${url}. Script ID: ${
        stableId}, Source map URL: ${sourceMapURL}, ${
        printDebugSymbols(debugSymbols)}. module begin: ${
        startColumn}, module end: ${endColumn}, code offset: ${codeOffset}`);
    let {result: {scriptSource, bytecode}} =
        await Protocol.Debugger.getScriptSource({scriptId});
    if (bytecode) {
      if (scriptSource) {
        InspectorTest.log('Unexpected scriptSource with bytecode: ');
        InspectorTest.log(scriptSource);
      }
      // Binary value is represented as base64 in JSON, decode it.
      bytecode = InspectorTest.decodeBase64(bytecode);
      // Check that it can be parsed back to a WebAssembly module.
      let module = new WebAssembly.Module(bytecode);
      scriptSource =
          `
Raw: ${Array.from(bytecode, b => ('0' + b.toString(16)).slice(-2)).join(' ')}
Imports: [${
              WebAssembly.Module.imports(module)
                  .map(i => `${i.name}: ${i.kind} from "${i.module}"`)
                  .join(', ')}]
Exports: [${
              WebAssembly.Module.exports(module)
                  .map(e => `${e.name}: ${e.kind}`)
                  .join(', ')}]
      `.trim();
    }
    InspectorTest.log(`Session #${sessionId}: Source for ${url}:`);
    InspectorTest.log(scriptSource);
  }

  function handleScriptParsed({params}) {
    if (params.url.startsWith('wasm://')) {
      scripts.push(loadScript(params));
    }
  }

  return {
    Protocol,
    getScripts: () => Promise.all(scripts),
  };
}
