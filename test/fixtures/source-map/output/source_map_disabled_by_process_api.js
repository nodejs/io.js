// Flags: --enable-source-maps

'use strict';
require('../../../common');
const assert = require('node:assert');
const Module = require('node:module');
Error.stackTraceLimit = 5;

assert.strictEqual(process.sourceMapsEnabled, true);
process.setSourceMapsEnabled(false);
assert.strictEqual(process.sourceMapsEnabled, false);
assert.deepStrictEqual(Module.getSourceMapsSupport(), {
  __proto__: null,
  enabled: false,
  nodeModules: false,
  generatedCode: false,
});

try {
  require('../enclosing-call-site-min.js');
} catch (e) {
  console.log(e);
}

// Delete the CJS module cache and loading the module again with source maps
// support enabled programmatically.
delete require.cache[require
  .resolve('../enclosing-call-site-min.js')];
process.setSourceMapsEnabled(true);
assert.strictEqual(process.sourceMapsEnabled, true);
assert.deepStrictEqual(Module.getSourceMapsSupport(), {
  __proto__: null,
  enabled: true,
  nodeModules: true,
  generatedCode: true,
});

try {
  require('../enclosing-call-site-min.js');
} catch (e) {
  console.log(e);
}
