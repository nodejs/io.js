import '../common/index.mjs';
import assert from 'node:assert';
import { registerHooks } from 'node:module';
import process from 'node:process';

// This tests that builtins can be redirected to another builtin.
// Pick a builtin that's unlikely to be loaded already - like zlib.
assert(!process.moduleLoadList.includes('NativeModule zlib'));

const hook = registerHooks({
  resolve(specifier, context, nextLoad) {
    if (specifier === 'node:assert') {
      return {
        url: 'node:zlib',
        shortCircuit: true,
      };
    }
  },
});


// Check assert, which is already loaded.
// zlib.createGzip is a function.
const redirected = await import('node:assert');
assert.strictEqual(typeof redirected.createGzip, 'function');

hook.deregister();
