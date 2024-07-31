// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
    InspectorTest.start('Regression test for crbug.com/1140845. Check that a "then" gettter on the object prototype does not crash V8');

const setupScript = `
  let obj = Object.prototype;
  obj.__defineGetter__('then', function() {console.log("foo")});
`;

(async function() {
  await Protocol.Debugger.enable();

  // Set a custom `then` method on the Object prototype. This should have no effect on replMode.
  await Protocol.Runtime.evaluate({
    expression: setupScript,
  });

  InspectorTest.log(`Evaluating a simple string 'foo' without side-effects should give us the string.`);
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: `"foo"`,
    replMode: true,
    throwOnSideEffect: true,
  }));

  InspectorTest.log(`Evaluating a simple string 'foo' with side-effets should give us the string.`);
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: `"foo"`,
    replMode: true,
  }));

  InspectorTest.completeTest();
})();
