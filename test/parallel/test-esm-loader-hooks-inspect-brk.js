// This tests esm loader's internal worker will not be blocked by --inspect-brk.
// Regression: https://github.com/nodejs/node/issues/53681

'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const fixtures = require('../common/fixtures');
const { NodeInstance } = require('../common/inspector-helper.js');

async function runTest() {
  const main = fixtures.path('es-module-loaders', 'register-loader.mjs');
  const child = new NodeInstance(['--inspect-brk=0'], '', main);

  const session = await child.connectInspectorSession();
  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send([
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ]);
  await session.send({ method: 'NodeRuntime.disable' });
  await session.waitForNotification('Debugger.paused');
  await session.runToCompletion();
  assert.strictEqual((await child.expectShutdown()).exitCode, 0);
}

runTest();
