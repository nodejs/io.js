'use strict';

// This tests that the nextEvaluate() can affect internal access to
// the exports object for CommonJS modules from import cjs.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    nextEvaluate();
    if (context.format === 'module') {  // Invoked for require(esm) call.
      assert.match(context.module.filename, /import-prop-exports-cjs\.mjs/);
      return;
    }
    // Invoked for the import cjs call.
    assert.match(context.module.filename, /prop-exports\.cjs/);
    assert.strictEqual(context.module.exports.prop, 'original');
    context.module.exports.prop = 'updated';
  }, 2),
});

const { getProp } = require('../fixtures/module-hooks/import-prop-exports-cjs.mjs');
assert.strictEqual(getProp(), 'updated');
hook.deregister();
