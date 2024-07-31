'use strict';

const common = require('../common');
const Countdown = require('../common/countdown');
const assert = require('assert');

// Verify that unhandled rejections always trigger uncaught exceptions instead
// of triggering unhandled rejections.

const err1 = new Error('One');
const err2 = new Error(
  'This error originated either by throwing ' +
  'inside of an async function without a catch block, or by rejecting a ' +
  'promise which was not handled with .catch(). The promise rejected with the' +
  ' reason "null".'
);
err2.code = 'ERR_UNHANDLED_REJECTION';
Object.defineProperty(err2, 'name', {
  value: 'UnhandledPromiseRejection',
  writable: true,
  configurable: true
});

const errors = [err1, err2];
const identical = [true, false];

const ref = new Promise(() => {
  throw err1;
});
// Explicitly reject `null`.
Promise.reject(null);

process.on('warning', common.mustNotCall('warning'));
// If we add an unhandledRejection handler, the exception won't be thrown
// process.on('unhandledRejection', common.mustCall(2));
process.on('rejectionHandled', common.mustNotCall('rejectionHandled'));
process.on('exit', assert.strictEqual.bind(null, 0));

const timer = setTimeout(() => console.log(ref), 1000);

const counter = new Countdown(2, () => {
  clearTimeout(timer);
});

process.on('uncaughtException', common.mustCall((err, origin) => {
  counter.dec();
  assert.strictEqual(origin, 'unhandledRejection', err);
  const knownError = errors.shift();
  assert.strictEqual(err.name, knownError.name);
  assert.strictEqual(err.toString(), knownError.toString());
  assert.strictEqual(err.code, knownError.code);
  // Check if the errors are reference equal.
  assert(identical.shift() ? err === knownError : err !== knownError);
}, 2));
