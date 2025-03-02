// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Checks API for ignoring execution contexts and anonymous scripts');

function testFunc() {
  debugger;
  console.log('');
  console.log('');
}

Protocol.Debugger.enable();
Protocol.Runtime.enable();
Protocol.Debugger.onPaused(message => {
  InspectorTest.log(
      `Paused on line ${message.params.callFrames[0].location.lineNumber + 1}`);
  Protocol.Debugger.stepOver();
});

InspectorTest.runAsyncTestSuite([
  async function testNoIgnore() {
    await test(false, false);
  },
  async function testIgnoreAnonymous() {
    await test(true, false);
  },
  async function testIgnoreExecutionContext() {
    await test(false, true);
  },
]);

async function test(ignoreAnonymous, ignoreExecutionContext) {
  const blackboxPatterns = ignoreAnonymous ?
      {patterns: [], skipAnonymous: true} :
      {patterns: ['hidden.js']};
  InspectorTest.log(
      'Setting blackbox patterns: ' + JSON.stringify(blackboxPatterns));
  await Protocol.Debugger.setBlackboxPatterns(blackboxPatterns);

  contextGroup.createContext();
  const contextId = (await Protocol.Runtime.onceExecutionContextCreated())
                        .params.context.uniqueId;
  const blackboxExecutionContextId =
      ignoreExecutionContext ? contextId : 'different-id-from-' + contextId;
  InspectorTest.log(`Setting blackbox execution context to ${
      ignoreExecutionContext ? 'same' : 'different'} id`);
  await Protocol.Debugger.setBlackboxExecutionContexts(
      {uniqueIds: [blackboxExecutionContextId]});
  const result = await Protocol.Runtime.evaluate({
    uniqueContextId: contextId,
    returnByValue: true,
    expression: testFunc.toString() + '\ntestFunc();'
  });
  InspectorTest.log(`Exited ${result.result ? 'normally' : 'with exception'}`);
}
