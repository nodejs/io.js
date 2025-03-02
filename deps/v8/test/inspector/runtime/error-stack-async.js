// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Checks that Error objects capture the correct async continuation');

contextGroup.addScript(`
async function generateError() {
  await 1;
  return new Error();
}

async function foo() {
  return await generateError();
}

async function bar() {
  const error = await foo();
  throw error;
}
//# sourceURL=test.js
`);

InspectorTest.runAsyncTestSuite([
  async function testErrorStackWithRuntimeEnabled() {
    await Promise.all([
      Protocol.Runtime.enable(),
      Protocol.Runtime.setAsyncCallStackDepth({maxDepth: 10}),
    ]);
    InspectorTest.logMessage(await Protocol.Runtime.evaluate(
        {awaitPromise: true, expression: 'bar()'}));
    await Protocol.Runtime.disable();
  },
]);
