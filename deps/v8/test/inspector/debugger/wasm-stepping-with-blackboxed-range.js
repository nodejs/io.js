// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} =
    InspectorTest.start('Tests stepping through wasm script with blackboxed range by byte offsets');
session.setupScriptMap();

var main_module = new WasmModuleBuilder();

var not_ignored_function_idx = main_module
    .addFunction('not_ignored', kSig_v_v)
    .addBody([kExprNop, kExprNop])
    .index;

var ignored_function = main_module
    .addFunction('ignored', kSig_v_v)
    .addBody([kExprNop, kExprCallFunction, not_ignored_function_idx])

var func_a_idx = main_module
    .addFunction('wasm_A', kSig_v_i)
    .addBody([kExprNop, kExprNop])
    .index;

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
          kExprCallFunction, ignored_function.index,
          kExprBr, 1,                     // continue
          kExprEnd,                       // -
        kExprEnd,                         // break
      // clang-format on
    ])
    .exportAs('main');

var main_module_bytes = main_module.toArray();

InspectorTest.runAsyncTestSuite([
  async function test() {
    for (const action of ['stepInto', 'stepOver', 'stepOut', 'resume'])
      InspectorTest.logProtocolCommandCalls('Debugger.' + action);

    await Protocol.Runtime.enable();
    await Protocol.Debugger.enable();
    InspectorTest.log('Setting up global instance variable.');

    WasmInspectorTest.instantiate(main_module_bytes);
    const [, {params: mainWasmScript}] = await Protocol.Debugger.onceScriptParsed(2)

    InspectorTest.log('Got wasm script: ' + mainWasmScript.url);

    var function_declaration_size = 2
    var ignored_function_start = ignored_function.body_offset - function_declaration_size
    var ignored_function_end = ignored_function_start + ignored_function.body.length + function_declaration_size

    await Protocol.Debugger.setBlackboxedRanges({
      scriptId: mainWasmScript.scriptId,
      positions: [
        {lineNumber: 0, columnNumber: ignored_function_start},
        {lineNumber: 0, columnNumber: ignored_function_end},
      ]
    })

    InspectorTest.log('Blackbox wasm script ' + mainWasmScript.url + ' in range from ' + ignored_function_start + ' to ' + ignored_function_end);

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
