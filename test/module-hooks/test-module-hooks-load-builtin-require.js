'use strict';

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// This tests that required builtins get null as source from default
// step, and the source returned are ignored.
// TODO(joyeecheung): this is to align with the module.register() behavior
// but perhaps the load hooks should not be invoked for builtins at all.

// Pick a builtin that's unlikely to be loaded already - like zlib.
assert(!process.moduleLoadList.includes('NativeModule zlib'));

const hook = registerHooks({
  load: common.mustCall(function load(url, context, nextLoad) {
    assert.strictEqual(url, 'node:zlib');
    const result = nextLoad(url, context);
    assert.strictEqual(result.source, null);
    return {
      source: 'throw new Error("I should not be thrown")',
      format: 'builtin',
    };
  }),
});

assert.strictEqual(typeof require('zlib').createGzip, 'function');

hook.deregister();
