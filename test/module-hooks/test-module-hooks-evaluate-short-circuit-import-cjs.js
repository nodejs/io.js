'use strict';

// This tests that the evaluate() can skip default evaluation for import cjs.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    if (!context.module.filename.endsWith('throws.cjs')) {
      return nextEvaluate();
    }
    assert.deepStrictEqual(context.module.exports, {});
    context.module.exports = { prop: 'updated' };
    return {
      shortCircuit: true,
    };
  }, 2),
});

const result = require('../fixtures/module-hooks/import-throws-cjs.mjs');
assert.strictEqual(result.prop, 'updated');

hook.deregister();
