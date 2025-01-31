'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// This tests that builtins can be redirected to another builtin.
// Pick a builtin that's unlikely to be loaded already - like zlib.
assert(!process.moduleLoadList.includes('NativeModule zlib'));

const hook = registerHooks({
  resolve(specifier, context, nextLoad) {
    if (specifier === 'assert') {
      return {
        url: 'node:zlib',
        shortCircuit: true,
      };
    }
  },
});

// Check assert, which is already loaded.
// zlib.createGzip is a function.
assert.strictEqual(typeof require('assert').createGzip, 'function');

hook.deregister();
