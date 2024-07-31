// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Checks when promise rejections are predicted to be caught.');

// catch-prediction.js

function rejectAfterDelayInPromiseConstructor() {
  return new Promise((pass, reject) => setTimeout(() => reject(new Error('fail')), 0));
}

function promiseReject() {
  return Promise.reject(new Error('fail'));
}

function throwInPromiseConstructor() {
  return new Promise(() => {throw new Error('fail');});
}

function rejectInPromiseConstructor() {
  return new Promise((pass, reject) => {
    reject(new Error('fail'));
    console.log('after reject');
  });
}

function rejectAfterDelayInTryInPromiseConstructor() {
  return new Promise((pass, reject) => setTimeout(() => {
    try {
      reject(new Error('fail'));
    } catch(e) {
      console.log('fake');  // Won't be hit
    }
  }, 0));
}

function rejectBindAfterDelay() {
  // Unbreakable location because there will be nothing on the callstack
  return new Promise((pass, reject) => setTimeout(reject.bind(null, new Error('fail')), 0));
}

async function throwFromAsync() {
  throw new Error('fail');
}

async function throwFromAsyncAfterDelay() {
  await delay();
  throw new Error('fail');
}

function customRejectingThenable() {
  // JS supports awaiting on user defined promise-like objects
  return {
    then(onFulfilled, onRejected) {
      onRejected(new Error('fail'));
    }
  }
}

function customRejectingThenableAfterDelay() {
  // JS supports awaiting on user defined promise-like objects
  return {
    then(onFulfilled, onRejected) {
      setTimeout(() => onRejected(new Error('fail')), 0);
    }
  }
}

function exceptionInThen() {
  return Promise.resolve().then(() => {throw new Error('fail');});
}

function exceptionInCatch() {
  return Promise.reject(new Error('will catch')).catch(() => {
    console.log('throwing again');
    throw new Error('fail');
  });
}

async function exceptionInAsyncGenerator() {
  async function* gen() {
    yield 1;
    throw new Error('fail');
  }
  for await(const x of gen()) {}
}

async function rejectInGenerator() {
  function* gen() {
    yield promiseReject();
  }
  for await(const x of gen()) {}
}

async function rejectAfterDelayInGenerator() {
  function* gen() {
    yield rejectAfterDelayInPromiseConstructor();
  }
  for await(const x of gen()) {}
}

async function rejectInAsyncGenerator() {
  async function* gen() {
    yield promiseReject();
  }
  for await(const x of gen()) {}
}

async function rejectAfterDelayInAsyncGenerator() {
  async function* gen() {
    yield rejectAfterDelayInPromiseConstructor();
  }
  for await(const x of gen()) {}
}

async function asyncGeneratorThrowMethod() {
  async function* gen() {
    yield 1;
  }
  const g = gen();
  await g.next();
  await g.throw(new Error('fail'));
}

async function asyncCatchingGeneratorThrowMethod() {
  async function* gen() {
    try {
      yield 1;
    } catch (e) {
      console.log('caught by generator');
    }
  }
  const g = gen();
  await g.next();
  await g.throw(new Error('fail'));
}

async function throwInAsyncGenerator() {
  async function* gen() {
    yield 1;
    throw new Error('fail');
  }
  for await(const x of gen()) {}
}

function promiseRejectInTry() {
  // A synchronous try can't catch a promise rejection.
  try {
    return Promise.reject(new Error('fail'));
  } catch (e) {
    console.log('fake');  // Will not catch
  }
}

function rejectAfterFulfillInPromiseConstructor() {
  return new Promise((pass, reject) => {
    pass('ok');
    console.log('after pass');
    reject(new Error('fail'));
    console.log('after reject');
  });
}

function rejectAfterDelayAfterFulfillInPromiseConstructor() {
  return new Promise((pass, reject) => {
    pass('ok');
    setTimeout(() => reject(new Error('fail')), 0);
  });
}

function throwAfterFulfillInPromiseConstructor() {
  return new Promise((pass, reject) => {
    pass('ok');
    console.log('after pass');
    throw new Error('fail');
  });
}

async function dontHandleAsync(fn) {
  // Unhandled exception that is trivial to predict,
  // for comparison.
  await fn();
}

