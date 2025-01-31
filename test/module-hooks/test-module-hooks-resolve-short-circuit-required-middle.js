'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that shortCircuit is required in a middle hook when nextResolve is not called.
const hook1 = registerHooks({
  resolve(specifier, context, nextResolve) {
    return nextResolve(specifier, context);
  },
});
const hook2 = registerHooks({
  resolve(specifier, context, nextResolve) {
    if (specifier === 'bar') {
      return {
        url: 'node:bar',
      };
    }
    return nextResolve(specifier, context);
  },
});

assert.throws(() => {
  require('bar');
}, {
  code: 'ERR_INVALID_RETURN_PROPERTY_VALUE',
  message: /shortCircuit/,
});

hook1.deregister();
hook2.deregister();
