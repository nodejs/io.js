// This tests that the evaluate() can replace entire exports of import cjs from ESM
// which does affect the internal access in the module.

import * as common from '../common/index.mjs';
import assert from 'node:assert';
import { registerHooks } from 'node:module';

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    // Invoked only once for the imported CJS module. import(esm) does not
    // trigger evaluate for now.
    assert.match(context.module.filename, /prop-exports\.cjs/);
    assert.deepStrictEqual(context.module.exports, {});
    nextEvaluate();
    const originalExports = context.module.exports;
    assert(typeof originalExports.getProp, 'function');
    assert.deepStrictEqual(originalExports, { prop: 'original', getProp: originalExports.getProp });
    context.module.exports = {
      ...originalExports,
      set prop(val) { originalExports.prop = val; },
      get prop() { return originalExports.prop; },
    };
    context.module.exports.prop = 'updated';
  }, 1),
});

const result = await import('../fixtures/module-hooks/import-prop-exports-cjs.mjs');
assert.strictEqual(result.prop, 'updated');
assert.strictEqual(result.getProp(), 'updated');

hook.deregister();
