'use strict';
// Flags: --expose-internals
const common = require('../common');

if (process.argv[2] === 'child') {
  setTimeout(() => {}, common.platformTimeout(1000));
  return;
}

const assert = require('node:assert');
const cp = require('node:child_process');
const { mock, test } = require('node:test');
const { ChildProcess } = require('internal/child_process');

// Monkey patch spawn() to create a child process normally, but destroy the
// stdout and stderr streams. This replicates the conditions where the streams
// cannot be properly created.
const original = ChildProcess.prototype.spawn;

mock.method(ChildProcess.prototype, 'spawn', function() {
  const err = original.apply(this, arguments);

  this.stdout.destroy();
  this.stderr.destroy();
  this.stdout = null;
  this.stderr = null;

  return err;
});

function createChild(options, callback) {
  const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" "${__filename}" child`;
  options = { ...options, env: { ...opts?.env, ...options.env } };

  return cp.exec(cmd, options, common.mustCall(callback));
}

test('normal execution of a child process is handled', (_, done) => {
  createChild({}, (err, stdout, stderr) => {
    assert.strictEqual(err, null);
    assert.strictEqual(stdout, '');
    assert.strictEqual(stderr, '');
    done();
  });
});

test('execution with an error event is handled', (_, done) => {
  const error = new Error('foo');
  const child = createChild({}, (err, stdout, stderr) => {
    assert.strictEqual(err, error);
    assert.strictEqual(stdout, '');
    assert.strictEqual(stderr, '');
    done();
  });

  child.emit('error', error);
});

test('execution with a killed process is handled', (_, done) => {
  createChild({ timeout: 1 }, (err, stdout, stderr) => {
    assert.strictEqual(err.killed, true);
    assert.strictEqual(stdout, '');
    assert.strictEqual(stderr, '');
    done();
  });
});
