'use strict';
const common = require('../common');
const assert = require('assert');

common.skipIfInspectorDisabled();

const { NodeInstance } = require('../common/inspector-helper.js');

async function runTest() {
  const child = new NodeInstance(['--inspect-brk'], 'console.log(1');
  const session = await child.connectInspectorSession();
  await session.send({ method: 'Runtime.enable' });
  await session.send({ method: 'Debugger.enable' });
  await session.send({ method: 'Runtime.runIfWaitingForDebugger' });
  let match = false;
  while (true) {
    const stderr = await child.nextStderrString();
    if (/SyntaxError: missing \) after argument list/.test(stderr)) {
      match = true;
    }
    if (/Waiting for the debugger to disconnect/.test(stderr)) {
      break;
    }
  }
  assert.ok(match);
  await session.disconnect();
  return child.expectShutdown();
}

runTest().then(common.mustCall());
