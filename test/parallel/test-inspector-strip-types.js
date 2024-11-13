'use strict';

const common = require('../common');
common.skipIfInspectorDisabled();
if (!process.config.variables.node_use_amaro) common.skip('Requires Amaro');

const { NodeInstance } = require('../common/inspector-helper.js');
const fixtures = require('../common/fixtures');
const assert = require('assert');

const scriptPath = fixtures.path('typescript/ts/test-typescript.ts');

async function runTest() {
  const child = new NodeInstance(
    ['--inspect-brk=0', '--experimental-strip-types'],
    undefined,
    scriptPath);

  const session = await child.connectInspectorSession();

  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send([
    { 'method': 'Debugger.enable' },
    { 'method': 'Runtime.enable' },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ]);
  await session.send({ method: 'NodeRuntime.disable' });

  const scriptParsed = await session.waitForNotification((notification) => {
    if (notification.method !== 'Debugger.scriptParsed') return false;

    return notification.params.url === scriptPath;
  });
  // Verify that the script has a sourceURL, hinting that it is a generated source.
  assert(scriptParsed.params.hasSourceURL);

  await session.waitForPauseOnStart();
  await session.runToCompletion();

  assert.strictEqual((await child.expectShutdown()).exitCode, 0);
}

runTest().then(common.mustCall());
