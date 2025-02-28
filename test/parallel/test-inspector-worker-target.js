'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');

common.skipIfInspectorDisabled();

const { NodeInstance } = require('../common/inspector-helper.js');

async function setupInspector(session, sessionId = undefined) {
  await session.send({ method: 'NodeRuntime.enable', sessionId });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send({ method: 'Runtime.enable', sessionId });
  await session.send({ method: 'Debugger.enable', sessionId });
  await session.send({ method: 'Runtime.runIfWaitingForDebugger', sessionId });
  await session.send({ method: 'NodeRuntime.disable', sessionId });
  await session.waitForNotification((notification) => {
    return notification.method === 'Debugger.scriptParsed' &&
            notification.params.url === 'node:internal/bootstrap/realm' &&
            notification.sessionId === sessionId;
  });
}

async function test(isSetAutoAttachBeforeExecution) {
  const child = new NodeInstance(['--inspect-brk=0', '--experimental-worker-inspection'],
                                 '',
                                 fixtures.path('inspect-worker/index.js')
  );


  const session = await child.connectInspectorSession();
  await setupInspector(session);

  if (isSetAutoAttachBeforeExecution) {
    await session.send({ method: 'Target.setAutoAttach', params: { autoAttach: true, waitForDebuggerOnStart: true } });
  }
  await session.waitForNotification('Debugger.paused');
  await session.send({ method: 'Debugger.resume' });

  const sessionId = '1';
  await session.waitForNotification('Target.targetCreated');

  if (!isSetAutoAttachBeforeExecution) {
    await session.send({ method: 'Target.setAutoAttach', params: { autoAttach: true, waitForDebuggerOnStart: true } });
  }
  await session.waitForNotification((notification) => {
    return notification.method === 'Target.attachedToTarget' &&
           notification.params.sessionId === sessionId;
  });
  await setupInspector(session, sessionId);
  await session.waitForNotification('Debugger.paused');
  await session.send({ method: 'Debugger.resume', sessionId });
  await session.waitForDisconnect();
}

test(true);
test(false);
