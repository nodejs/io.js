// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Checks that conditional breakpoints can mute other break reasons.');

// breaking-functions.js

function rejectInPromiseConstructor() {
  return new Promise((pass, reject) => reject('f'));
}

function promiseReject() {
  return Promise.reject('f');
}

function throwStatement() {
  throw 'f';
}

function debuggerStatement() {
  debugger;
}

function runtimeError(unused) {
  unused.x();
}

function runtimeFunctionThrows() {
  return JSON.parse('}');
}

function domBreakpoint() {
  inspector.breakProgram('DOM', '');
}

function badConstructor() {
  Set();  // Requires new
}

// -------------------------------------

// Order of functions should match above so that line numbers match
const breakingFunctions = [
    rejectInPromiseConstructor,
    promiseReject,
    throwStatement,
    debuggerStatement,
    runtimeError,
    runtimeFunctionThrows,
    domBreakpoint,
    badConstructor,
];

const file = breakingFunctions.join('\n\n');
const startLine = 9;  // Should match first line of first function
contextGroup.addScript(file, startLine, 0, 'breaking-functions.js');
session.setupScriptMap();

let pauseCount = 0;
let messageCount = 0;
let location = {};

Protocol.Debugger.onPaused(message => {
  InspectorTest.log(`Paused on ${message.params.reason}`);
  session.logCallFrames(message.params.callFrames);
  InspectorTest.log('');
  pauseCount++;
  // Save break location so we don't pause here again
  location = message.params.callFrames[0].location;
  Protocol.Debugger.resume();
});

Protocol.Console.onMessageAdded(event => {
  InspectorTest.log('console: ' + event.params.message.text);
  messageCount++;
});

Protocol.Debugger.enable();
Protocol.Console.enable();

const testFunctions = [];

for (const breakingFunc of breakingFunctions) {
  async function testCase(next) {
    InspectorTest.log(`> Testing pause muting in ${breakingFunc}`);
    await Protocol.Debugger.setPauseOnExceptions({state: 'all'});
    await Protocol.Debugger.setInstrumentationBreakpoint({eventName: 'setInterval'});
    await Protocol.Debugger.setInstrumentationBreakpoint({eventName: 'setTimeout.callback'});
    const evalArgs = {expression: `${breakingFunc.name}();`, awaitPromise: true};
    pauseCount = 0;
    messageCount = 0;
    await Protocol.Runtime.evaluate(evalArgs);
    if (pauseCount !== 1) {
      InspectorTest.log(`WARNING: Test paused ${pauseCount} times`);
    }
    if (messageCount !== 0) {
      InspectorTest.log(`WARNING: Test printed to console ${messageCount} times`);
    }

    // Now mute location with log point...
    InspectorTest.log(`> Running again with location ${location.lineNumber}:${location.columnNumber} muted`);
    Protocol.Debugger.setBreakpoint({ location, condition: 'console.log("log instead of break")' });
    // And run again...
    pauseCount = 0;
    messageCount = 0;
    await Protocol.Runtime.evaluate(evalArgs);
    if (pauseCount !== 0) {
      InspectorTest.log(`WARNING: Test paused ${pauseCount} times`);
    }
    if (messageCount !== 1) {
      InspectorTest.log(`WARNING: Test printed to console ${messageCount} times`);
    }
  }
  testFunctions.push(testCase);
}

InspectorTest.runAsyncTestSuite(testFunctions);
