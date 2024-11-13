'use strict';
const common = require('../common');
common.skipIfInspectorDisabled();
common.skipIf32Bits();
const { NodeInstance } = require('../common/inspector-helper');
const assert = require('assert');

const script = `runTest();
function runTest() {
  const p = Promise.resolve();
  p.then(function break1() { // lineNumber 3
    debugger;
  });
  p.then(function break2() { // lineNumber 6
    debugger;
  });
}
`;

async function runTests() {
  const instance = new NodeInstance(undefined, script);
  const session = await instance.connectInspectorSession();
  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send([
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Debugger.setAsyncCallStackDepth',
      'params': { 'maxDepth': 10 } },
    { 'method': 'Debugger.setBlackboxPatterns',
      'params': { 'patterns': [] } },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ]);
  await session.send({ method: 'NodeRuntime.disable' });

  await session.waitForBreakOnLine(0, '[eval]');
  await session.send({ 'method': 'Debugger.resume' });

  console.error('[test] Waiting for break1');
  debuggerPausedAt(await session.waitForBreakOnLine(4, '[eval]'),
                   'break1', 'runTest:3');

  await session.send({ 'method': 'Debugger.resume' });

  console.error('[test] Waiting for break2');
  debuggerPausedAt(await session.waitForBreakOnLine(7, '[eval]'),
                   'break2', 'runTest:6');

  await session.runToCompletion();
  assert.strictEqual((await instance.expectShutdown()).exitCode, 0);
}

function debuggerPausedAt(msg, functionName, previousTickLocation) {
  assert(
    !!msg.params.asyncStackTrace,
    `${Object.keys(msg.params)} contains "asyncStackTrace" property`);

  assert.strictEqual(msg.params.callFrames[0].functionName, functionName);
  assert.strictEqual(msg.params.asyncStackTrace.description, 'Promise.then');

  const frameLocations = msg.params.asyncStackTrace.callFrames.map(
    (frame) => `${frame.functionName}:${frame.lineNumber}`);
  assertArrayIncludes(frameLocations, previousTickLocation);
}

function assertArrayIncludes(actual, expected) {
  const expectedString = JSON.stringify(expected);
  const actualString = JSON.stringify(actual);
  assert(
    actual.includes(expected),
    `Expected ${actualString} to contain ${expectedString}.`);
}

runTests().then(common.mustCall());
