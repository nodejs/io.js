'use strict';

// This tests that the lower bits of mode > 0o777 still works in
// process.umask()

const common = require('../common');
const assert = require('assert');
const { isMainThread } = require('worker_threads');

if (!isMainThread)
  common.skip('Setting process.umask is not supported in Workers');

let mask;

if (common.isWindows) {
  mask = 0o600;
} else {
  mask = 0o664;
}

const maskToIgnore = 0o10000;

const old = process.umask();

function test(input, output) {
  process.umask(input);
  assert.strictEqual(process.umask(), output);

  process.umask(old);
}

test(mask | maskToIgnore, mask);
test((mask | maskToIgnore).toString(8), mask);
