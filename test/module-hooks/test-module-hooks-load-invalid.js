'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// This tests that the invalid return values in load hooks are not accepted.

const hook = registerHooks({
  resolve(specifier, context, nextResolve) {
    return { shortCircuit: true, url: `test://${specifier}` };
  },
  load(url, context, nextLoad) {
    const result = { shortCircuit: true };
    if (url.endsWith('array')) {
      result.source = [];
    } else if (url.endsWith('null')) {
      result.source = null;
    } else if (url.endsWith('number')) {
      result.source = 1;
    } else if (url.endsWith('boolean')) {
      result.source = true;
    } else if (url.endsWith('function')) {
      result.source = () => {};
    } else if (url.endsWith('object')) {
      result.source = {};
    }
    return result;
  },
});

for (const item of ['undefined', 'array', 'null', 'number', 'boolean', 'function', 'object']) {
  assert.throws(() => { require(item); }, {
    code: 'ERR_INVALID_RETURN_PROPERTY_VALUE',
    message: /"source" from the "load" hook/,
  });
}

hook.deregister();
