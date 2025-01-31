import { mustCall } from '../common/index.mjs';
import assert from 'node:assert';
import { registerHooks } from 'node:module';
import process from 'node:process';

// This tests that imported builtins get null as source from default
// step, and the source returned are ignored.
// TODO(joyeecheung): this is to align with the module.register() behavior
// but perhaps the load hooks should not be invoked for builtins at all.

// Pick a builtin that's unlikely to be loaded already - like zlib.
assert(!process.moduleLoadList.includes('NativeModule zlib'));

const hook = registerHooks({
  load: mustCall(function load(url, context, nextLoad) {
    assert.strictEqual(url, 'node:zlib');
    const result = nextLoad(url, context);
    assert.strictEqual(result.source, null);
    return {
      source: 'throw new Error("I should not be thrown")',
      format: 'builtin',
    };
  }),
});

const ns = await import('node:zlib');
assert.strictEqual(typeof ns.createGzip, 'function');

hook.deregister();
