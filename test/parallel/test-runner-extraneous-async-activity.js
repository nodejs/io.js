'use strict';
require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');
const { spawnSync } = require('child_process');

{
  const child = spawnSync(process.execPath, [
    '--test',
    fixtures.path('test-runner', 'extraneous_set_immediate_async.mjs'),
  ]);
  const stdout = child.stdout.toString();
  assert.match(stdout, /^# Error: Test "extraneous async activity test" at .+extraneous_set_immediate_async\.mjs:3:1 generated asynchronous activity after the test ended/m);
  assert.match(stdout, /^# pass 1/m);
  assert.match(stdout, /^# fail 1$/m);
  assert.match(stdout, /^# cancelled 0$/m);
  assert.strictEqual(child.status, 1);
  assert.strictEqual(child.signal, null);
}

{
  const child = spawnSync(process.execPath, [
    '--test',
    fixtures.path('test-runner', 'extraneous_set_timeout_async.mjs'),
  ]);
  const stdout = child.stdout.toString();
  assert.match(stdout, /^# Error: Test "extraneous async activity test" at .+extraneous_set_timeout_async\.mjs:3:1 generated asynchronous activity after the test ended/m);
  assert.match(stdout, /^# pass 1$/m);
  assert.match(stdout, /^# fail 1$/m);
  assert.match(stdout, /^# cancelled 0$/m);
  assert.strictEqual(child.status, 1);
  assert.strictEqual(child.signal, null);
}

{
  const child = spawnSync(process.execPath, [
    '--test',
    fixtures.path('test-runner', 'async-error-in-test-hook.mjs'),
  ]);
  const stdout = child.stdout.toString();
  assert.match(stdout, /^# Error: Test hook "before" at .+async-error-in-test-hook\.mjs:3:1 generated asynchronous activity after the test ended/m);
  assert.match(stdout, /^# Error: Test hook "beforeEach" at .+async-error-in-test-hook\.mjs:9:1 generated asynchronous activity after the test ended/m);
  assert.match(stdout, /^# Error: Test hook "after" at .+async-error-in-test-hook\.mjs:15:1 generated asynchronous activity after the test ended/m);
  assert.match(stdout, /^# Error: Test hook "afterEach" at .+async-error-in-test-hook\.mjs:21:1 generated asynchronous activity after the test ended/m);
  assert.match(stdout, /^# pass 1$/m);
  assert.match(stdout, /^# fail 1$/m);
  assert.match(stdout, /^# cancelled 0$/m);
  assert.strictEqual(child.status, 1);
  assert.strictEqual(child.signal, null);
}
