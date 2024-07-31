import * as common from '../common/index.mjs';

common.skipIfInspectorDisabled();

import assert from 'node:assert';
import { NodeInstance } from '../common/inspector-helper.js';


async function runTests() {
  const child = new NodeInstance(['--inspect-wait=0'], 'console.log(0);');
  const session = await child.connectInspectorSession();
  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');

  // The execution should be paused until the debugger is attached
  while (await child.nextStderrString() !== 'Debugger attached.');

  await session.send({ 'method': 'Runtime.runIfWaitingForDebugger' });

  // Wait for the execution to finish
  while (await child.nextStderrString() !== 'Waiting for the debugger to disconnect...');

  await session.send({ method: 'NodeRuntime.disable' });
  session.disconnect();
  assert.strictEqual((await child.expectShutdown()).exitCode, 0);
}

runTests().then(common.mustCall());
