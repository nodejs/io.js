'use strict';

// This tests that the evaluate() can replace entire exports of require(cjs)
// which does affect the internal access in the module.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
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
  }, 1),
});

// If the internal access is done as a receiver access, it can still be updated.
const result = require('../fixtures/module-hooks/prop-exports.cjs');
assert.strictEqual(result.prop, 'updated');
assert.strictEqual(result.getProp(), 'updated');

hook.deregister();
