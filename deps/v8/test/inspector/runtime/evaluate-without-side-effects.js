// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start("Tests that Runtime.evaluate can run without side effects.");

session.setupScriptMap();
contextGroup.addScript(`
function f() {
  return 2;
}

class Foo {#bar = 1}
const foo = new Foo;
//# sourceURL=test.js`);
Protocol.Runtime.enable();
Protocol.Debugger.enable();

Protocol.Debugger.onPaused(message => {
  InspectorTest.log("paused");
  Protocol.Debugger.resume();
});

(async function() {
  InspectorTest.log("Test throwOnSideEffect: false");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "var x = 2; x;",
    throwOnSideEffect: false
  }));

  InspectorTest.log("Test prototype extension expression with side-effect, with throwOnSideEffect: true");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "f.prototype.test = () => console.log('test fn');",
    throwOnSideEffect: true
  }));

  InspectorTest.log("Test expression with side-effect, with throwOnSideEffect: true");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "x = 3; x;",
    throwOnSideEffect: true
  }));

  InspectorTest.log("Test expression without side-effect, with throwOnSideEffect: true");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "x * 2",
    throwOnSideEffect: true
  }));

  InspectorTest.log("Test that debug break triggers without throwOnSideEffect");
  await Protocol.Debugger.setBreakpointByUrl({ url: 'test.js', lineNumber: 2 });
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "f()",
    throwOnSideEffect: false
  }));

  InspectorTest.log("Test that debug break does not trigger with throwOnSideEffect");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "f()",
    throwOnSideEffect: true
  }));

  InspectorTest.log("Test that private member access does not trigger with replMode and throwOnSideEffect");
  InspectorTest.logMessage(await Protocol.Runtime.evaluate({
    expression: "foo.#bar",
    throwOnSideEffect: true,
    replMode: true,
  }));

  InspectorTest.completeTest();
})();
