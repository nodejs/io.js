// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
    InspectorTest.start('Tests Runtime.terminateExecution on pause');

Protocol.Runtime.enable();
Protocol.Runtime.onConsoleAPICalled(
    message => InspectorTest.logMessage(message.params.args[0]));
Protocol.Debugger.enable();

InspectorTest.runAsyncTestSuite([
  async function testTerminateOnDebugger() {
    Protocol.Runtime.evaluate({expression: `
function callback() {
  debugger;
  console.log(42);
  setTimeout(callback, 0);
}
callback();
//# sourceURL=test.js`});
    await Protocol.Debugger.oncePaused();
    const terminated = Protocol.Runtime.terminateExecution();
    await Protocol.Debugger.resume();
    await terminated;
  },

  async function testTerminateAtBreakpoint() {
    Protocol.Debugger.setBreakpointByUrl({url: 'test.js', lineNumber: 2});
    const result = Protocol.Runtime.evaluate({expression: `
function callback() {
  console.log(42);
  setTimeout(callback, 0);
}
callback();
//# sourceURL=test.js`}).then(InspectorTest.logMessage);
    await Protocol.Debugger.oncePaused();
    const terminated = Protocol.Runtime.terminateExecution();
    await Protocol.Debugger.resume();
    await terminated;
    await result;
  },

  async function testTerminateRuntimeEvaluateOnCallFrame() {
    Protocol.Runtime.evaluate({expression: `
function callback() {
  let a = 1;
  debugger;
  console.log(43);
}
callback();
//# sourceURL=test.js`});
    let message = await Protocol.Debugger.oncePaused();
    let topFrameId = message.params.callFrames[0].callFrameId;
    await Protocol.Debugger.evaluateOnCallFrame({callFrameId: topFrameId, expression: "a"})
    .then(InspectorTest.logMessage)
    await Promise.all([
      Protocol.Runtime.terminateExecution().then(InspectorTest.logMessage),
      Protocol.Debugger.evaluateOnCallFrame({callFrameId: topFrameId, expression: "a"})
          .then(InspectorTest.logMessage)
    ]);
    await Protocol.Debugger.resume();
  },
]);
