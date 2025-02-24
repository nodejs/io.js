// This tests that the nextEvaluate() can affect internal access to
// the exports object for CommonJS modules from import cjs from ESM.

import * as common from '../common/index.mjs';
import assert from 'node:assert';
import { registerHooks } from 'node:module';

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    nextEvaluate();
    // Invoked only once for the imported CJS module. import(esm) does not
    // trigger evaluate for now.
    assert.match(context.module.filename, /prop-exports\.cjs/);
    assert.strictEqual(context.module.exports.prop, 'original');
    context.module.exports.prop = 'updated';
  }, 1),
});

const { getProp } = await import('../fixtures/module-hooks/import-prop-exports-cjs.mjs');
assert.strictEqual(getProp(), 'updated');
hook.deregister();
