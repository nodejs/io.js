'use strict';

// This tests that the context of the evaluate hooks cannot be modified or
// overriden when being passed to the nextEvaluate hook.

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

const hook1 = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.strictEqual(context.format, 'commonjs');
    // context.module is still bar, unaffected by hook 2.
    assert.match(context.module.filename, /bar\.js/);

    // Cannot change the module.
    const originalModule = context.module;
    assert.throws(() => {
      context.module = module;
    }, { name: 'TypeError' });
    assert.strictEqual(context.module, originalModule);
    return nextEvaluate();
  }, 1),
});

const hook2 = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    assert.strictEqual(context.format, 'commonjs');
    assert.match(context.module.filename, /bar\.js/);

    // Changing the module does nothing.
    const originalModule = context.module;
    assert.throws(() => {
      context.module = module;
    }, { name: 'TypeError' });
    assert.strictEqual(context.module, originalModule);
    // Does not take the module from the argument.
    return nextEvaluate({ ...context, cjsModule: module });
  }, 1),
});

assert.deepStrictEqual(require('../fixtures/module-hooks/node_modules/bar'), { '$key': 'bar' });

hook2.deregister();
hook1.deregister();
