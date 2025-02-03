'use strict';

const common = require('../common');
const assert = require('assert');

const { isMainThread } = require('worker_threads');

if (common.isWindows) {
  assert.strictEqual(process.geteuid, undefined);
  assert.strictEqual(process.getegid, undefined);
  assert.strictEqual(process.seteuid, undefined);
  assert.strictEqual(process.setegid, undefined);
  return;
}

if (!isMainThread) {
  return;
}

assert.throws(() => {
  process.seteuid({});
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'The "id" argument must be one of type number or string. ' +
    'Received an instance of Object'
});

assert.throws(() => {
  process.seteuid('fhqwhgadshgnsdhjsdbkhsdabkfabkveyb');
}, {
  code: 'ERR_UNKNOWN_CREDENTIAL',
  message: 'User identifier does not exist: fhqwhgadshgnsdhjsdbkhsdabkfabkveyb'
});

// IBMi does not support below operations.
if (common.isIBMi)
  return;

// If we're not running as super user...
if (process.getuid() !== 0) {
  // Should not throw.
  process.getegid();
  process.geteuid();

  assert.throws(() => {
    process.setegid('nobody');
  }, /(?:EPERM, .+|Group identifier does not exist: nobody)$/);

  assert.throws(() => {
    process.seteuid('nobody');
  }, /^Error: (?:EPERM, .+|User identifier does not exist: nobody)$/);

  return;
}

// If we are running as super user...
const oldgid = process.getegid();
try {
  process.setegid('nobody');
} catch (err) {
  if (err.message !== 'Group identifier does not exist: nobody') {
    throw err;
  } else {
    process.setegid('nogroup');
  }
}
const newgid = process.getegid();
assert.notStrictEqual(newgid, oldgid);

const olduid = process.geteuid();
process.seteuid('nobody');
const newuid = process.geteuid();
assert.notStrictEqual(newuid, olduid);