async function awaitAndCreateInTry(fn) {
  try {
    // The easy case to get right, for comparison.
    await fn();
  } catch(e) {
    console.log('caught');
  }
}

function catchMethod(fn) {
  // Catch handler may not be attached at the time promise is rejected
  fn().catch((e) => console.log('caught'));
}

function finallyMethod(fn) {
  // Is a finally handler considered caught?
  fn().finally(() => console.log('finally handler'));
}

function catchMethodInTry(fn) {
  try {
    fn().catch((e) => console.log('caught'));
  } catch(e) {
    console.log('fake');  // Won't be hit
  }
}

async function awaitInTry(fn) {
  // No handler when promise is created, but there is when it is awaited
  const promise = fn();
  try {
    await promise;
  } catch(e) {
    console.log('caught');
  }
}

async function awaitAndCreateInTryFinally(fn) {
  try {
    await fn();
  } finally {
    // Is finally handler treated as a catch?
    console.log('finally handler');
  }
}

async function createInTry(fn) {
  let promise = null;
  // In this test case we don't handle the exception,
  // but first we try to convince JS we will.
  try {
    promise = fn();
  } catch(e) {
    console.log('fake');  // Won't be hit
  }

  if(promise) {
    await promise;
  }
}

function fireAndForgetInTry(fn) {
  try {
    // Unhandled exception, but will we mispredict as caught?
    fn();
  } catch(e) {
    console.log('fake');  // Won't be hit
  }
}

function finallyAndCatchMethod(fn) {
  // Will we account for the catch method after the finally?
  fn().finally(() => console.log('finally handler'))
      .catch(() => console.log('caught'));
}

function thenMethod(fn) {
  fn().then(() => console.log('not hit'));
}

function thenMethodWithTwoArgs(fn) {
  fn().then(() => console.log('not hit'), () => console.log('caught'));
}

async function caughtPromiseRace(fn) {
  try {
    await Promise.race([fn()]);
  } catch(e) {
    console.log('caught');
  }
}

async function caughtInTryPromiseRace(fn) {
  const promise = fn();
  try {
    await Promise.race([promise]);
  } catch(e) {
    console.log('caught');
  }
}

async function uncaughtPromiseRace(fn) {
  await Promise.race([fn()]);
}

async function ignoredPromiseRace(fn) {
  // Race will resolve to first promise, preventing exception from second
  // from propagating. Is this caught or uncaught?
  await Promise.race([Promise.resolve(), fn()]);
}

async function ignoredPromiseRaceInTry(fn) {
  try {
    // Race will resolve to first promise, preventing exception from second
    // from propagating. Is this caught or uncaught?
    await Promise.race([Promise.resolve(), fn()]);
  } catch(e) {
    console.log('caught');
  }
}

async function caughtPromiseAll(fn) {
  try {
    await Promise.all([fn()]);
  } catch(e) {
    console.log('caught');
  }
}

async function uncaughtPromiseAll(fn) {
  await Promise.all([fn()]);
}

async function caughtPromiseAny(fn) {
  try {
    await Promise.any([fn()]);
  } catch(e) {
    console.log('caught');
  }
}

async function uncaughtPromiseAny(fn) {
  await Promise.any([fn()]);
}

async function ignoredPromiseAny(fn) {
  // Any will resolve to first promise, preventing exception from second
  // from propagating. Is this caught or uncaught?
  await Promise.any([Promise.resolve(), fn()]);
}

async function ignoredPromiseAnyInTry(fn) {
  try {
    // Any will resolve to first promise, preventing exception from second
    // from propagating. Is this caught or uncaught?
    await Promise.any([Promise.resolve(), fn()]);
  } catch(e) {
    console.log('caught');
  }
}

async function ignoredPromiseAnyWithIndirection(fn) {
  // Similar to other ignored cases but we have to walk the promise tree to
  // find the already fulfilled promise.
  async function indirectThrow() {
    await fn();
  }
  async function indirectAny() {
    await Promise.any([Promise.resolve(), indirectThrow()]);
  }
  await indirectAny();
}

async function ignoredPromiseAnyWithIndirectionInTry(fn) {
  async function indirectThrow() {
    await fn();
  }

  try {
    // Similar to other ignored cases but we have to walk the promise tree to
    // find the already fulfilled promise.
    await Promise.any([Promise.resolve(), indirectThrow()]);
  } catch(e) {
    console.log('caught');
  }
}

