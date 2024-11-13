// This tests that after loading a ESM with import() and then retrying
// with require(), it errors as expected, and produces consistent results.
'use strict';
const common = require('../common');
const assert = require('assert');

let ns;
async function test() {
  const newNs = await import('../fixtures/es-modules/tla/export-async.mjs');
  if (ns === undefined) {
    ns = newNs;
  } else {
    // Check that re-evalaution is returning the same namespace.
    assert.strictEqual(ns, newNs);
  }
  assert.strictEqual(ns.hello, 'world');

  assert.throws(() => {
    require('../fixtures/es-modules/tla/export-async.mjs');
  }, {
    code: 'ERR_REQUIRE_ASYNC_MODULE'
  });
}

// Run the test twice to check consistency after caching.
test().then(common.mustCall(test)).catch(common.mustNotCall());
