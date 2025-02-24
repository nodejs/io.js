'use strict';

// This tests that the nextEvaluate() cannot affect internal access to
// the exports object for ESM.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    nextEvaluate();
    assert.throws(() => {
      context.module.exports.prop = 'changed';
    }, { name: 'TypeError' });
    assert.strictEqual(context.module.exports.prop, 'original');
  }, 1),
});

const { getProp } = require('../fixtures/module-hooks/prop-exports.mjs');
assert.strictEqual(getProp(), 'original');
hook.deregister();
