// This synchronous rejections from require(esm) still go to the unhandled rejection
// handler.

'use strict';

const common = require('../common');
const assert = require('assert');

process.on('unhandledRejection', common.mustCall((reason, promise) => {
  assert.strictEqual(reason, 'reject!');
}));

require('../fixtures/es-modules/synchronous-rejection-esm.js');
