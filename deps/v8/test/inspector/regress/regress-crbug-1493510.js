// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, Protocol} =
    InspectorTest.start('Checks that wrong restart "mode" doesn\'t crash the inspector');

session.setupScriptMap();

const source = `
function foo() {
  debugger;
}
foo();
//# sourceURL=testRestartFrame.js`;

(async () => {
  await Protocol.Debugger.enable();

  const { callFrames } = await InspectorTest.evaluateAndWaitForPause(source);
  const response = await Protocol.Debugger.restartFrame({ callFrameId: callFrames[0].callFrameId, mode: 'StepIn@to' });
  InspectorTest.logMessage(response);
  InspectorTest.completeTest();
})();