async function awaitAfterDelayInTry(fn) {
  try {
    // Predicts correctly if thrown immediately but if not no handler will
    // be attached when it throws.
    const promise = fn();
    console.log('sleeping');
    await delay();
    await delay();
    console.log('awaiting');
    await promise;
  } catch(e) {
    console.log('caught');
  }
}

async function catchInAsyncGenerator(fn) {
  async function* gen() {
    try {
      yield fn();
    } catch (e) {
      console.log('caught');
    }
  }
  for await(const x of gen()) {}
}

async function catchInGenerator(fn) {
  function* gen() {
    try {
      yield fn();
    } catch (e) {
      console.log('fake');  // Will not catch
    }
  }
  for await(const x of gen()) {}
}

async function catchAndAlsoDont(fn) {
  const uncaughtPromise = fn();
  const caughtPromise = uncaughtPromise.catch(() => console.log('caught'));
  await uncaughtPromise;
  console.log('never reached');
}

function delay() {
  return new Promise(resolve => setTimeout(resolve, 0));
}

async function testWrapper(throwFunc, handleFunc) {
  // testWrapper runs testFunc and completes successfully when testFunc
  // completes, but does so in a setTimeout so that it doesn't catch any
  // exceptions thrown by testFunc.
  async function runWithResolution(resolve) {
    try {
      const promise = handleFunc(throwFunc);
      console.log('awaiting promise');
      await promise;
      console.log('finished without error');
    } finally {
      // Leave exceptions uncaught, but the testWrapper continues normally
      // anyway. Needs to be in a setTimeout so that uncaught exceptions
      // are handled before the test is complete.
      setTimeout(resolve, 0);
    }
  }
  await new Promise(resolve => setTimeout(runWithResolution.bind(null, resolve), 0));
}

// -------------------------------------

// Order of functions should match above so that line numbers match
const basicThrowFunctions = [rejectAfterDelayInPromiseConstructor, promiseReject];
const advancedThrowFunctions = [
    throwInPromiseConstructor,
    rejectInPromiseConstructor,
    rejectAfterDelayInTryInPromiseConstructor,
    rejectBindAfterDelay,
    throwFromAsync,
    throwFromAsyncAfterDelay,
    customRejectingThenable,
    customRejectingThenableAfterDelay,
    exceptionInThen,
    exceptionInCatch,
    exceptionInAsyncGenerator,
    rejectInGenerator,
    rejectAfterDelayInGenerator,
    rejectInAsyncGenerator,
    rejectAfterDelayInAsyncGenerator,
    asyncGeneratorThrowMethod,
    asyncCatchingGeneratorThrowMethod,
    throwInAsyncGenerator,
    promiseRejectInTry,
    rejectAfterFulfillInPromiseConstructor,
    rejectAfterDelayAfterFulfillInPromiseConstructor,
    throwAfterFulfillInPromiseConstructor,
];
const basicCatchFunctions = [dontHandleAsync, awaitAndCreateInTry];
const advancedCatchFunctions = [
    catchMethod,
    finallyMethod,
    catchMethodInTry,
    awaitInTry,
    awaitAndCreateInTryFinally,
    createInTry,
    fireAndForgetInTry,
    finallyAndCatchMethod,
    thenMethod,
    thenMethodWithTwoArgs,
    caughtPromiseRace,
    caughtInTryPromiseRace,
    uncaughtPromiseRace,
    ignoredPromiseRace,
    ignoredPromiseRaceInTry,
    caughtPromiseAll,
    uncaughtPromiseAll,
    caughtPromiseAny,
    uncaughtPromiseAny,
    ignoredPromiseAny,
    ignoredPromiseAnyInTry,
    ignoredPromiseAnyWithIndirection,
    ignoredPromiseAnyWithIndirectionInTry,
    awaitAfterDelayInTry,
    catchInAsyncGenerator,
    catchInGenerator,
    catchAndAlsoDont,
];
const helpers = [delay, testWrapper];

const file = [...basicThrowFunctions, ...advancedThrowFunctions, ...basicCatchFunctions, ...advancedCatchFunctions, ...helpers].join('\n\n');
const startLine = 9;  // Should match first line of first function
contextGroup.addScript(file, startLine, 0, 'catch-prediction.js');
session.setupScriptMap();

