'use strict';
const common = require('../common');

// Regression test for https://github.com/nodejs/node/issues/32648

common.skipIfInspectorDisabled();

const { NodeInstance } = require('../common/inspector-helper.js');

async function runTest() {
  const child = new NodeInstance(['--inspect-brk-node=0', '-p', '42']);
  const session = await child.connectInspectorSession();
  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send({ method: 'Runtime.enable' });
  await session.send({ method: 'Debugger.enable' });
  await session.send({ method: 'Runtime.runIfWaitingForDebugger' });
  await session.send({ method: 'NodeRuntime.disable' });
  await session.waitForNotification((notification) => {
    // The main assertion here is that we do hit the loader script first.
    return notification.method === 'Debugger.scriptParsed' &&
           notification.params.url === 'node:internal/bootstrap/realm';
  });

  await session.waitForNotification('Debugger.paused');
  await session.send({ method: 'Debugger.resume' });
  await session.waitForNotification('Debugger.paused');
  await session.runToCompletion();
}

runTest().then(common.mustCall());
