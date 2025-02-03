// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Checks that console.timeEnd() always attached exactly one stack frame');

Protocol.Runtime.enable();

InspectorTest.runAsyncTestSuite([
  function top() {
    return checkStack(1);
  },
  function deep() {
    return checkStack(100);
  },
]);

async function checkStack(depth) {
  await Promise.all([
    Protocol.Runtime.onceConsoleAPICalled().then(({params}) => {
      InspectorTest.log('Stack depth: ' + params.stackTrace.callFrames.length);
    }),
    Protocol.Runtime.evaluate({
      expression: `
  console.time('foo'),
  (function recurse(depth) {
    if (depth <= 1) {
      console.timeEnd('foo');
    } else {
      recurse(depth - 1);
    }
  })(${depth})
`
    }),
  ]);
}
