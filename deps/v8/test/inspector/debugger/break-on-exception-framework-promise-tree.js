// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Checks that exceptions in framework code have correct stopping behavior.');

// framework.js

async function frameworkUncaught() {
  await complicatedThrow(false);
}

async function frameworkCaught() {
  await complicatedThrow(true);
}

async function uncaughtAsync() {
  await delay();
  throw new Error('Failed');
}

function delay() {
  return new Promise(resolve => setTimeout(resolve, 0));
}

function branchPromise(func, branchCatches) {
  const newPromise = func().then(()=>console.log('Will not print'));
  if (branchCatches) {
    newPromise.catch((e)=>console.error('Caught at ' + e.stack));
  } else {
    newPromise.then(()=>console.log('Also will not print'));
  }
  return newPromise;
}

async function frameworkAsync(func) {
  await func();
}

function complicatedThrow(catches) {
  return frameworkAsync(branchPromise.bind(null, frameworkAsync.bind(null, branchPromise.bind(null, uncaughtAsync, false)), catches));
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
  await new Promise(resolve => setTimeout(runWithResolution.bind(null, resolve), 0));
}

// user.js

async function caughtInUser() {
  try {
    await complicatedThrow(false);
  } catch (e) {
    console.log('Caught in user code at ' + e.stack);
  }
}

async function caughtInUserAndFramework() {
  try {
    await complicatedThrow(true);
  } catch (e) {
    console.log('Caught in user code at ' + e.stack);
  }
}

async function uncaughtInUser() {
  await complicatedThrow(false);
}

async function uncaughtInUserCaughtInFramework() {
  await complicatedThrow(true);
}

function branchInUser() {
  const p = complicatedThrow(false);
  p.catch((e)=>console.error('Caught in user code at ' + e.stack));
  return p;
}

// -------------------------------------

// Order of functions should match above so that line numbers match

const files = [
  {
    name: 'framework.js',
    scenarios: [frameworkUncaught, frameworkCaught],
    helpers: [uncaughtAsync, delay, branchPromise, frameworkAsync, complicatedThrow, testWrapper],
    startLine: 9,
  }, {
    name: 'user.js',
    scenarios: [caughtInUser, caughtInUserAndFramework, uncaughtInUser, uncaughtInUserCaughtInFramework, branchInUser],
    helpers: [],
    startLine: 64,
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

Protocol.Console.onMessageAdded(event => InspectorTest.log('console: ' + event.params.message.text));
Protocol.Debugger.enable();
Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 6});
Protocol.Console.enable();
Protocol.Runtime.onExceptionRevoked(event => InspectorTest.log('Exception revoked for reason: ' + event.params.reason));
Protocol.Runtime.onExceptionThrown(event => {
  InspectorTest.log(`Uncaught exception: ${event.params.exceptionDetails.text}`);
  session.logAsyncStackTrace(event.params.exceptionDetails.stackTrace);
});
Protocol.Runtime.enable();

const testFunctions = [];

for (const scenario of allScenarios) {
  for (const state of ['caught', 'uncaught']) {
    for (const ignoreList of [true, false]) {
      const patterns = ignoreList ? ['framework\.js'] : [];
      testFunctions.push(async function testCase() {
        InspectorTest.log(`> Running scenario ${scenario.name}, breaking on ${state} exceptions, ignore listing ${ignoreList ? 'on' : 'off'}:`);
        await Protocol.Debugger.setPauseOnExceptions({state});
        await Protocol.Debugger.setBlackboxPatterns({patterns});
        await Protocol.Runtime.evaluate({expression: `testWrapper(${scenario.name})//# sourceURL=test_framework.js`, awaitPromise: true});
      });
    }
  }
}

InspectorTest.runAsyncTestSuite(testFunctions);
