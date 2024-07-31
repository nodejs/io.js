'use strict';

const common = require('../common');
common.skipIfInspectorDisabled();

const { NodeInstance } = require('../common/inspector-helper.js');
const assert = require('assert');

async function runTest() {
  const script = 'require(\'inspector\').console.log(\'hello world\');';
  const child = new NodeInstance('--inspect-brk=0', script, '');

  let out = '';
  child.on('stdout', (line) => out += line);

  const session = await child.connectInspectorSession();

  const commands = [
    { 'method': 'Runtime.enable' },
    { 'method': 'Runtime.runIfWaitingForDebugger' },
  ];

  await session.send({ method: 'NodeRuntime.enable' });
  await session.waitForNotification('NodeRuntime.waitingForDebugger');
  await session.send(commands);
  await session.send({ method: 'NodeRuntime.disable' });

  const msg = await session.waitForNotification('Runtime.consoleAPICalled');

  assert.strictEqual(msg.params.type, 'log');
  assert.deepStrictEqual(msg.params.args, [{
    type: 'string',
    value: 'hello world'
  }]);
  assert.strictEqual(out, '');

  session.disconnect();
}

runTest().then(common.mustCall());
