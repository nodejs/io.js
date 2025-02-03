'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that shortCircuit is required in a middle hook when nextLoad is not called.
const hook1 = registerHooks({
  load(url, context, nextLoad) {
    return nextLoad(url, context);
  },
});
const hook2 = registerHooks({
  load(url, context, nextLoad) {
    if (url.includes('empty')) {
      return {
        format: 'commonjs',
        source: 'module.exports = "modified"',
      };
    }
    return nextLoad(url, context);
  },
});

assert.throws(() => {
  require('../fixtures/empty.js');
}, {
  code: 'ERR_INVALID_RETURN_PROPERTY_VALUE',
  message: /shortCircuit/,
});

hook1.deregister();
hook2.deregister();
