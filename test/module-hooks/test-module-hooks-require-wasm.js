'use strict';

// This tests that module.registerHooks() can be used to support unknown formats, like
// require(wasm) and import(wasm)
const common = require('../common');

const assert = require('assert');
const { registerHooks } = require('module');
const { readFileSync } = require('fs');

registerHooks({
  load(url, context, nextLoad) {
    assert.match(url, /simple\.wasm$/);
    const source =
      `const buf = Buffer.from([${Array.from(readFileSync(new URL(url))).join(',')}]);
       const compiled = new WebAssembly.Module(buf);
       module.exports = (new WebAssembly.Instance(compiled)).exports;`;
    return {
      shortCircuit: true,
      source,
      format: 'commonjs',
    };
  },
});

// Checks that it works with require.
const { add } = require('../fixtures/simple.wasm');
assert.strictEqual(add(1, 2), 3);

(async () => {   // Checks that it works with import.
  const { default: { add } } = await import('../fixtures/simple.wasm');
  assert.strictEqual(add(1, 2), 3);
})().then(common.mustCall());
