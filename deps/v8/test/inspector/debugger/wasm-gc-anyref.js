// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Test inspection of Wasm anyref objects');
session.setupScriptMap();
Protocol.Runtime.enable();
Protocol.Debugger.enable();
Protocol.Debugger.onPaused(printPauseLocationsAndContinue);

let breakpointLocation = -1;

InspectorTest.runAsyncTestSuite([
  async function test() {
    let wasm_promise = instantiateWasm();
    let scriptIds = await waitForWasmScripts();
    await wasm_promise;  // Make sure the instantiation is finished.

    // Set a breakpoint.
    InspectorTest.log('Setting breakpoint');
    let breakpoint = await Protocol.Debugger.setBreakpoint(
        {'location': {'scriptId': scriptIds[0],
                      'lineNumber': 0,
                      'columnNumber': breakpointLocation}});
    printIfFailure(breakpoint);
    InspectorTest.logMessage(breakpoint.result.actualLocation);

    // Now run the wasm code.
    await WasmInspectorTest.evalWithUrl('instance.exports.main()', 'runWasm');
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
  Protocol.Debugger.resume();
}

async function instantiateWasm() {
  var builder = new WasmModuleBuilder();
  builder.startRecGroup();
  let struct_type = builder.addStruct([makeField(kWasmI32, false)]);
  let array_type = builder.addArray(kWasmI32);
  let imported_ref_table =
      builder.addImportedTable('import', 'any_table', 4, 4, kWasmAnyRef);
  let imported_func_table =
      builder.addImportedTable('import', 'func_table', 3, 3, kWasmFuncRef);
  let ref_table = builder.addTable(kWasmAnyRef, 4)
                         .exportAs('exported_ref_table');
  let func_table = builder.addTable(kWasmFuncRef, 3)
                         .exportAs('exported_func_table');
  let i31ref_table = builder.addTable(kWasmI31Ref, 3)
                         .exportAs('exported_i31_table');

  let func = builder.addFunction('my_func', kSig_v_v).addBody([kExprNop]);
  // Make the function "declared".
  builder.addGlobal(kWasmFuncRef, false, false, [kExprRefFunc, func.index]);

  builder.addFunction('fill_tables', kSig_v_v)
    .addBody([
      ...wasmI32Const(0), ...wasmI32Const(123),
      kGCPrefix, kExprStructNew, struct_type, kExprTableSet, ref_table.index,
      ...wasmI32Const(1), ...wasmI32Const(20), ...wasmI32Const(21),
      kGCPrefix, kExprArrayNewFixed, array_type, 2,
      kExprTableSet, ref_table.index,
      ...wasmI32Const(2), ...wasmI32Const(30),
      kGCPrefix, kExprRefI31, kExprTableSet, ref_table.index,

      // Fill imported any table.
      ...wasmI32Const(1),
      ...wasmI32Const(123), kGCPrefix, kExprStructNew, struct_type,
      kExprTableSet, imported_ref_table,

      ...wasmI32Const(1),
      ...wasmI32Const(321), kGCPrefix, kExprRefI31,
      kExprTableSet, imported_ref_table,

      // Fill imported func table.
      ...wasmI32Const(1),
      kExprRefFunc, func.index,
      kExprTableSet, imported_func_table,

      // Fill func table.
      ...wasmI32Const(1),
      kExprRefFunc, func.index,
      kExprTableSet, func_table.index,

      // Fill i31 table.
      ...wasmI32Const(0),
      ...wasmI32Const(123456), kGCPrefix, kExprRefI31,
      kExprTableSet, i31ref_table.index,

      ...wasmI32Const(1),
      ...wasmI32Const(-123), kGCPrefix, kExprRefI31,
      kExprTableSet, i31ref_table.index,
    ]).exportFunc();

  let body = [
    // Set local anyref_local to new struct.
    ...wasmI32Const(12),
    kGCPrefix, kExprStructNew, struct_type,
    kExprLocalSet, 0,
    // Set local anyref_local2 to new array.
    ...wasmI32Const(21),
    kGCPrefix, kExprArrayNewFixed, array_type, 1,
    kExprLocalSet, 1,
    ...wasmI32Const(30),
    kGCPrefix, kExprRefI31,
    kExprLocalSet, 2,
    kExprNop,
  ];
  let main = builder.addFunction('main', kSig_v_v)
      .addLocals(kWasmAnyRef, 1, ['anyref_local'])
      .addLocals(kWasmAnyRef, 1, ['anyref_local2'])
      .addLocals(kWasmAnyRef, 1, ['anyref_local_i31'])
      .addLocals(kWasmAnyRef, 1, ['anyref_local_null'])
      .addBody(body)
      .exportFunc();
  builder.endRecGroup();
  var module_bytes = builder.toArray();
  breakpointLocation = main.body_offset + body.length - 1;

  InspectorTest.log('Calling instantiate function.');
  let imports = `{'import' : {
      'any_table': (() => {
        let js_table =
            new WebAssembly.Table({element: 'anyref', initial: 4, maximum: 4});
        js_table.set(0, ['JavaScript', 'value']);
        return js_table;
      })(),
      'func_table': (() => {
        let func_table =
            new WebAssembly.Table({element: 'anyfunc', initial: 3, maximum: 3});
        func_table.set(0, new WebAssembly.Function(
          {parameters:['i32', 'i32'], results: ['i32']},
          function /*anonymous*/ (a, b) { return a * b; }));
        return func_table;
      })(),
    }}`;
  await WasmInspectorTest.instantiate(module_bytes, 'instance', imports);
  InspectorTest.log('Module instantiated.');
  await WasmInspectorTest.evalWithUrl(
    'instance.exports.fill_tables();', 'fill_tables');
  await WasmInspectorTest.evalWithUrl(
      `instance.exports.exported_func_table.set(0, new WebAssembly.Function(
          {parameters:['i32', 'i32'], results: ['i32']},
          function external_fct(a, b) { return a * b; }))`,
      'add_func_to_table');
  InspectorTest.log('Tables populated.');
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
