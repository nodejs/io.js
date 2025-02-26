'use strict';

// This tests that forgetting shortCircuit: true in the return value of evaluate()
// hook without calling nextEvalaute causes an error.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.deepStrictEqual(context.module.exports, {});
    context.module.exports = { skipped: true };
  }, 1),
});

assert.throws(() => {
  require('../fixtures/module-hooks/prop-exports.cjs');
}, {
  code: 'ERR_INVALID_RETURN_PROPERTY_VALUE',
});

hook.deregister();
