// This tests esm loader's internal worker will not be blocked by --inspect-wait.
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
}

async function runTest() {
  const main = fixtures.path('es-module-loaders', 'register-loader.mjs');
  const child = new NodeInstance(['--inspect-wait=0'], '', main);

  const session = await child.connectInspectorSession();
  await runIfWaitingForDebugger(session);
  await session.waitForDisconnect();
  assert.strictEqual((await child.expectShutdown()).exitCode, 0);
}

runTest();
