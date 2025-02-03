// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} =
    InspectorTest.start('Tests stepping through wasm scripts by byte offsets');
session.setupScriptMap();

var not_ignored_module = new WasmModuleBuilder();
not_ignored_module
    .addFunction('not_ignored', kSig_v_v)
    .addBody([kExprNop, kExprNop])
    .exportAs('not_ignored')

var not_ignored_module_bytes = not_ignored_module.toArray();

var ignored_module = new WasmModuleBuilder();

var imported_not_ignored_function_idx =
  ignored_module.addImport('not_ignored_module', 'not_ignored', kSig_v_v)

ignored_module
    .addFunction('ignored', kSig_v_v)
    .addBody([kExprNop, kExprCallFunction, imported_not_ignored_function_idx])
    .exportAs('ignored')

var ignored_module_bytes = ignored_module.toArray();

var main_module = new WasmModuleBuilder();

var imported_ignored_function_idx =
    main_module.addImport('ignored_module', 'ignored', kSig_v_v)

var func_a_idx =
    main_module.addFunction('wasm_A', kSig_v_i).addBody([kExprNop, kExprNop]).index;

// wasm_B calls wasm_A <param0> times.
var func_b = main_module.addFunction('wasm_B', kSig_v_i)
    .addBody([
      // clang-format off
      kExprLoop, kWasmVoid,               // while
        kExprLocalGet, 0,                 // -
        kExprIf, kWasmVoid,               // if <param0> != 0
          kExprLocalGet, 0,               // -
          kExprI32Const, 1,               // -
          kExprI32Sub,                    // -
          kExprLocalSet, 0,               // decrease <param0>
          ...wasmI32Const(1024),          // some longer i32 const (2 byte imm)
          kExprCallFunction, func_a_idx,  // -
          kExprCallFunction, imported_ignored_function_idx,
          kExprBr, 1,                     // continue
          kExprEnd,                       // -
        kExprEnd,                         // break
      // clang-format on
    ])
    .exportAs('main');

let fact = main_module.addFunction('fact', kSig_i_i)
    .addLocals(kWasmI32, 1)
    .addBody([
    // clang-format off
    kExprLocalGet, 0,
    kExprIf, kWasmI32,               // if <param0> != 0
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprCallFunction, 3,
      kExprLocalGet, 0,
      kExprI32Mul,                   //   return fact(<param0> - 1) * <param0>
    kExprElse,                       // else
      kExprI32Const, 1,              //   return 1
    kExprEnd,
    // clang-format on
  ])
  .exportAs('fact');

var main_module_bytes = main_module.toArray();

InspectorTest.runAsyncTestSuite([
  async function test() {
    for (const action of ['stepInto', 'stepOver', 'stepOut', 'resume'])
      InspectorTest.logProtocolCommandCalls('Debugger.' + action);

    await Protocol.Runtime.enable();
    await Protocol.Debugger.enable();
    InspectorTest.log('Setting up global instance variable.');


    WasmInspectorTest.instantiate(not_ignored_module_bytes, 'not_ignored_module');
    const [, {params: notIgnoredModuleScript}] = await Protocol.Debugger.onceScriptParsed(2);

    InspectorTest.log('Got wasm script: ' + notIgnoredModuleScript.url);

    WasmInspectorTest.instantiate(ignored_module_bytes, 'ignored_module', '{not_ignored_module: not_ignored_module.exports}');
    const [, {params: ignoredModuleScript}] = await Protocol.Debugger.onceScriptParsed(2);

    InspectorTest.log('Got wasm script: ' + ignoredModuleScript.url);

    WasmInspectorTest.instantiate(main_module_bytes, 'instance', '{ignored_module: ignored_module.exports}');
    const [, {params: mainWasmScript}] = await Protocol.Debugger.onceScriptParsed(2)

    InspectorTest.log('Got wasm script: ' + mainWasmScript.url);

    await Protocol.Debugger.setBlackboxPatterns({patterns: [ignoredModuleScript.url]})

    InspectorTest.log('Blackbox wasm script: ' + ignoredModuleScript.url);

    // Set the breakpoint on a non-breakable position. This should resolve to the
    // next instruction.
    var offset = func_b.body_offset + 15;
    InspectorTest.log(
        `Setting breakpoint on offset ` + offset + ` (should be propagated to ` +
          (offset + 1) + `, the offset of the call), url ${mainWasmScript.url}`);
    let bpmsg = await Protocol.Debugger.setBreakpoint({
      location: {scriptId: mainWasmScript.scriptId, lineNumber: 0, columnNumber: offset}
    });

    InspectorTest.logMessage(bpmsg.result.actualLocation);
    Protocol.Runtime.evaluate({ expression: 'instance.exports.main(4)' });
    await waitForPauseAndStep('stepInto');  // into call to wasm_A
    await waitForPauseAndStep('stepOver');  // over first nop
    await waitForPauseAndStep('stepOut');   // out of wasm_A
    await waitForPauseAndStep('stepOut');   // out of wasm_B, stop on breakpoint
    await waitForPauseAndStep('stepOver');  // over call
    await waitForPauseAndStep('stepInto');  // into call to ignored
    await waitForPauseAndStep('stepOver');  // over not_ignored nop
    await waitForPauseAndStep('stepOut');   // out of not_ignored
    await waitForPauseAndStep('stepInto');  // == stepOver br
    await waitForPauseAndStep('resume');    // to next breakpoint (3rd iteration)
    await waitForPauseAndStep('stepInto');  // into wasm_A
    await waitForPauseAndStep('stepOut');   // out to wasm_B
    await waitForPauseAndStep('stepOver');  // over ignored
    // Now step 14 times, until we are in wasm_A and in ignore_func again.
    for (let i = 0; i < 14; ++i) await waitForPauseAndStep('stepInto');
    // 3 more times, back to wasm_B.
    for (let i = 0; i < 3; ++i) await waitForPauseAndStep('stepInto');
    // Then just resume.
    await waitForPauseAndStep('resume');
    InspectorTest.log('exports.main returned!');

    InspectorTest.log('Test stepping over a recursive call');
    // Set a breakpoint at the recursive call and run.
    offset = fact.body_offset + 9; // Offset of the recursive call instruction.
    InspectorTest.log(
        `Setting breakpoint on the recursive call instruction @+` + offset +
        `, url ${mainWasmScript.url}`);
    bpmsg = await Protocol.Debugger.setBreakpoint({
      location: {scriptId: mainWasmScript.scriptId, lineNumber: 0, columnNumber: offset}
    });

    actualLocation = bpmsg.result.actualLocation;
    InspectorTest.logMessage(actualLocation);
    Protocol.Runtime.evaluate({ expression: 'instance.exports.fact(4)' });
    await waitForPause();

    // Remove the breakpoint before stepping over.
    InspectorTest.log('Removing breakpoint');
    let breakpointId = bpmsg.result.breakpointId;
    await Protocol.Debugger.removeBreakpoint({breakpointId});
    await Protocol.Debugger.stepOver();
    await waitForPauseAndStep('resume');
  }
]);

async function waitForPauseAndStep(stepAction) {
  await waitForPause();
  Protocol.Debugger[stepAction]();
}

async function waitForPause() {
  const {params: {callFrames}} = await Protocol.Debugger.oncePaused();
  const [currentFrame] = callFrames;
  await InspectorTest.log('Function: ' + currentFrame.functionName);
  await session.logSourceLocation(currentFrame.location);
}
