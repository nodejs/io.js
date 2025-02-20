'use strict';

// This tests that the nextEvaluate() returns the top-level return value, if any.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.strictEqual(context.format, 'commonjs');
    assert.match(context.module.filename, /top-level-return\.js/);
    const { returned } = nextEvaluate();
    assert.strictEqual(returned, 'top-level-return');
    // Top-level return does not affect module.exports.
    assert.deepStrictEqual(context.module.exports, {});
    return { returned };
  }, 1),
});

// Top-level return does not affect module.exports.
assert.deepStrictEqual(require('../fixtures/top-level-return'), {});

hook.deregister();