let pauseCount = 0
let uncaught = false;
let predictedUncaught = null;
let exceptionDetails = null;
let exceptionStackTrace = null;
let exceptionId = null;

function lazyLogException() {
  if (exceptionDetails || exceptionStackTrace) {
    InspectorTest.log(`Uncaught exception: ${exceptionDetails}`);
    session.logAsyncStackTrace(exceptionStackTrace);
  }
  exceptionDetails = null;
  exceptionStackTrace = null;
}

function uncaughtString(uncaught) {
  return ((uncaught && 'uncaught') ?? '(undefined)') || 'caught';
}

Protocol.Debugger.onPaused(message => {
  lazyLogException();
  predictedUncaught = message.params.data?.uncaught;
  InspectorTest.log(`Paused on ${uncaughtString(predictedUncaught)} ${message.params.reason}`);
  session.logCallFrames(message.params.callFrames);
  session.logAsyncStackTrace(message.params.asyncStackTrace);
  InspectorTest.log('');
  pauseCount++;
  Protocol.Debugger.resume();
});

Protocol.Console.onMessageAdded(event => {
  lazyLogException();
  InspectorTest.log('console: ' + event.params.message.text);
});
Protocol.Debugger.enable();
Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 6});
Protocol.Console.enable();
Protocol.Runtime.onExceptionRevoked(event => {
  if (!uncaught) {
    InspectorTest.log('ERROR: Revoked exception when none thrown');
  } else if (!exceptionId) {
    InspectorTest.log('ERROR: No exception id');
  } else if (exceptionId !== event.params.exceptionId) {
    InspectorTest.log('ERROR: exception id does not match');
  }
  exceptionId = null;
  uncaught = false;
  let message = 'Previously printed uncaught exception revoked';
  if (exceptionDetails || exceptionStackTrace) {
    message = 'Uncaught message immediately revoked';
    exceptionDetails = null;
    exceptionStackTrace = null;
  }
  InspectorTest.log(message + ' for reason: ' + event.params.reason)
});
Protocol.Runtime.onExceptionThrown(event => {
  lazyLogException();
  if (uncaught) {
    InspectorTest.log('ERROR: Uncaught exception thrown twice');
  }
  uncaught = true;
  // Save this info and lazily log it so we can not log it when it is
  // immediately revoked.
  exceptionDetails = event.params.exceptionDetails.text;
  exceptionStackTrace = event.params.exceptionDetails.stackTrace;
  exceptionId = event.params.exceptionDetails.exceptionId;
});
Protocol.Runtime.enable();

const testFunctions = [];

function addTestFunctionsFor(throwFunc, catchFunc) {
  async function testCase(next) {
    InspectorTest.log(`> Throwing from ${throwFunc.name}, handling with ${catchFunc.name}`);
    await Protocol.Debugger.setPauseOnExceptions({state: 'all'});
    uncaught = false;
    pauseCount = 0;
    predictedUncaught = null;
    exceptionId = null;
    await Protocol.Runtime.evaluate({expression: `testWrapper(${throwFunc.name}, ${catchFunc.name})//# sourceURL=test_framework.js`, awaitPromise: true});
    lazyLogException();
    InspectorTest.log('');
    if (pauseCount !== 1) {
      InspectorTest.log(`WARNING: Test paused ${pauseCount} times`);
    }
    if (pauseCount > 0) {
      if (uncaught === predictedUncaught) {
        InspectorTest.log('Correctly predicted as ' + uncaughtString(uncaught));
      } else {
        InspectorTest.log(`PREDICTION MISMATCH: Exception was ${uncaughtString(uncaught)} but was predicted to be ${uncaughtString(predictedUncaught)}`);
      }
    }
    InspectorTest.log('------------------------------------------------------');
  }
  testFunctions.push(testCase);
}

for (const throwFunc of [...basicThrowFunctions, ...advancedThrowFunctions]) {
  for (const catchFunc of basicCatchFunctions) {
    addTestFunctionsFor(throwFunc, catchFunc);
  }
}

for (const catchFunc of advancedCatchFunctions) {
  for (const throwFunc of basicThrowFunctions) {
    addTestFunctionsFor(throwFunc, catchFunc);
  }
}

InspectorTest.runAsyncTestSuite(testFunctions);
