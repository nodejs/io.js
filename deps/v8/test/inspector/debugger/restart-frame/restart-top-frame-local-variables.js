// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, Protocol} =
    InspectorTest.start('Checks that after restarting the top frame, local variables are reset');

session.setupScriptMap();

const source = `
function foo() {
  var x = 'some var';
  const y = 'some const';
  let z = 'some let';
  debugger;
}
foo();
//# sourceURL=testRestartFrame.js`;

(async () => {
  await Protocol.Debugger.enable();

  const { callFrames } = await InspectorTest.evaluateAndWaitForPause(source);

  let { callFrameId } = callFrames[0];
  InspectorTest.log('Evaluating x:');
  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({ callFrameId, expression: 'x' }));

  InspectorTest.log('Evaluating y:');
  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({ callFrameId, expression: 'y' }));

  InspectorTest.log('Evaluating z:');
  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({ callFrameId, expression: 'z' }));

  const callFramesAfter = await InspectorTest.restartFrameAndWaitForPause(callFrames, 0);

  ({ callFrameId } = callFramesAfter[0]);
  InspectorTest.log('Evaluating x:');
  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({ callFrameId, expression: 'x' }));

  InspectorTest.log('Evaluating y:');
  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({ callFrameId, expression: 'y' }));

  InspectorTest.log('Evaluating z:');
  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({ callFrameId, expression: 'z' }));

  Protocol.Debugger.resume();  // Resuming hits the 'debugger' stmt again.
  await Protocol.Debugger.oncePaused();
  await Protocol.Debugger.resume();

  InspectorTest.completeTest();
})();
