'use strict';

// This tests that the nextEvaluate() can affect internal access to
// the exports object for CommonJS modules.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    nextEvaluate();
    assert.strictEqual(context.module.exports.prop, 'original');
    context.module.exports.prop = 'updated';
  }, 1),
});

const { getProp } = require('../fixtures/module-hooks/prop-exports.cjs');
assert.strictEqual(getProp(), 'updated');
hook.deregister();
