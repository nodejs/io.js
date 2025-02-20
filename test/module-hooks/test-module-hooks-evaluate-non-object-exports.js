'use strict';

// This tests that the evaluate() can deal with non-object exports.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.deepStrictEqual(context.module.exports, {});
    nextEvaluate();
    // module.exports is evalauted to be replaced by a string.
    assert.strictEqual(context.module.exports, 'perhaps I work');
    context.module.exports = { changed: true };  // Change it into an object again.
    return { };
  }, 1),
});

assert.deepStrictEqual(require('../fixtures/baz.js'), { changed: true });

hook.deregister();
