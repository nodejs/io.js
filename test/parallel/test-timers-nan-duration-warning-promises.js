'use strict';

const common = require('../common');
const assert = require('assert');
const { setTimeout } = require('timers/promises');

process.once('warning', common.mustCall((warning) => {
  assert.strictEqual(warning.name, 'TimeoutNaNWarning');
}));

setTimeout(NaN).then(common.mustCall(), common.mustNotCall());
