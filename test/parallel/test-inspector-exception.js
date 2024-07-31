'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');

common.skipIfInspectorDisabled();

const assert = require('assert');
const { NodeInstance } = require('../common/inspector-helper.js');
const { pathToFileURL } = require('url');

const script = fixtures.path('throws_error.js');

async function testBreakpointOnStart(session) {
  console.log('[test]',
              'Verifying debugger stops on start (--inspect-brk option)');
  const commands = [
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Debugger.setPauseOnExceptions',
      'params': { 'state': 'none' } },
    { 'method': 'Debugger.setAsyncCallStackDepth',
      'params': { 'maxDepth': 0 } },
    { 'method': 'Profiler.enable' },
    { 'method': 'Profiler.setSamplingInterval',
      'params': { 'interval': 100 } },
    { 'method': 'Debugger.setBlackboxPatterns',
      'params': { 'patterns': [] } },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ];

  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send(commands);
  await session.send({ method: 'NodeRuntime.disable' });
  await session.waitForBreakOnLine(21, pathToFileURL(script).toString());
}


async function runTest() {
  const child = new NodeInstance(undefined, undefined, script);
  const session = await child.connectInspectorSession();
  await testBreakpointOnStart(session);
  await session.runToCompletion();
  assert.strictEqual((await child.expectShutdown()).exitCode, 1);
}

runTest().then(common.mustCall());
