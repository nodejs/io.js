'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

registerHooks({
  evaluate(context, nextEvaluate) {
    nextEvaluate(context);
    // It should not be possible to call defaultEvaluate on the same module again.
    return nextEvaluate(context);
  },
});

// Check both require(cjs) and require(esm).
assert.throws(() => {
  require('../fixtures/module-hooks/node_modules/bar');
}, { code: 'ERR_MODULE_ALREADY_EVALUATED' });
assert.throws(() => {
  require('../fixtures/module-hooks/node_modules/bar-esm');
}, { code: 'ERR_MODULE_ALREADY_EVALUATED' });
