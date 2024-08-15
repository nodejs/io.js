// This tests esm loader's internal worker will not be blocked by --inspect-brk.
// Regression: https://github.com/nodejs/node/issues/53681

'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const fixtures = require('../common/fixtures');
const { NodeInstance } = require('../common/inspector-helper.js');

async function runIfWaitingForDebugger(session) {
  const commands = [
    { 'method': 'Runtime.enable' },
    { 'method': 'Debugger.enable' },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ];

  await session.send(commands);
  await session.waitForNotification('Debugger.paused');
}

async function runTest() {
  const main = fixtures.path('es-module-loaders', 'register-loader.mjs');
  const child = new NodeInstance(['--inspect-brk=0'], '', main);

  const session = await child.connectInspectorSession();
  await runIfWaitingForDebugger(session);
  await session.runToCompletion();
  assert.strictEqual((await child.expectShutdown()).exitCode, 0);
}

runTest();
