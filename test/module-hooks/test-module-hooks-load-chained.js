'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that multiple loaders works together.
const hook1 = registerHooks({
  load(url, context, nextLoad) {
    const result = nextLoad(url, context);
    assert.strictEqual(result.source, '');
    return {
      source: 'exports.hello = "world"',
      format: 'commonjs',
    };
  },
});

const hook2 = registerHooks({
  load(url, context, nextLoad) {
    const result = nextLoad(url, context);
    assert.strictEqual(result.source, 'exports.hello = "world"');
    return {
      source: 'export const hello = "world"',
      format: 'module',
    };
  },
});

const mod = require('../fixtures/empty.js');
assert.strictEqual(mod.hello, 'world');

hook1.deregister();
hook2.deregister();
