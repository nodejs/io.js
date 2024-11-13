// This tests that after failing to require an ESM that contains TLA,
// retrying with import() still works, and produces consistent results.
'use strict';
const common = require('../common');
const assert = require('assert');

const { exportedResolve } = require('../fixtures/es-modules/tla/export-promise.mjs');

assert.throws(() => {
  require('../fixtures/es-modules/tla/await-export-promise.mjs');
}, {
  code: 'ERR_REQUIRE_ASYNC_MODULE'
});

const interval = setInterval(() => {}, 1000);  // Keep the test running, because await alone doesn't.
const value = { hello: 'world' };

const p1 = import('../fixtures/es-modules/tla/await-export-promise.mjs').then(common.mustCall((ns) => {
  assert.strictEqual(ns.default, value);
}), common.mustNotCall());

const p2 = import('../fixtures/es-modules/tla/await-export-promise.mjs').then(common.mustCall((ns) => {
  assert.strictEqual(ns.default, value);
}), common.mustNotCall());

const p3 = import('../fixtures/es-modules/tla/await-export-promise.mjs').then(common.mustCall((ns) => {
  assert.strictEqual(ns.default, value);
}), common.mustNotCall());

exportedResolve(value);

Promise.all([p1, p2, p3]).then(common.mustCall(() => { clearInterval(interval); }));
