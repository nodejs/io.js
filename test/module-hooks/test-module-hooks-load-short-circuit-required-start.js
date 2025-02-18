'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that shortCircuit is required in the starting hook when nextLoad is not called.
const hook = registerHooks({
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

const baz = require('../fixtures/baz.js');
assert.strictEqual(baz, 'perhaps I work');
hook.deregister();
