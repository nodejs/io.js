'use strict';

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that shortCircuit works for the resolve hook.
const source1 = 'module.exports = "modified"';
const hook1 = registerHooks({
  load: common.mustNotCall(),
});
const hook2 = registerHooks({
  load(url, context, nextLoad) {
    if (url.includes('empty')) {
      return {
        format: 'commonjs',
        source: source1,
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
