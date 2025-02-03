// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Test for default label with console.time() and friends');

(async function test() {
  await Protocol.Runtime.enable();

  utils.setCurrentTimeMSForTest(0.0);
  await Protocol.Runtime.evaluate({expression: `console.time(undefined)`});
  utils.setCurrentTimeMSForTest(1.0);
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.timeLog(undefined, 'a')`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);
  utils.setCurrentTimeMSForTest(2.0);
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.timeEnd(undefined)`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);

  utils.setCurrentTimeMSForTest(3.0);
  await Protocol.Runtime.evaluate({expression: `console.time()`});
  utils.setCurrentTimeMSForTest(4.0);
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.timeLog()`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);
  utils.setCurrentTimeMSForTest(5.0);
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.timeEnd()`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);

  await Protocol.Runtime.disable();
  InspectorTest.completeTest();
})()

function logArgs(message) {
  InspectorTest.logMessage(message.params.args);
}
