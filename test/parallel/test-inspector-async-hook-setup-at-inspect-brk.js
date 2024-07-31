'use strict';
const common = require('../common');
common.skipIfInspectorDisabled();
common.skipIf32Bits();
const { NodeInstance } = require('../common/inspector-helper.js');
const assert = require('assert');

const script = `
setTimeout(() => {
  debugger;
  process.exitCode = 55;
}, 50);
`;

async function skipBreakpointAtStart(session) {
  await session.waitForBreakOnLine(1, '[eval]');
  await session.send({ 'method': 'Debugger.resume' });
}

async function checkAsyncStackTrace(session) {
  console.error('[test]', 'Verify basic properties of asyncStackTrace');
  const paused = await session.waitForBreakOnLine(2, '[eval]');
  assert(paused.params.asyncStackTrace,
         `${Object.keys(paused.params)} contains "asyncStackTrace" property`);
  assert(paused.params.asyncStackTrace.description, 'Timeout');
  assert(paused.params.asyncStackTrace.callFrames
           .some((frame) => frame.url === 'node:internal/process/execution'));
}

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
  await skipBreakpointAtStart(session);
  await checkAsyncStackTrace(session);

  await session.runToCompletion();
  assert.strictEqual((await instance.expectShutdown()).exitCode, 55);
}

runTests().then(common.mustCall());
