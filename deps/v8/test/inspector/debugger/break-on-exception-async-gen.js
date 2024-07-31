// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Check that "break on exceptions" works for async generators.');

contextGroup.addScript(`
async function* simpleThrow() {
  throw new Error();
}

async function* yieldBeforeThrow() {
  yield 1;
  throw new Error();
}

async function* awaitBeforeThrow() {
  await 1;
  throw new Error();
}

async function* yieldBeforeThrowWithAwait() {
  await 1;
  yield 2;
  throw new Error();
}

async function* awaitBeforeThrowWithYield() {
  yield 1;
  await 2;
  throw new Error();
}

async function* yieldThrows() {
  yield 1;
  yield thrower();
}

async function* yieldThrowsCaughtInGenerator() {
  try {
    yield 1;
    yield thrower();
  } catch {
    throw new Error();
  }
}

async function* awaitThrows() {
  yield 1;
  await thrower();
}

async function runGenWithCatch(gen) {
  try {
    for await (const val of gen());
  } catch {}
}

async function runGenWithoutCatch(gen) {
  for await (const val of gen());
}

async function injectExceptionIntoGen(gen) {
  const g = gen();
  await gen.next();
  await gen.throw(new Error());
}

async function injectExceptionIntoGenWithCatch(gen) {
  try {
    const g = gen();
    await gen.next();
    await gen.throw(new Error());
  } catch {}
}

async function thrower() {
  await 1;  // Suspend once.
  throw new Error();
}`);

Protocol.Debugger.onPaused(async ({ params: { data }}) => {
  const caughtOrUncaught = data.uncaught ? 'UNCAUGHT' : 'CAUGHT';
  InspectorTest.log(`Paused for ${caughtOrUncaught} exception.`);
  await Protocol.Debugger.resume();
});

async function runTest(expression) {
  await Promise.all([
    Protocol.Debugger.enable(),
    Protocol.Debugger.setPauseOnExceptions({state: 'all'}),
  ]);

  await Protocol.Runtime.evaluate({ expression, awaitPromise: true });
  await Protocol.Debugger.disable();
}

InspectorTest.runAsyncTestSuite([
  async function testSimpleGeneratorThrowCaught() {
    await runTest('runGenWithCatch(simpleThrow)');
  },
  async function testSimpleGeneratorThrowUncaught() {
    await runTest('runGenWithoutCatch(simpleThrow)');
  },
  async function testYieldBeforeThrowCaught() {
    await runTest('runGenWithCatch(yieldBeforeThrow)');
  },
  async function testYieldBeforeThrowUncaught() {
    await runTest('runGenWithoutCatch(yieldBeforeThrow)');
  },
  async function testAwaitBeforeThrowCaught() {
    await runTest('runGenWithCatch(awaitBeforeThrow)');
  },
  async function testAwaitBeforeThrowUncaught() {
    await runTest('runGenWithoutCatch(awaitBeforeThrow)');
  },
  async function testYieldBeforeThrowWithAwaitCaught() {
    await runTest('runGenWithCatch(yieldBeforeThrowWithAwait)');
  },
  async function testYieldBeforeThrowWithAwaitUncaught() {
    await runTest('runGenWithoutCatch(yieldBeforeThrowWithAwait)');
  },
  async function testAwaitBeforeThrowWithYieldCaught() {
    await runTest('runGenWithCatch(awaitBeforeThrowWithYield)');
  },
  async function testAwaitBeforeThrowWithYieldUncaught() {
    await runTest('runGenWithoutCatch(awaitBeforeThrowWithYield)');
  },
  async function testYieldThrowsCaught() {
    await runTest('runGenWithCatch(yieldThrows)');
  },
  async function testYieldThrowsUncaught() {
    await runTest('runGenWithoutCatch(yieldThrows)');
  },
  async function testYieldThrowsCaughtInGenerator() {
    await runTest('runGenWithoutCatch(yieldThrowsCaughtInGenerator)');
  },
  async function testAwaitThrowsCaught() {
    await runTest('runGenWithCatch(awaitThrows)');
  },
  async function testAwaitThrowsUncaught() {
    await runTest('runGenWithoutCatch(awaitThrows)');
  },
  async function testYieldThrowMethodCaught() {
    await runTest('injectExceptionIntoGenWithCatch(yieldThrows)');
  },
  async function testYieldThrowMethodUncaught() {
    await runTest('injectExceptionIntoGen(yieldThrows)');
  },
  async function testYieldThrowMethodCaughtInGenerator() {
    await runTest('injectExceptionIntoGen(yieldThrowsCaughtInGenerator)');
  },
]);
