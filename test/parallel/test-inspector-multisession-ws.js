'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const { NodeInstance } = require('../common/inspector-helper.js');

// Sets up JS bindings session and runs till the "paused" event
const script = `
const { Session } = require('inspector');
const session = new Session();
let done = false;
const interval = setInterval(() => {
  if (done)
    clearInterval(interval);
}, 150);
session.on('Debugger.paused', () => {
  done = true;
});
session.connect();
session.post('Debugger.enable');
console.log('Ready');
`;

async function setupSession(node) {
  const session = await node.connectInspectorSession();
  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send([
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
  ]);

  return session;
}

async function testSuspend(sessionA, sessionB) {
  console.log('[test]', 'Breaking in code and verifying events are fired');
  await Promise.all([
    sessionA.waitForNotification('Debugger.paused', 'Initial sessionA paused'),
    sessionB.waitForNotification('Debugger.paused', 'Initial sessionB paused'),
  ]);
  sessionA.send({ 'method': 'Debugger.resume' });

  await sessionA.waitForNotification('Runtime.consoleAPICalled',
                                     'Console output');
  sessionA.send({ 'method': 'Debugger.pause' });
  return Promise.all([
    sessionA.waitForNotification('Debugger.paused', 'SessionA paused'),
    sessionB.waitForNotification('Debugger.paused', 'SessionB paused'),
  ]);
}

async function runTest() {
  const child = new NodeInstance(undefined, script);

  const [session1, session2] =
      await Promise.all([setupSession(child), setupSession(child)]);
  await Promise.all([
    session1.send({ method: 'Runtime.runIfWaitingForDebugger' }),
    session2.send({ method: 'Runtime.runIfWaitingForDebugger' }),
  ]);
  await Promise.all([
    session1.send({ method: 'NodeRuntime.disable' }),
    session2.send({ method: 'NodeRuntime.disable' }),
  ]);
  await testSuspend(session2, session1);
  console.log('[test]', 'Should shut down after both sessions disconnect');

  await session1.runToCompletion();
  await session2.send({ 'method': 'Debugger.disable' });
  await session2.disconnect();
  return child.expectShutdown();
}

runTest().then(common.mustCall());
