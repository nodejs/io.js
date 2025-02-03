'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that shortCircuit is required in the starting hook when nextResolve is not called.
const hook = registerHooks({
  resolve(specifier, context, nextResolve) {
    if (specifier === 'foo') {
      return {
        url: 'node:foo',
      };
    }
    return nextResolve(specifier, context);
  },
});

assert.throws(() => {
  require('foo');
}, {
  code: 'ERR_INVALID_RETURN_PROPERTY_VALUE',
  message: /shortCircuit/,
});

const baz = require('../fixtures/baz.js');
assert.strictEqual(baz, 'perhaps I work');
hook.deregister();
