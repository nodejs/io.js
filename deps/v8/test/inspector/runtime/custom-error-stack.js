// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} =
    InspectorTest.start('Checks that error.stack works correctly for custom Error subclasses');

contextGroup.addScript(`
function recurse(f, n) {
  if (n-- > 0) return recurse(f, n);
  return f();
}

function foo(f) {
  recurse(f, 5);
}

class MyError extends Error {
  get message() { return 'custom message'; }
}

class ErrorWithCustomName extends Error {
  name = "NamedError";
}

//# sourceURL=test.js
`);

(async () => {
  Protocol.Runtime.onConsoleAPICalled(({ params }) => {
    InspectorTest.logMessage(params.args[0]);
  });
  await Protocol.Runtime.enable();

  InspectorTest.logMessage(
    (await Protocol.Runtime.evaluate({ expression: 'foo(() => {throw new MyError;})' })).result.exceptionDetails.exception);
  InspectorTest.logMessage(
    (await Protocol.Runtime.evaluate({ expression: 'foo(() => {throw new MyError("foo");})' })).result.exceptionDetails.exception);
  InspectorTest.logMessage(
    (await Protocol.Runtime.evaluate({ expression: 'foo(() => {throw new ErrorWithCustomName("bar");})' })).result.exceptionDetails.exception);

  await Protocol.Runtime.evaluate({ expression: 'foo(() => console.log(new MyError))' });
  await Protocol.Runtime.evaluate({ expression: 'foo(() => console.log(new MyError("foo")))' });
  await Protocol.Runtime.evaluate({ expression: 'foo(() => console.log(new ErrorWithCustomName("bar")))' });

  InspectorTest.completeTest();
})();
