'use strict';
const common = require('../common');

const assert = require('assert');
const cp = require('child_process');

function fail(proc, args, code = 'ERR_INVALID_ARG_TYPE') {
  assert.throws(() => {
    proc.send.apply(proc, args);
  }, { code, name: 'TypeError' });
}

let target = process;

if (process.argv[2] !== 'child') {
  target = cp.fork(__filename, ['child']);
  target.on('exit', common.mustCall((code, signal) => {
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  }));
}

fail(target, ['msg', null, null]);
fail(target, ['msg', null, '']);
fail(target, ['msg', null, 'foo']);
fail(target, ['msg', null, 0]);
fail(target, ['msg', null, NaN]);
fail(target, ['msg', 'meow', undefined], 'ERR_INVALID_HANDLE_TYPE');
fail(target, ['msg', null, 1]);
fail(target, ['msg', null, null, common.mustNotCall()]);
