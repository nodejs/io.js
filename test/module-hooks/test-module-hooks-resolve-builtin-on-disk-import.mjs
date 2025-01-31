import '../common/index.mjs';
import { fileURL } from '../common/fixtures.mjs';
import assert from 'node:assert';
import { registerHooks } from 'node:module';
import process from 'node:process';

// This tests that builtins can be redirected to a local file.
// Pick a builtin that's unlikely to be loaded already - like zlib.
assert(!process.moduleLoadList.includes('NativeModule zlib'));

const hook = registerHooks({
  resolve(specifier, context, nextLoad) {
    // FIXME(joyeecheung): when it gets redirected to a CommonJS module, the
    // ESM loader invokes the CJS loader with the resolved URL again even when
    // it already has the url and source code. Fix it so that the hooks are
    // skipped during the second loading.
    if (!specifier.startsWith('node:')) {
      return nextLoad(specifier, context);
    }
    return {
      url: fileURL(
        'module-hooks',
        `redirected-${specifier.replace('node:', '')}.js`).href,
      shortCircuit: true,
    };
  },
});

// Check assert, which is already loaded.
assert.strictEqual((await import('node:assert')).exports_for_test, 'redirected assert');
// Check zlib, which is not yet loaded.
assert.strictEqual((await import('node:zlib')).exports_for_test, 'redirected zlib');
// Check fs, which is redirected to an ESM
assert.strictEqual((await import('node:fs')).exports_for_test, 'redirected fs');

hook.deregister();
