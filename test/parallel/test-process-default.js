'use strict';
const common = require('../common');
const assert = require('node:assert');

process.default = 1;
import('node:process').then(common.mustCall((processModule) => {
  assert.strictEqual(processModule.default.default, 1);
}));
