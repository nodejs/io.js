'use strict';

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that shortCircuit is required in a middle hook when nextResolve is not called.
const hook1 = registerHooks({
  load: common.mustNotCall(),
});
const hook2 = registerHooks({
  load(url, context, nextLoad) {
    if (url.includes('empty')) {
      return {
        format: 'commonjs',
        source: 'module.exports = "modified"',
        shortCircuit: true,
      };
    }
    return nextLoad(url, context);
  },
});

const value = require('../fixtures/empty.js');
assert.strictEqual(value, 'modified');

hook1.deregister();
hook2.deregister();
