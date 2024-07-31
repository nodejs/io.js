'use strict';

const common = require('../common');

const timer = require('node:timers');
const timerPromises = require('node:timers/promises');
const assert = require('assert');
const { test } = require('node:test');

test('(node:timers/promises) is equal to (node:timers).promises', common.mustCall(() => {
  assert.deepStrictEqual(timerPromises, timer.promises);
}));
