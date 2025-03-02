// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} = InspectorTest.start('Tests call stack in wasm scripts');

var builder = new WasmModuleBuilder();

var imported_idx = builder.addImport("mode", "func", kSig_v_v);

var call_imported_idx = builder.addFunction('call_func', kSig_v_v)
  .addBody([kExprCallFunction, imported_idx])
  .index;

// Open a block in order to make the positions more interesting...
builder.addFunction('main', kSig_v_v)
  .addBody(
    [kExprBlock, kWasmVoid, kExprCallFunction, call_imported_idx, kExprEnd])
  .exportAs('main');

var module_bytes = builder.toArray();

function testFunction(bytes) {
  function call_debugger() {
    debugger;
  }

  var buffer = new ArrayBuffer(bytes.length);
  var view = new Uint8Array(buffer);
  for (var i = 0; i < bytes.length; i++) {
    view[i] = bytes[i] | 0;
  }

  var module = new WebAssembly.Module(buffer);
  var instance = new WebAssembly.Instance(module, {mode: {func: call_debugger}});

  instance.exports.main();
}

contextGroup.addScript(testFunction.toString());

Protocol.Debugger.enable();
Protocol.Debugger.onPaused(handleDebuggerPaused);
InspectorTest.log('Running testFunction with generated wasm bytes...');
Protocol.Runtime.evaluate(
    {'expression': 'testFunction(' + JSON.stringify(module_bytes) + ')'});

function locationToString(callFrame) {
  var res = {functionName: callFrame.functionName};
  for (var attr in callFrame.functionLocation) {
    if (attr == 'scriptId') continue;
    res['function_'+attr] = callFrame.functionLocation[attr];
  }
  for (var attr in callFrame.location) {
    if (attr == 'scriptId') continue;
    res[attr] = callFrame.location[attr];
  }
  return JSON.stringify(res);
}

function logStackTrace(messageObject) {
  var frames = messageObject.params.callFrames;
  InspectorTest.log('Number of frames: ' + frames.length);
  for (var i = 0; i < frames.length; ++i) {
    InspectorTest.log('  - [' + i + '] ' + locationToString(frames[i]));
  }
}

function handleDebuggerPaused(messageObject)
{
  InspectorTest.log('Paused on \'debugger;\'');
  logStackTrace(messageObject);
  InspectorTest.log('Getting v8-generated stack trace...');
  var topFrameId = messageObject.params.callFrames[0].callFrameId;
  Protocol.Debugger
      .evaluateOnCallFrame({
        callFrameId: topFrameId,
        expression: '(new Error("this is your stack trace:")).stack'
      })
      .then(callbackEvaluate);
}

function callbackEvaluate(response)
{
  InspectorTest.log(
      'Result of evaluate (' + response.result.result.type + '):');
  var result_lines = response.result.result.value.split('\n');
  // Skip the second line, containing the 'evaluate' position.
  result_lines[1] = '    -- skipped --';
  InspectorTest.log(result_lines.join('\n'));
  InspectorTest.log('Finished!');
  InspectorTest.completeTest();
}
