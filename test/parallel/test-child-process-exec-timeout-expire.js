'use strict';

// Test exec() with a timeout that expires.

const common = require('../common');
const assert = require('assert');
const cp = require('child_process');

const {
  cleanupStaleProcess,
  logAfterTime,
  kExpiringChildRunTime,
  kExpiringParentTimer
} = require('../common/child_process');

if (process.argv[2] === 'child') {
  logAfterTime(kExpiringChildRunTime);
  return;
}

const cmd = `"${process.execPath}" "${__filename}" child`;

cp.exec(cmd, {
  timeout: kExpiringParentTimer,
}, common.mustCall((err, stdout, stderr) => {
  console.log('[stdout]', stdout.trim());
  console.log('[stderr]', stderr.trim());

  let sigterm = 'SIGTERM';
  assert.strictEqual(err.killed, true);
  // TODO OpenBSD returns a null signal and 143 for code
  if (common.isOpenBSD) {
    assert.strictEqual(err.code, 143);
    sigterm = null;
  } else {
    assert.strictEqual(err.code, null);
  }
  // At least starting with Darwin Kernel Version 16.4.0, sending a SIGTERM to a
  // process that is still starting up kills it with SIGKILL instead of SIGTERM.
  // See: https://github.com/libuv/libuv/issues/1226
  if (common.isMacOS)
    assert.ok(err.signal === 'SIGTERM' || err.signal === 'SIGKILL');
  else
    assert.strictEqual(err.signal, sigterm);
  assert.strictEqual(err.cmd, cmd);
  assert.strictEqual(stdout.trim(), '');
  assert.strictEqual(stderr.trim(), '');
}));

cleanupStaleProcess(__filename);
