// Flags: --enable-source-maps

'use strict';
require('../../../common');
const assert = require('node:assert');
const Module = require('node:module');
Error.stackTraceLimit = 5;

assert.deepStrictEqual(Module.getSourceMapsSupport(), {
  __proto__: null,
  enabled: true,
  nodeModules: true,
  generatedCode: true,
});
Module.setSourceMapsSupport(false);
assert.deepStrictEqual(Module.getSourceMapsSupport(), {
  __proto__: null,
  enabled: false,
  nodeModules: false,
  generatedCode: false,
});
assert.strictEqual(process.sourceMapsEnabled, false);

try {
  require('../enclosing-call-site-min.js');
} catch (e) {
  console.log(e);
}

// Delete the CJS module cache and loading the module again with source maps
// support enabled programmatically.
delete require.cache[require
  .resolve('../enclosing-call-site-min.js')];
Module.setSourceMapsSupport(true);
assert.deepStrictEqual(Module.getSourceMapsSupport(), {
  __proto__: null,
  enabled: true,
  nodeModules: false,
  generatedCode: false,
});
assert.strictEqual(process.sourceMapsEnabled, true);

try {
  require('../enclosing-call-site-min.js');
} catch (e) {
  console.log(e);
}
