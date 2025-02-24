'use strict';

// This tests that the evaluate() can replace entire exports of require(esm)
// which does not affect the internal access in the module.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.deepStrictEqual(context.module.exports, {});
    nextEvaluate();
    const originalExports = context.module.exports;
    assert(typeof originalExports.getProp, 'function');
    const expected = { prop: 'original', getProp: originalExports.getProp };
    common.expectRequiredModule(originalExports, { ...expected, default: expected });
    context.module.exports = { ...expected, prop: 'updated' };
  }, 1),
});

const result = require('../fixtures/module-hooks/prop-exports.mjs');
assert.strictEqual(result.prop, 'updated');
assert.strictEqual(result.getProp(), 'original');

hook.deregister();
