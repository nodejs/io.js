// Flags: --enable-source-maps

'use strict';
require('../../../common');
const assert = require('assert');
Error.stackTraceLimit = 5;

assert.strictEqual(process.sourceMapsEnabled, true);
process.setSourceMapsEnabled(false);
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
process.setSourceMapsEnabled(true);
assert.strictEqual(process.sourceMapsEnabled, true);

try {
  require('../enclosing-call-site-min.js');
} catch (e) {
  console.log(e);
}
