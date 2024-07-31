'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const { NodeInstance } = require('../common/inspector-helper.js');

function mainContextDestroyed(notification) {
  return notification.method === 'Runtime.executionContextDestroyed' &&
      notification.params.executionContextId === 1;
}

async function runTest() {
  const child = new NodeInstance(['--inspect-brk=0', '-e', 'process.exit(55)']);
  const session = await child.connectInspectorSession();
  const oldStyleSession = await child.connectInspectorSession();
  await oldStyleSession.send([
    { method: 'Runtime.enable' }]);
  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send([
    { method: 'Runtime.enable' },
    { method: 'NodeRuntime.notifyWhenWaitingForDisconnect',
      params: { enabled: true } },
    { method: 'Runtime.runIfWaitingForDebugger' }]);
  await session.send({ method: 'NodeRuntime.disable' });
  await session.waitForNotification((notification) => {
    return notification.method === 'NodeRuntime.waitingForDisconnect';
  });
  const receivedExecutionContextDestroyed =
    session.unprocessedNotifications().some(mainContextDestroyed);
  if (receivedExecutionContextDestroyed) {
    assert.fail('When NodeRuntime enabled, ' +
      'Runtime.executionContextDestroyed should not be sent');
  }
  const { result: { value } } = await session.send({
    method: 'Runtime.evaluate', params: { expression: '42' }
  });
  assert.strictEqual(value, 42);
  await session.disconnect();
  await oldStyleSession.waitForNotification(mainContextDestroyed);
  await oldStyleSession.disconnect();
  assert.strictEqual((await child.expectShutdown()).exitCode, 55);
}

runTest().then(common.mustCall());
