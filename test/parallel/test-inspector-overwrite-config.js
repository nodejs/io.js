// Flags: --require ./test/fixtures/overwrite-config-preload-module.js
'use strict';

// This test ensures that overwriting a process configuration
// value does not affect code in lib/internal/bootstrap/node.js.
// Specifically this tests
// that the inspector console functions are bound even though
// overwrite-config-preload-module.js overwrote the process.config variable.

// We cannot do a check for the inspector because the configuration variables
// were reset/removed by overwrite-config-preload-module.js.
/* eslint-disable node-core/inspector-check */

const common = require('../common');
const assert = require('assert');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('--require does not work with Workers');
}

const inspector = require('inspector');
const msg = 'Test inspector logging';
let asserted = false;

async function testConsoleLog() {
  const session = new inspector.Session();
  session.connect();
  session.on('inspectorNotification', (data) => {
    if (data.method === 'Runtime.consoleAPICalled') {
      assert.strictEqual(data.params.args.length, 1);
      assert.strictEqual(data.params.args[0].value, msg);
      asserted = true;
    }
  });
  session.post('Runtime.enable');
  console.log(msg);
  session.disconnect();
}

async function runTests() {
  await testConsoleLog();
  assert.ok(asserted, 'log statement did not reach the inspector');
}

runTests().then(common.mustCall());
