// This tests that module.registerHooks() can be used to support unknown formats, like
// import(wasm)
import '../common/index.mjs';

import assert from 'node:assert';
import { registerHooks, createRequire } from 'node:module';
import { readFileSync } from 'node:fs';

registerHooks({
  load(url, context, nextLoad) {
    assert.match(url, /simple\.wasm$/);
    const source =
      `const buf = Buffer.from([${Array.from(readFileSync(new URL(url))).join(',')}]);
       const compiled = new WebAssembly.Module(buf);
       const { exports } = new WebAssembly.Instance(compiled);
       export default exports;
       export { exports as 'module.exports' };
    `;
    return {
      shortCircuit: true,
      source,
      format: 'module',
    };
  },
});

// Checks that it works with require.
const require = createRequire(import.meta.url);
const { add } = require('../fixtures/simple.wasm');
assert.strictEqual(add(1, 2), 3);

// Checks that it works with import.
const { default: { add: add2 } } = await import('../fixtures/simple.wasm');
assert.strictEqual(add2(1, 2), 3);
