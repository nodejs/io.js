// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

// Test debugger type name resolution for ref types with multiple modules.
// Due to type canonicalization, for any given canonicalized type there is only
// one RTT. In the absence of a name section, we generate type names based on
// their canonicalized type index, e.g. "$canon3".

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Test inspection of Wasm GC objects with multiple modules');
session.setupScriptMap();
Protocol.Runtime.enable();
Protocol.Debugger.enable();
Protocol.Debugger.onPaused(printPauseLocationsAndContinue);

let breakpointLocation = -1;

InspectorTest.runAsyncTestSuite([
  async function test() {
    // Instantiate two modules. Module B has the rec type group of Module A,
    // however at a different type index offset.
    let wasmPromiseA = instantiateWasm('A', false);
    let scriptIdsA = await waitForWasmScripts();
    let breakLocationA = breakpointLocation;
    let wasmPromiseB = instantiateWasm('B', true);
    let scriptIdsB = await waitForWasmScripts();
    let breakLocationB = breakpointLocation;
    await wasmPromiseA;  // Make sure the instantiation is finished.
    await wasmPromiseB;  // Make sure the instantiation is finished.

    // Set a breakpoint on each module.
    async function setBreakpoint(scriptId, breakAt) {
      InspectorTest.log('Setting breakpoint');
      let breakpoint = await Protocol.Debugger.setBreakpoint(
          {'location': {'scriptId': scriptId,
                        'lineNumber': 0,
                        'columnNumber': breakAt}});
      printIfFailure(breakpoint);
      InspectorTest.logMessage(breakpoint.result.actualLocation);
    }
    setBreakpoint(scriptIdsA[0], breakLocationA);
    setBreakpoint(scriptIdsB[0], breakLocationB);

    // Now run the wasm code.
    await WasmInspectorTest.evalWithUrl(
      // Note: Neither the function that has the breakpoint nor the function
      // that created the object is relevant for the type name printed but the
      // order in which recgroups were encountered. In this case
      // `createStruct` for both modules returns a struct that is printed as
      // 'ref $canon3'.
      // Similarly `createArray` - which only exists in module B - derives its
      // name from the canonical type index assigned to it, in this case
      // `ref $canon6`.
      `instanceA.exports.main(
        instanceB.exports.createArray(10),
        instanceA.exports.createStruct(11)
      );
      instanceB.exports.main(
        instanceB.exports.createArray(10),
        instanceA.exports.createStruct(11)
      );`,
      'runWasm');
    InspectorTest.log('main returned. Test finished.');
  }
]);

async function printPauseLocationsAndContinue(msg) {
  let loc = msg.params.callFrames[0].location;
  InspectorTest.log('Paused:');
  await session.logSourceLocation(loc);
  InspectorTest.log('Scope:');
  for (var frame of msg.params.callFrames) {
    var isWasmFrame = /^wasm/.test(frame.url);
    var functionName = frame.functionName || '(anonymous)';
    var lineNumber = frame.location.lineNumber;
    var columnNumber = frame.location.columnNumber;
    InspectorTest.log(`at ${functionName} (${lineNumber}:${columnNumber}):`);
    for (var scope of frame.scopeChain) {
      InspectorTest.logObject(' - scope (' + scope.type + '):');
      if (!isWasmFrame && scope.type == 'global') {
        // Skip global scope for non wasm-functions.
        InspectorTest.logObject('   -- skipped globals');
        continue;
      }
      var properties = await Protocol.Runtime.getProperties(
          {'objectId': scope.object.objectId});
      await WasmInspectorTest.dumpScopeProperties(properties);
    }
  }
  InspectorTest.log();
  Protocol.Debugger.resume();
}

async function instantiateWasm(module_name, add_extra_types) {
  var builder = new WasmModuleBuilder();
  let array_type;
  if (add_extra_types) {
    builder.startRecGroup();
    array_type = builder.addArray(kWasmI32, true);
    builder.endRecGroup();
  }
  builder.startRecGroup();
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  builder.endRecGroup();

  builder.addFunction('createStruct', makeSig([kWasmI32], [kWasmAnyRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct_type,
    ])
    .exportFunc();

  if (add_extra_types) {
    builder.addFunction('createArray', makeSig([kWasmI32], [kWasmAnyRef]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprArrayNewFixed, array_type, 1,
      ])
      .exportFunc();
  }

  let body = [
    kExprNop,
  ];
  let main = builder.addFunction('main', makeSig([kWasmAnyRef, kWasmAnyRef], []))
      .addBody(body)
      .exportFunc();
  var module_bytes = builder.toArray();
  breakpointLocation = main.body_offset + body.length - 1;

  InspectorTest.log(`Calling instantiate function for module ${module_name}.`);
  let imports = `{}`;
  await WasmInspectorTest.instantiate(
    module_bytes, `instance${module_name}`, imports);
  InspectorTest.log(`Module ${module_name} instantiated.`);
}

async function waitForWasmScripts() {
  InspectorTest.log('Waiting for wasm script to be parsed.');
  let wasm_script_ids = [];
  while (wasm_script_ids.length < 1) {
    let script_msg = await Protocol.Debugger.onceScriptParsed();
    let url = script_msg.params.url;
    if (url.startsWith('wasm://')) {
      InspectorTest.log('Got wasm script!');
      wasm_script_ids.push(script_msg.params.scriptId);
    }
  }
  return wasm_script_ids;
}
