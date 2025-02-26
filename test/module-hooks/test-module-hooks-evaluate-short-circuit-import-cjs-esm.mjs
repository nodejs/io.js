// This tests that the evaluate() can skip default evaluation for import cjs from ESM.

import * as common from '../common/index.mjs';
import assert from 'node:assert';
import { registerHooks } from 'node:module';

const hook = registerHooks({
  evaluate: common.mustCall(function(context, nextEvaluate) {
    // Only invoked for the import cjs call.
    assert.deepStrictEqual(context.module.exports, {});
    context.module.exports = { prop: 'updated' };
    return {
      shortCircuit: true,
    };
  }, 1),
});

const result = await import('../fixtures/module-hooks/import-throws-cjs.mjs');
assert.strict(result.prop, 'updated');

hook.deregister();
