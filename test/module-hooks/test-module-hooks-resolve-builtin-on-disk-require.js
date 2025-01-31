'use strict';

require('../common');

const assert = require('assert');
const { registerHooks } = require('module');
const fixtures = require('../common/fixtures');

// This tests that builtins can be redirected to a local file.
// Pick a builtin that's unlikely to be loaded already - like zlib.
assert(!process.moduleLoadList.includes('NativeModule zlib'));

const hook = registerHooks({
  resolve(specifier, context, nextLoad) {
    return {
      url: fixtures.fileURL('module-hooks', `redirected-${specifier}.js`).href,
      shortCircuit: true,
    };
  },
});

// Check assert, which is already loaded.
assert.strictEqual(require('assert').exports_for_test, 'redirected assert');
// Check zlib, which is not yet loaded.
assert.strictEqual(require('zlib').exports_for_test, 'redirected zlib');
// Check fs, which is redirected to an ESM
assert.strictEqual(require('fs').exports_for_test, 'redirected fs');

hook.deregister();
