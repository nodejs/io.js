'use strict';

// This tests that the context of the evaluate hooks cannot be modified or
// overriden when being passed to the nextEvaluate hook.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook1 = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    // context.format is still module, unaffected by hook 2.
    assert.strictEqual(context.format, 'module');
    assert.match(context.module.filename, /bar-esm\.js/);

    // Cannot change the format.
    const originalFormat = context.format;
    assert.throws(() => {
      context.format = 'commonjs';
    }, { name: 'TypeError' });
    assert.strictEqual(context.format, originalFormat);
    return nextEvaluate();
  }, 1),
});

const hook2 = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.strictEqual(context.format, 'module');
    assert.match(context.module.filename, /bar-esm\.js/);

    // Cannot change the format.
    const originalFormat = context.format;
    assert.throws(() => {
      context.format = 'commonjs';
    }, { name: 'TypeError' });
    assert.strictEqual(context.format, originalFormat);
    // Does not take the format from the argument.
    return nextEvaluate({ ...context, format: 'commonjs' });
  }, 1),
});

common.expectRequiredModule(
  require('../fixtures/module-hooks/node_modules/bar-esm'),
  { '$key': 'bar-esm' });

hook2.deregister();
hook1.deregister();
