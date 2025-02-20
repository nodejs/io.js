// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

const {Protocol} =
    InspectorTest.start('Check that `using` and `await using` do not work in debug evaluate.');

(async function testExplicitResourceManagement() {
  await Protocol.Debugger.enable();
  await Protocol.Runtime.enable();
  const promise = Protocol.Debugger.oncePaused();
  Protocol.Runtime.evaluate({ expression: 'debugger;' });

  const { params: { callFrames: [{ callFrameId, functionLocation: { scriptId } }] } } = await promise;

  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({
    callFrameId,
    expression: `
    using x = {
        value: 1,
        [Symbol.dispose]() {
            return 42;
      }
      };
    `
  }));

  InspectorTest.logMessage(await Protocol.Debugger.evaluateOnCallFrame({
    callFrameId,
    expression: `
    await using y = {
      value: 2,
      [Symbol.asyncDispose]() {
          return 43;
    }
    };
    `
  }));

  Protocol.Debugger.resume();
  InspectorTest.completeTest();
})();
