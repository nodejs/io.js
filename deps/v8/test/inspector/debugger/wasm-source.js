// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} = InspectorTest.start('Tests how wasm scrips report the source');

var builder = new WasmModuleBuilder();

var imported_idx = builder.addImport("xxx", "func", kSig_v_v);

var call_imported_idx = builder.addFunction("call_func", kSig_v_v)
    .addBody([kExprCallFunction, imported_idx])
    .index;

var sig_index = builder.addType(kSig_v_v);

builder.addFunction('main', kSig_v_v)
    .addBody([
      kExprBlock, kWasmVoid, kExprI32Const, 0, kExprCallIndirect, sig_index,
      kTableZero, kExprEnd
    ])
    .exportAs('main');

builder.appendToTable([call_imported_idx]);

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
  var instance = new WebAssembly.Instance(module, {xxx: {func: call_debugger}});

  instance.exports.main();
}

contextGroup.addScript(testFunction.toString());
contextGroup.addScript('var module_bytes = ' + JSON.stringify(module_bytes));

Protocol.Debugger.enable();
Protocol.Debugger.onPaused(handleDebuggerPaused);
InspectorTest.log('Check that inspector gets wasm bytecode');
Protocol.Runtime.evaluate({'expression': 'testFunction(module_bytes)'});

async function handleDebuggerPaused(message) {
  InspectorTest.log('Paused on debugger!');
  var frames = message.params.callFrames;
  InspectorTest.log('Number of frames: ' + frames.length);
  async function dumpSourceLine(frameId, sourceMessage) {
    if (sourceMessage.error) InspectorTest.logObject(sourceMessage);
    var text = sourceMessage.result.scriptSource;
    var lineNr = frames[frameId].location.lineNumber;
    if (text) {
      var line = text.split('\n')[lineNr];
      InspectorTest.log('[' + frameId + '] ' + line);
    } else {
      if (lineNr != 0) {
        InspectorTest.log('Unexpected line number in wasm: ' + lineNr);
      }
      let byteOffset = frames[frameId].location.columnNumber;
      let data = InspectorTest.decodeBase64(sourceMessage.result.bytecode);
      // Print a byte, which can be compared to the expected wasm opcode.
      InspectorTest.log('[' + frameId + '] Wasm offset ' + byteOffset
                        + ': 0x' + data[byteOffset].toString(16));
    }
  }

  for (let frameId = 0; frameId < frames.length; frameId++) {
    result = await Protocol.Debugger
        .getScriptSource({scriptId: frames[frameId].location.scriptId});
    await dumpSourceLine(frameId, result);
  }
  InspectorTest.log('Finished.');
  InspectorTest.completeTest();
}
