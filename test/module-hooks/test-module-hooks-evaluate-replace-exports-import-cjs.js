'use strict';

// This tests that the evaluate() can replace entire exports of import cjs
// which does affect the internal access in the module.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    if (context.format === 'module') {  // Invoked for require(esm) call.
      assert.match(context.module.filename, /import-prop-exports-cjs\.mjs/);
      nextEvaluate();
      return;
    }
    assert.match(context.module.filename, /prop-exports\.cjs/);
    assert.deepStrictEqual(context.module.exports, {});
    nextEvaluate();
    const originalExports = context.module.exports;
    assert(typeof originalExports.getProp, 'function');
    assert.deepStrictEqual(originalExports, { prop: 'original', getProp: originalExports.getProp });
    context.module.exports = {
      ...originalExports,
      set prop(val) { originalExports.prop = val; },
      get prop() { return originalExports.prop; },
    };
    context.module.exports.prop = 'updated';
  }, 2),
});

const result = require('../fixtures/module-hooks/import-prop-exports-cjs.mjs');
assert.strictEqual(result.prop, 'updated');
assert.strictEqual(result.getProp(), 'updated');

hook.deregister();
