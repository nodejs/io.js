// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan --deopt-every-n-times=0 --ignore-unhandled-promises

let log = [];
let asyncId = 0;

function logEvent (type, args) {
  const promise = args[0];
  promise.asyncId = promise.asyncId || ++asyncId;
  log.push({
    type,
    promise,
    parent: args[1],
    argsLength: args.length
  })
}
function initHook(...args) {
  logEvent('init', args);
}
function resolveHook(...args) {
  logEvent('resolve', args);
}
function beforeHook(...args) {
  logEvent('before', args);
}
function afterHook(...args) {
  logEvent('after', args);
}

function printLog(message) {
  console.log(` --- ${message} --- `)
  for (const event of log) {
    console.log(JSON.stringify(event))
  }
}

let has_promise_hooks = false;
try {
  d8.promise.setHooks();
  has_promise_hooks = true;
} catch {
  has_promise_hooks = false;
}

function assertNextEvent(type, args) {
  const [ promiseOrId, parentOrId ] = args;
  const nextEvent = log.shift();

  assertEquals(type, nextEvent.type);
  assertEquals(type === 'init' ? 2 : 1, nextEvent.argsLength);

  assertTrue(nextEvent.promise instanceof Promise);
  if (promiseOrId instanceof Promise) {
    assertEquals(promiseOrId, nextEvent.promise);
  } else {
    assertTrue(typeof promiseOrId === 'number');
    assertEquals(promiseOrId, nextEvent.promise?.asyncId);
  }

  if (parentOrId instanceof Promise) {
    assertEquals(parentOrId, nextEvent.parent);
    assertTrue(nextEvent.parent instanceof Promise);
  } else if (typeof parentOrId === 'number') {
    assertEquals(parentOrId, nextEvent.parent?.asyncId);
    assertTrue(nextEvent.parent instanceof Promise);
  } else {
    assertEquals(undefined, parentOrId);
    assertEquals(undefined, nextEvent.parent);
  }
}
function assertEmptyLog() {
  assertEquals(0, log.length);
  asyncId = 0;
  log = [];
}

// Verify basic log structure of different promise behaviours
function basicTest() {
  d8.promise.setHooks(initHook, beforeHook, afterHook, resolveHook);

  // `new Promise(...)` triggers init event with correct promise
  var done, p1 = new Promise(r => done = r);
  %PerformMicrotaskCheckpoint();
  assertNextEvent('init', [ p1 ]);
  assertEmptyLog();

  // `promise.then(...)` triggers init event with correct promise and parent
  var p2 = p1.then(() => { });
  %PerformMicrotaskCheckpoint();
  assertNextEvent('init', [ p2, p1 ]);
  assertEmptyLog();

  // `resolve(...)` triggers resolve event and any already attached continuations
  done();
  %PerformMicrotaskCheckpoint();
  assertNextEvent('resolve', [ p1 ]);
  assertNextEvent('before', [ p2 ]);
  assertNextEvent('resolve', [ p2 ]);
  assertNextEvent('after', [ p2 ]);
  assertEmptyLog();

  // `reject(...)` triggers the resolve event
  var done, p3 = new Promise((_, r) => done = r);
  done();
  %PerformMicrotaskCheckpoint();
  assertNextEvent('init', [ p3 ]);
  assertNextEvent('resolve', [ p3 ]);
  assertEmptyLog();

  // `promise.catch(...)` triggers init event with correct promise and parent
  // When the promise is already completed, the continuation should also run
  // immediately at the next checkpoint.
  var p4 = p3.catch(() => { });
  %PerformMicrotaskCheckpoint();
  assertNextEvent('init', [ p4, p3 ]);
  assertNextEvent('before', [ p4 ]);
  assertNextEvent('resolve', [ p4 ]);
  assertNextEvent('after', [ p4 ]);
  assertEmptyLog();

  // Detach hooks
  d8.promise.setHooks();
}

// Exceptions thrown in hook handlers should not raise or reject
function exceptions() {
  function thrower() {
    throw new Error('unexpected!');
  }

  // Init hook
  d8.promise.setHooks(thrower);
  assertDoesNotThrow(() => {
    Promise.resolve()
      .catch(assertUnreachable);
  });
  %PerformMicrotaskCheckpoint();
  d8.promise.setHooks();

  // Before hook
  d8.promise.setHooks(undefined, thrower);
  assertDoesNotThrow(() => {
    Promise.resolve()
      .then(() => {})
      .catch(assertUnreachable);
  });
  %PerformMicrotaskCheckpoint();
  d8.promise.setHooks();

  // After hook
  d8.promise.setHooks(undefined, undefined, thrower);
  assertDoesNotThrow(() => {
    Promise.resolve()
      .then(() => {})
      .catch(assertUnreachable);
  });
  %PerformMicrotaskCheckpoint();
  d8.promise.setHooks();

  // Resolve hook
  d8.promise.setHooks(undefined, undefined, undefined, thrower);
  assertDoesNotThrow(() => {
    Promise.resolve()
      .catch(assertUnreachable);
  });
  %PerformMicrotaskCheckpoint();
  d8.promise.setHooks();

  // Resolve hook for a reject
  d8.promise.setHooks(undefined, undefined, undefined, thrower);
  assertDoesNotThrow(() => {
    Promise.reject()
      .then(assertUnreachable)
      .catch();
  });
  %PerformMicrotaskCheckpoint();
  d8.promise.setHooks();
}

