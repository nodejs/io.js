'use strict';
const common = require('../common');
const assert = require('assert');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('process.env.TZ is not intercepted in Workers');
}

if (common.isWindows) { // Using a different TZ format.
  common.skip('todo: test on Windows');
}

const date = new Date('2018-04-14T12:34:56.789Z');

process.env.TZ = 'Europe/Amsterdam';

if (date.toString().includes('(Europe)'))
  common.skip('not using bundled ICU');  // Shared library or --with-intl=none.

if ('Sat Apr 14 2018 12:34:56 GMT+0000 (GMT)' === date.toString())
  common.skip('missing tzdata');  // Alpine buildbots lack Europe/Amsterdam.

if (date.toString().includes('(Central European Time)') ||
    date.toString().includes('(CET)')) {
  // The AIX and SmartOS buildbots report 2018 CEST as CET
  // because apparently for them that's still the deep future.
  common.skip('tzdata too old');
}

// Text representation of timezone depends on locale in environment
assert.match(
  date.toString(),
  /^Sat Apr 14 2018 14:34:56 GMT\+0200 \(.+\)$/);

process.env.TZ = 'Europe/London';
assert.match(
  date.toString(),
  /^Sat Apr 14 2018 13:34:56 GMT\+0100 \(.+\)$/);

process.env.TZ = 'Etc/UTC';
assert.match(
  date.toString(),
  /^Sat Apr 14 2018 12:34:56 GMT\+0000 \(.+\)$/);

// Just check that deleting the environment variable doesn't crash the process.
// We can't really check the result of date.toString() because we don't know
// the default time zone.
delete process.env.TZ;
date.toString();
