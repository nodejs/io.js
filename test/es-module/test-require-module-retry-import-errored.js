// This tests that after failing to require an ESM that contains TLA,
// retrying with import() still works, and produces consistent results.
'use strict';
const common = require('../common');
const assert = require('assert');

const { exportedReject } = require('../fixtures/es-modules/tla/export-promise.mjs');

assert.throws(() => {
  require('../fixtures/es-modules/tla/await-export-promise.mjs');
}, {
  code: 'ERR_REQUIRE_ASYNC_MODULE'
});

const interval = setInterval(() => {}, 1000);  // Keep the test running, because await alone doesn't.
const err = new Error('rejected');

const p1 = import('../fixtures/es-modules/tla/await-export-promise.mjs')
  .then(common.mustNotCall(), common.mustCall((e) => {
    assert.strictEqual(e, err);
  }));

const p2 = import('../fixtures/es-modules/tla/await-export-promise.mjs')
  .then(common.mustNotCall(), common.mustCall((e) => {
    assert.strictEqual(e, err);
  }));

const p3 = import('../fixtures/es-modules/tla/await-export-promise.mjs')
  .then(common.mustNotCall(), common.mustCall((e) => {
    assert.strictEqual(e, err);
  }));

exportedReject(err);

Promise.all([p1, p2, p3]).then(common.mustCall(() => { clearInterval(interval); }));
