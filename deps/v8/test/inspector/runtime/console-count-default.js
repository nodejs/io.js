// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Test for default label with console.count() and console.countReset()');

(async function test() {
  await Protocol.Runtime.enable();

  await Protocol.Runtime.evaluate({expression: `console.count(undefined)`});
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.count(undefined)`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);
  await Protocol.Runtime.evaluate(
      {expression: `console.countReset(undefined)`});
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.count(undefined)`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);
  await Protocol.Runtime.evaluate(
      {expression: `console.countReset(undefined)`});

  await Protocol.Runtime.evaluate({expression: `console.count()`});
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.count()`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);
  await Protocol.Runtime.evaluate({expression: `console.countReset()`});
  await Promise.all([
    Protocol.Runtime.evaluate({expression: `console.count()`}),
    Protocol.Runtime.onceConsoleAPICalled().then(logArgs),
  ]);
  await Protocol.Runtime.evaluate({expression: `console.countReset()`});

  await Protocol.Runtime.disable();
  InspectorTest.completeTest();
})()

function logArgs(message) {
  InspectorTest.logMessage(message.params.args);
}
