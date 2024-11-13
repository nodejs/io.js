'use strict';
// Flags: --expose-internals
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');

if (process.argv[2] === 'child') {
  setTimeout(() => {}, common.platformTimeout(100));
  return;
}

// Monkey patch spawn() to create a child process normally, but destroy the
// stdout and stderr streams. This replicates the conditions where the streams
// cannot be properly created.
const ChildProcess = require('internal/child_process').ChildProcess;
const original = ChildProcess.prototype.spawn;

ChildProcess.prototype.spawn = function() {
  const err = original.apply(this, arguments);

  this.stdout.destroy();
  this.stderr.destroy();
  this.stdout = null;
  this.stderr = null;

  return err;
};

function createChild(options, callback) {
  const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" "${__filename}" child`;
  options = { ...options, env: { ...opts?.env, ...options.env } };

  return cp.exec(cmd, options, common.mustCall(callback));
}

// Verify that normal execution of a child process is handled.
{
  createChild({}, (err, stdout, stderr) => {
    assert.strictEqual(err, null);
    assert.strictEqual(stdout, '');
    assert.strictEqual(stderr, '');
  });
}

// Verify that execution with an error event is handled.
{
  const error = new Error('foo');
  const child = createChild({}, (err, stdout, stderr) => {
    assert.strictEqual(err, error);
    assert.strictEqual(stdout, '');
    assert.strictEqual(stderr, '');
  });

  child.emit('error', error);
}

// Verify that execution with a killed process is handled.
{
  createChild({ timeout: 1 }, (err, stdout, stderr) => {
    assert.strictEqual(err.killed, true);
    assert.strictEqual(stdout, '');
    assert.strictEqual(stderr, '');
  });
}
