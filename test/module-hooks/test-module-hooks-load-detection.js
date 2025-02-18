'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// Test that module syntax detection works.
const hook = registerHooks({
  load(url, context, nextLoad) {
    const result = nextLoad(url, context);
    assert.strictEqual(result.source, '');
    return {
      source: 'export const hello = "world"',
    };
  },
});

const mod = require('../fixtures/empty.js');
assert.strictEqual(mod.hello, 'world');

hook.deregister();
