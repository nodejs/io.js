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
  return assertPromiseResult(p.then(() => {
    assertUnreachable(name + " didn't throw");
  }, (e) => {
    assertTrace(name, e, expected, unexpected);
  }));
}

var shadowRealm = new ShadowRealm();
Error.prepareStackTrace = (error) => {
  assertTrue(error instanceof globalThis.Error);
  return error.message + ' (outside realm)';
}

{
  let shadowRealm = new ShadowRealm();
  shadowRealm.evaluate(`
    globalThis.prepareStackTraceCount = 0;
    globalThis.notRealmErrorCount = 0;
    Error.prepareStackTrace = (error) => {
      globalThis.prepareStackTraceCount++;
      if (!(error instanceof Error)) {
        globalThis.notRealmErrorCount++;
      }
      return error.message + ' (shadow realm)';
    }
  `);
  testTrace('ShadowRealm prepareStackTrace parameters', () => shadowRealm.evaluate(`
    throw new Error('foo');
  `), [
    "foo (shadow realm)",
  ]);
  assertEquals(1, shadowRealm.evaluate(`globalThis.prepareStackTraceCount`));
  assertEquals(0, shadowRealm.evaluate(`globalThis.notRealmErrorCount`));
}

{
  let shadowRealm = new ShadowRealm();
  shadowRealm.evaluate(`
    globalThis.prepareStackTraceCount = 0;
    Error.prepareStackTrace = (error) => {
      globalThis.prepareStackTraceCount++;
      return error.message + ' (shadow realm)';
    }
  `);
  testTrace('ShadowRealm error with initialized stack property', () => shadowRealm.evaluate(`
    const error = new Error('foo');
    // Initialize the stack property.
    error.stack;
    throw error;
  `), [
    "foo (shadow realm)",
  ]);
  assertEquals(1, shadowRealm.evaluate(`globalThis.prepareStackTraceCount`));
}

{
  let shadowRealm = new ShadowRealm();
  shadowRealm.evaluate(`
    globalThis.prepareStackTraceCount = 0;
    Error.prepareStackTrace = (error) => {
      globalThis.prepareStackTraceCount++;
      return error.message + ' (shadow realm)';
    }
  `);
  testTrace('ShadowRealm error with string stack property', () => shadowRealm.evaluate(`
    const error = new Error('foo');
    error.stack = 'bar';
    throw error;
  `), [
    "bar",
  ]);
  assertEquals(0, shadowRealm.evaluate(`globalThis.prepareStackTraceCount`));
}

{
  let shadowRealm = new ShadowRealm();
  testTrace('ShadowRealm prepareStackTrace return arbitrary objects', () => shadowRealm.evaluate(`
    globalThis.prepareStackTraceCount = 0;
    Error.prepareStackTrace = (error) => {
      globalThis.prepareStackTraceCount++;
      return { arbitrary: 'object' };
    }

    throw new Error('foo');
  `), [
    "Error stack is not a string in ShadowRealm ([object Object]) (outside realm)",
  ]);
  assertEquals(1, shadowRealm.evaluate(`globalThis.prepareStackTraceCount`));
}

testTrace('ShadowRealm prepareStackTrace throws', () => new ShadowRealm().evaluate(`
  Error.prepareStackTrace = (error) => {
    throw 'malformed prepare stack trace';
  }

  throw new Error('foo');
`), [
  "Error stack getter threw in ShadowRealm (malformed prepare stack trace) (outside realm)",
]);

testTrace('ShadowRealm evaluate throw string', () => new ShadowRealm().evaluate(`
  throw 'foo';
`), [
  // No inner stack trace, only the stacktrace at the realm boundary.
  "ShadowRealm evaluate threw (foo) (outside realm)",
]);

{
  let shadowRealm = new ShadowRealm();
  shadowRealm.evaluate(`
    globalThis.prepareStackTraceCount = 0;
    Error.prepareStackTrace = (error) => {
      globalThis.prepareStackTraceCount++;
      return error.message + ' (shadow realm)';
    }
  `);
  let fn = shadowRealm.evaluate(`
    function myFunc() {
      throw new Error('foo');
    }
    myFunc;
  `);
  testTrace('WrappedFunction error stack', () => fn(), [
    "foo (shadow realm)",
  ]);
  assertEquals(1, shadowRealm.evaluate(`globalThis.prepareStackTraceCount`));
}

{
  let shadowRealm = new ShadowRealm();
  shadowRealm.evaluate(`
    globalThis.prepareStackTraceCount = 0;
    Error.prepareStackTrace = (error) => {
      globalThis.prepareStackTraceCount++;
      return error.message + ' (shadow realm)';
    }
  `);
  promiseTestTrace(
    'ShadowRealm importValue error stack',
    () => shadowRealm.importValue('./shadowrealm-skip-2-throw.mjs', 'foo')
      .finally(() => {
        assertEquals(1, shadowRealm.evaluate(`globalThis.prepareStackTraceCount`));
      }),
    [
      "foobar (shadow realm)",
    ]);
}
