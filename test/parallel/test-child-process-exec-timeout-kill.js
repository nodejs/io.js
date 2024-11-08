'use strict';

// Test exec() with both a timeout and a killSignal.

const common = require('../common');
const assert = require('assert');
const cp = require('child_process');

const {
  cleanupStaleProcess,
  logAfterTime,
  kExpiringChildRunTime,
  kExpiringParentTimer,
} = require('../common/child_process');

if (process.argv[2] === 'child') {
  logAfterTime(kExpiringChildRunTime);
  return;
}

const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" "${__filename}" child`;

// Test with a different kill signal.
cp.exec(cmd, {
  ...opts,
  timeout: kExpiringParentTimer,
  killSignal: 'SIGKILL'
}, common.mustCall((err, stdout, stderr) => {
  console.log('[stdout]', stdout.trim());
  console.log('[stderr]', stderr.trim());

  assert.strictEqual(err.killed, true);
  assert.strictEqual(err.code, null);
  assert.strictEqual(err.signal, 'SIGKILL');
  assert.strictEqual(err.cmd, cmd);
  assert.strictEqual(stdout.trim(), '');
  assert.strictEqual(stderr.trim(), '');
}));

cleanupStaleProcess(__filename);