// For now, expect the optimizer to bail out on async functions
// when context promise hooks are attached.
function optimizerBailout(test, verify) {
  // Warm up test method
  %DeoptimizeFunction(test);
  %PrepareFunctionForOptimization(test);
  assertUnoptimized(test);
  test();
  test();
  test();
  %PerformMicrotaskCheckpoint();

  // Prove transition to optimized code when no hooks are present
  assertUnoptimized(test);
  %OptimizeFunctionOnNextCall(test);
  test();
  assertOptimized(test);
  %PerformMicrotaskCheckpoint();

  // Verify that attaching hooks deopts the async function
  d8.promise.setHooks(initHook, beforeHook, afterHook, resolveHook);
  %DeoptimizeFunction(test);
  assertUnoptimized(test);

  // Verify log structure of deoptimized call
  %PrepareFunctionForOptimization(test);
  test();
  %PerformMicrotaskCheckpoint();

  verify();

  // Optimize and verify log structure again
  %OptimizeFunctionOnNextCall(test);
  test();
  assertOptimized(test);
  %PerformMicrotaskCheckpoint();

  verify();

  d8.promise.setHooks();
}

function doTest () {
  optimizerBailout(async () => {
    await Promise.resolve();
  }, () => {
    assertNextEvent('init', [ 1 ]);
    assertNextEvent('init', [ 2 ]);
    assertNextEvent('resolve', [ 2 ]);
    assertNextEvent('init', [ 3, 2 ]);
    assertNextEvent('before', [ 3 ]);
    assertNextEvent('resolve', [ 1 ]);
    assertNextEvent('resolve', [ 3 ]);
    assertNextEvent('after', [ 3 ]);
    assertEmptyLog();
  });
  optimizerBailout(async () => {
    await Promise.reject();
  }, () => {
    assertNextEvent('init', [ 1 ]);
    assertNextEvent('init', [ 2 ]);
    assertNextEvent('resolve', [ 2 ]);
    assertNextEvent('init', [ 3, 2 ]);
    assertNextEvent('before', [ 3 ]);
    assertNextEvent('resolve', [ 1 ]);
    assertNextEvent('resolve', [ 3 ]);
    assertNextEvent('after', [ 3 ]);
    assertEmptyLog();
  });
  optimizerBailout(async () => {
    await { then (cb) { cb() } };
  }, () => {
    assertNextEvent('init', [ 1 ]);
    assertNextEvent('init', [ 2, 1 ]);
    assertNextEvent('init', [ 3, 2 ]);
    assertNextEvent('before', [ 2 ]);
    assertNextEvent('resolve', [ 2 ]);
    assertNextEvent('after', [ 2 ]);
    assertNextEvent('before', [ 3 ]);
    assertNextEvent('resolve', [ 1 ]);
    assertNextEvent('resolve', [ 3 ]);
    assertNextEvent('after', [ 3 ]);
    assertEmptyLog();
  });
  optimizerBailout(async () => {
    await { then (_, cb) { cb() } };
  }, () => {
    assertNextEvent('init', [ 1 ]);
    assertNextEvent('init', [ 2, 1 ]);
    assertNextEvent('init', [ 3, 2 ]);
    assertNextEvent('before', [ 2 ]);
    assertNextEvent('resolve', [ 2 ]);
    assertNextEvent('after', [ 2 ]);
    assertNextEvent('before', [ 3 ]);
    assertNextEvent('resolve', [ 1 ]);
    assertNextEvent('resolve', [ 3 ]);
    assertNextEvent('after', [ 3 ]);
    assertEmptyLog();
  });
  basicTest();
  exceptions();

  (function regress1126309() {
    function __f_16(test) {
      test();
      d8.promise.setHooks(undefined, () => {});
      %PerformMicrotaskCheckpoint();
      d8.promise.setHooks();
    }
    __f_16(async () => { await Promise.resolve()});
  })();

  (function boundFunction() {
    function hook() {};
    const bound = hook.bind(this);
    d8.promise.setHooks(bound, bound, bound, bound);
    Promise.resolve();
    Promise.reject();
    %PerformMicrotaskCheckpoint();
    d8.promise.setHooks();
  })();


  (function promiseAll() {
    let initCount = 0;
    d8.promise.setHooks(() => { initCount++});
    Promise.all([Promise.resolve(1)]);
    %PerformMicrotaskCheckpoint();
    assertEquals(initCount, 3);

    d8.promise.setHooks();
  })();

  (function overflow(){
    d8.promise.setHooks(() => { new Promise(()=>{}) });
    // Trigger overflow from JS code:
    Promise.all([Promise.resolve(1)]);
    %PerformMicrotaskCheckpoint();
    d8.promise.setHooks();
  });

}

if (has_promise_hooks) {
  doTest();
  d8.debugger.enable();
  doTest();
}
