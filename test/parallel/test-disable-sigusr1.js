'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const { it } = require('node:test');
const assert = require('node:assert');
const { NodeInstance } = require('../common/inspector-helper.js');

common.skipIfInspectorDisabled();

it('should not attach a debugger with SIGUSR1', { skip: common.isWindows }, async () => {
  const file = fixtures.path('disable-signal/sigusr1.js');
  const instance = new NodeInstance(['--disable-sigusr1'], undefined, file);

  instance.on('stderr', common.mustNotCall());
  const loggedPid = await new Promise((resolve) => {
    instance.on('stdout', (data) => {
      const matches = data.match(/pid is (\d+)/);
      if (matches) resolve(Number(matches[1]));
    });
  });

  assert.ok(process.kill(instance.pid, 'SIGUSR1'));
  assert.strictEqual(loggedPid, instance.pid);
  assert.ok(await instance.kill());
});
