// Flags: --expose-internals
'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const { test } = require('node:test');
const internalCp = require('internal/child_process');
const cmd = process.execPath;
const args = ['-p', '42'];
const options = { windowsHide: true };

// Since windowsHide isn't really observable, this test relies on monkey
// patching spawn() and spawnSync() to verify that the flag is being passed
// through correctly.

test('spawnSync() passes windowsHide correctly', (t) => {
  const spy = t.mock.method(internalCp, 'spawnSync');
  const child = cp.spawnSync(cmd, args, options);

  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
  assert.strictEqual(child.stdout.toString().trim(), '42');
  assert.strictEqual(child.stderr.toString().trim(), '');
  assert.strictEqual(spy.mock.calls.length, 1);
  assert.strictEqual(spy.mock.calls[0].arguments[0].windowsHide, true);
});

test('spawn() passes windowsHide correctly', (t, done) => {
  const spy = t.mock.method(internalCp.ChildProcess.prototype, 'spawn');
  const child = cp.spawn(cmd, args, options);

  child.on('exit', common.mustCall((code, signal) => {
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
    assert.strictEqual(spy.mock.calls.length, 1);
    assert.strictEqual(spy.mock.calls[0].arguments[0].windowsHide, true);
    done();
  }));
});

test('execFile() passes windowsHide correctly', (t, done) => {
  const spy = t.mock.method(internalCp.ChildProcess.prototype, 'spawn');
  cp.execFile(cmd, args, options, common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(stdout.trim(), '42');
    assert.strictEqual(stderr.trim(), '');
    assert.strictEqual(spy.mock.calls.length, 1);
    assert.strictEqual(spy.mock.calls[0].arguments[0].windowsHide, true);
    done();
  }));
});
