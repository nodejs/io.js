'use strict';

// This tests that the evaluate() can skip default evaluation.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.deepStrictEqual(context.module.exports, {});
    context.module.exports = { prop: 'updated' };
    return {
      shortCircuit: true,
    };
  }, 1),
});

assert.deepStrictEqual(require('../fixtures/module-hooks/throws.cjs'), { prop: 'updated' });

hook.deregister();
