'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// This tests that the invalid return values in resolve hooks are not accepted.

const hook = registerHooks({
  resolve(specifier, context, nextLoad) {
    const result = { shortCircuit: true };
    if (specifier === 'array') {
      result.url = [];
    } else if (specifier === 'null') {
      result.url = null;
    } else if (specifier === 'number') {
      result.url = 1;
    } else if (specifier === 'boolean') {
      result.url = true;
    } else if (specifier === 'function') {
      result.url = () => {};
    } else if (specifier === 'object') {
      result.url = {};
    }
    return result;
  },
});

for (const item of ['undefined', 'array', 'null', 'number', 'boolean', 'function', 'object']) {
  assert.throws(() => { require(item); }, {
    code: 'ERR_INVALID_RETURN_PROPERTY_VALUE',
    message: /"url" from the "resolve" hook/,
  });
}

hook.deregister();
