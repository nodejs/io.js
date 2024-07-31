'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const { NodeInstance } = require('../common/inspector-helper.js');

async function runTests() {
  const child = new NodeInstance(['-e', `(${main.toString()})()`], '', '');
  const session = await child.connectInspectorSession();
  await session.send({ method: 'Runtime.enable' });
  // Check that there is only one console message received.
  await session.waitForConsoleOutput('log', 'before wait for debugger');
  assert.ok(!session.unprocessedNotifications()
                    .some((n) => n.method === 'Runtime.consoleAPICalled'));
  // Check that inspector.url() is available between inspector.open() and
  // inspector.waitForDebugger()
  const { result: { value } } = await session.send({
    method: 'Runtime.evaluate',
    params: {
      expression: 'process._ws',
      includeCommandLineAPI: true
    }
  });
  assert.ok(value.startsWith('ws://'));
  await session.send({ method: 'NodeRuntime.enable' });
  child.write('first');
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send({ method: 'Runtime.runIfWaitingForDebugger' });
  await session.send({ method: 'NodeRuntime.disable' });
  // Check that messages after first and before second waitForDebugger are
  // received
  await session.waitForConsoleOutput('log', 'after wait for debugger');
  await session.waitForConsoleOutput('log', 'before second wait for debugger');
  assert.ok(!session.unprocessedNotifications()
                    .some((n) => n.method === 'Runtime.consoleAPICalled'));
  const secondSession = await child.connectInspectorSession();
  // Check that inspector.waitForDebugger can be resumed from another session
  await session.send({ method: 'NodeRuntime.enable' });
  child.write('second');
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send({ method: 'Runtime.runIfWaitingForDebugger' });
  await session.send({ method: 'NodeRuntime.disable' });
  await session.waitForConsoleOutput('log', 'after second wait for debugger');
  assert.ok(!session.unprocessedNotifications()
                    .some((n) => n.method === 'Runtime.consoleAPICalled'));
  secondSession.disconnect();
  session.disconnect();

  function main(prefix) {
    const inspector = require('inspector');
    inspector.open(0, undefined, false);
    process._ws = inspector.url();
    console.log('before wait for debugger');
    process.stdin.once('data', (data) => {
      if (data.toString() === 'first') {
        inspector.waitForDebugger();
        console.log('after wait for debugger');
        console.log('before second wait for debugger');
        process.stdin.once('data', (data) => {
          if (data.toString() === 'second') {
            inspector.waitForDebugger();
            console.log('after second wait for debugger');
            process.exit();
          }
        });
      }
    });
  }

  // Check that inspector.waitForDebugger throws if there is no active
  // inspector
  const re = /^Error \[ERR_INSPECTOR_NOT_ACTIVE\]: Inspector is not active$/;
  assert.throws(() => require('inspector').waitForDebugger(), re);
}

runTests().then(common.mustCall());
