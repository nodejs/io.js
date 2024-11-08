// This tests that after failing to require an ESM that contains TLA,
// retrying with import() still works, and produces consistent results.
'use strict';
const common = require('../common');
const assert = require('assert');

let ns;
async function test() {
  assert.throws(() => {
    require('../fixtures/es-modules/tla/export-async.mjs');
  }, {
    code: 'ERR_REQUIRE_ASYNC_MODULE'
  });
  const newNs = await import('../fixtures/es-modules/tla/export-async.mjs');
  if (ns === undefined) {
    ns = newNs;
  } else {
    // Check that re-evalaution is returning the same namespace.
    assert.strictEqual(ns, newNs);
  }
  assert.strictEqual(ns.hello, 'world');
}

// Run the test twice to check consistency after caching.
test().then(common.mustCall(test)).catch(common.mustNotCall());
