// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-shadow-realm

// Utility function for testing that the expected strings occur
// in the stack trace produced when running the given function.
function assertTrace(name, e, expected, unexpected) {
  for (var i = 0; i < expected.length; i++) {
    assertTrue(e.stack.indexOf(expected[i]) !== -1,
               name + " doesn't contain expected[" + i + "] stack = " + e.stack);
  }
  if (unexpected) {
    for (var i = 0; i < unexpected.length; i++) {
      assertEquals(e.stack.indexOf(unexpected[i]), -1,
                   name + " contains unexpected[" + i + "]");
    }
  }
}

function testTrace(name, fun, expected, unexpected) {
  var threw = false;
  try {
    fun();
  } catch (e) {
    assertTrace(name, e, expected, unexpected);
    threw = true;
  }
  assertTrue(threw, name + " didn't throw");
}

function promiseTestTrace(name, fun, expected, unexpected) {
  var p = fun();
  assertPromiseResult(p.then(() => {
    assertUnreachable(name + " didn't throw");
  }, (e) => {
    assertTrace(name, e, expected, unexpected);
  }));
}

var shadowRealm = new ShadowRealm();

testTrace('ShadowRealm evaluate throw error', () => shadowRealm.evaluate(`
throw new Error('foo');
`), [
  "Error: foo",
  "at eval",
  "at ShadowRealm.evaluate",
]);

testTrace('ShadowRealm evaluate throw string', () => shadowRealm.evaluate(`
throw 'foo';
`), [
  // No inner stack trace, only the stacktrace at the realm boundary.
  "TypeError: ShadowRealm evaluate threw (foo)",
  "at ShadowRealm.evaluate",
], [
  // No inner stack trace.
  "at eval",
]);

testTrace('ShadowRealm evaluate throw string', () => {
  var wrappedFunction = shadowRealm.evaluate(`
function myFunc() {
  throw new Error('foo');
}
myFunc;
`);
  wrappedFunction();
}, [
  "Error: foo",
  "at myFunc",
]);

promiseTestTrace(
  'ShadowRealm importValue throw error',
  () => shadowRealm.importValue('./shadowrealm-skip-2-throw.mjs', 'foo'),
  [
    "Error: foobar",
    "at myFunc",
  ]);

promiseTestTrace(
  'ShadowRealm importValue throw object',
  () => shadowRealm.importValue('./shadowrealm-skip-3-throw-object.mjs', 'foo'),
  [
    "TypeError: Cannot import in ShadowRealm ([object Object])",
  ], [
    // No inner stack trace.
    "at myFunc",
  ]);

promiseTestTrace(
  'ShadowRealm importValue throw string',
  () => shadowRealm.importValue('./shadowrealm-skip-4-throw-string.mjs', 'foo'),
  [
    "TypeError: Cannot import in ShadowRealm (foobar)",
  ], [
    // No inner stack trace.
    "at myFunc",
  ]);
