// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Test retrieving scope information from compiled Liftoff frames');
session.setupScriptMap();
Protocol.Runtime.enable();
Protocol.Debugger.enable();
Protocol.Debugger.onPaused(printPauseLocationsAndContinue);

let breakpointLocation = -1;

InspectorTest.runAsyncTestSuite([
  async function test() {
    // Instantiate wasm and wait for three wasm scripts for the three functions.
    instantiateWasm();
    let scriptIds = await waitForWasmScripts();

    // Set a breakpoint.
    InspectorTest.log(
        'Setting breakpoint on line 2 (first instruction) of third function');
    let breakpoint = await Protocol.Debugger.setBreakpoint(
        {'location': {'scriptId': scriptIds[0], 'lineNumber': 0, 'columnNumber': breakpointLocation}});
    printIfFailure(breakpoint);
    InspectorTest.logMessage(breakpoint.result.actualLocation);

    // Now run the wasm code.
    await WasmInspectorTest.evalWithUrl('instance.exports.main(42)', 'runWasm');
    InspectorTest.log('exports.main returned. Test finished.');
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
  Protocol.Debugger.stepOver();
}

async function instantiateWasm() {
  var builder = new WasmModuleBuilder();
  // Add a global, memory and exports to populate the module scope.
  builder.addGlobal(kWasmI32, true, false).exportAs('exported_global');
  builder.addMemory(1, 1);
  builder.exportMemoryAs('exported_memory');
  builder.addTable(kWasmAnyFunc, 3).exportAs('exported_table');

  // Add two functions without breakpoint, to check that locals and operand
  // stack values are shown correctly in Liftoff code.
  // Function A will have its parameter spilled to the stack, because it calls
  // function B.
  // Function B has a local with a constant value (not using a register), the
  // parameter will be held in a register.
  const main = builder.addFunction('A (liftoff)', kSig_v_i)
      .addBody([
        // Call function 'B', forwarding param 0.
        kExprLocalGet, 0, kExprCallFunction, 1
      ])
      .exportAs('main');

  builder.addFunction('B (liftoff)', kSig_v_i, ['i32_arg'])
      .addLocals(kWasmI32, 1, ['i32_local'])
      .addLocals(kWasmF32, 4, ['f32_local', '0', '0'])
      .addLocals(kWasmS128, 1, ['v128_local'])
      .addBody([
        // Load a parameter and a constant onto the operand stack.
        kExprLocalGet, 0, kExprI32Const, 3,
        // Set local 6 to v128 i32.x4 23, 23, 23, 23.
        kExprI32Const, 23,
        kSimdPrefix, kExprI32x4Splat,
        kExprLocalSet, 6,
        // Set local 2 to 7.2.
        ...wasmF32Const(7.2), kExprLocalSet, 2,
        // Call function 'C', forwarding param 0.
        kExprLocalGet, 0, kExprCallFunction, 2,
        // Drop the two operand stack values.
        kExprDrop, kExprDrop
      ]);

  // A third function which will be stepped through.
  let func = builder.addFunction('C (interpreted)', kSig_v_i, ['i32_arg'])
      .addLocals(kWasmI32, 1, ['i32_local'])
      .addLocals(kWasmF32, 1, [''])
      .addBody([
        // Set global 0 to param 0.
        kExprLocalGet, 0, kExprGlobalSet, 0,
        // Set local 1 to 47.
        kExprI32Const, 47, kExprLocalSet, 1,
      ]);

  // Append function to table to test function table output.
  builder.appendToTable([main.index]);

  var module_bytes = builder.toArray();
  breakpointLocation = func.body_offset;


  function addWasmJSToTable() {
    // Create WasmJS functions to test the function tables output.
    const js_func = function js_func() { return 7; };
    const wasmjs_func = new WebAssembly.Function({parameters:[], results:['i32']}, js_func);
    const wasmjs_anonymous_func = new WebAssembly.Function({parameters:[], results:['i32']}, _ => 7);

    instance.exports.exported_table.set(0, wasmjs_func);
    instance.exports.exported_table.set(1, wasmjs_anonymous_func);
  }

  InspectorTest.log('Calling instantiate function.');
  await WasmInspectorTest.instantiate(module_bytes);
  await WasmInspectorTest.evalWithUrl(`(${addWasmJSToTable})()`, 'populateTable');
}

function printIfFailure(message) {
  if (!message.result) {
    InspectorTest.logMessage(message);
  }
  return message;
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
