// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Checks that exceptions in framework code have correct stopping behavior.');

// framework.js

function throwUncaughtError() {
  throw new Error();
}

function throwCaughtError() {
  try {
    throw new Error();
  } catch (e) {
    console.log('Caught at ' + e.stack);
  }
}

function catchErrorInFramework() {
  wrapErrorHandler(throwUncaughtError);
}

function frameworkFinallyMethod() {
  return uncaughtAsync().finally(
      () => console.log('finally in framework code'));
}

async function uncaughtAsync() {
  await delay();
  throwUncaughtError();
}

function delay() {
  return new Promise(resolve => setTimeout(resolve, 0));
}

function wrapErrorHandler(errorFunc) {
  try {
    errorFunc();
  } catch (e) {
    console.log('Caught at ' + e.stack);
  }
}

async function testWrapper(testFunc) {
  // testWrapper runs testFunc and completes successfully when testFunc
  // completes, but does so in a setTimeout so that it doesn't catch any
  // exceptions thrown by testFunc.
  async function runWithResolution(resolve) {
    try {
      await testFunc();
      console.log('finished without error');
    } finally {
      // Leave exceptions uncaught, but the testWrapper continues normally
      // anyway. Needs to be in a setTimeout so that uncaught exceptions
      // are handled before the test is complete.
      setTimeout(resolve, 0);
    }
  }
  await new Promise(resolve => setTimeout(runWithResolution.bind(null, resolve),
                                          0));
}

// user.js

function catchErrorInUserCode() {
  try {
    throwUncaughtError();
  } catch (e) {
    console.log('Caught at ' + e.stack);
  }
}

function passErrorThroughUserCode() {
  throwUncaughtError();
}

function notAffectingUserCode() {
  catchErrorInFramework();
}

async function uncaughtWithAsyncUserCode() {
  await uncaughtAsync();
}

async function uncaughtWithAsyncUserCodeAndDelay() {
  await delay();
  throwUncaughtError();
}

async function uncaughtWithAsyncUserCodeMissingAwait() {
  return uncaughtWithAsyncUserCodeAndDelay();
}

function catchPassingThroughUserCode() {
  wrapErrorHandler(passErrorThroughUserCode);
}

function userException() {
  throw new Error();
}

function catchUserException() {
  wrapErrorHandler(userException);
}

function nowhereToStop() {
  setTimeout(JSON.parse.bind(null, 'ping'), 0);
}

function userFinallyMethod() {
  return uncaughtAsync().finally(() => console.log('finally in user code'));
}

// -------------------------------------

// Order of functions should match above so that line numbers match

const files = [
  {
    name: 'framework.js',
    scenarios: [
        throwUncaughtError,
        throwCaughtError,
        catchErrorInFramework,
        frameworkFinallyMethod
    ],
    helpers: [uncaughtAsync, delay, wrapErrorHandler, testWrapper],
    startLine: 9,
  }, {
    name: 'user.js',
    scenarios: [
        catchErrorInUserCode,
        passErrorThroughUserCode,
        notAffectingUserCode,
        uncaughtWithAsyncUserCode,
        uncaughtWithAsyncUserCodeAndDelay,
        uncaughtWithAsyncUserCodeMissingAwait,
        catchPassingThroughUserCode,
        userException,
        catchUserException,
        nowhereToStop,
        userFinallyMethod
    ],
    helpers: [],
    startLine: 68,
  }
];

const allScenarios = [];

for (const {name, scenarios, helpers, startLine} of files) {
  allScenarios.push(...scenarios);
  const file = [...scenarios, ...helpers].join('\n\n');
  contextGroup.addScript(file, startLine, 0, name);
}

session.setupScriptMap();
Protocol.Debugger.onPaused(message => {
  InspectorTest.log('Paused');
  session.logCallFrames(message.params.callFrames);
  session.logAsyncStackTrace(message.params.asyncStackTrace);
  InspectorTest.log('');
  Protocol.Debugger.resume();
});

Protocol.Console.onMessageAdded(
    event => InspectorTest.log('console: ' + event.params.message.text));
Protocol.Debugger.enable();
Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 6});
Protocol.Console.enable();
Protocol.Debugger.setBlackboxPatterns({patterns: ['framework\.js']});
Protocol.Runtime.onExceptionRevoked(event => InspectorTest.log(
    'Exception revoked for reason: ' + event.params.reason));
Protocol.Runtime.onExceptionThrown(event => {
  InspectorTest.log(
      `Uncaught exception: ${event.params.exceptionDetails.text}`);
  session.logAsyncStackTrace(event.params.exceptionDetails.stackTrace);
});
Protocol.Runtime.enable();

const testFunctions = [];

for (const scenario of allScenarios) {
  for (const state of ['caught', 'uncaught']) {
    testFunctions.push(async function testCase() {
      InspectorTest.log(`> Running scenario ${scenario.name}, breaking on ${state} exceptions:`);
      await Protocol.Debugger.setPauseOnExceptions({state});
      await Protocol.Runtime.evaluate({
          expression: `testWrapper(${scenario.name})//# sourceURL=test_framework.js`,
          awaitPromise: true
      });
    });
  }
}

InspectorTest.runAsyncTestSuite(testFunctions);
