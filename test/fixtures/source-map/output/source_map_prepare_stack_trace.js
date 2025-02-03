// Flags: --enable-source-maps

'use strict';
require('../../../common');
const assert = require('node:assert');
const Module = require('node:module');
Error.stackTraceLimit = 5;

assert.strictEqual(typeof Error.prepareStackTrace, 'function');
const defaultPrepareStackTrace = Error.prepareStackTrace;
Error.prepareStackTrace = (error, trace) => {
  trace = trace.filter(it => {
    return it.getFunctionName() !== 'functionC';
  });
  return defaultPrepareStackTrace(error, trace);
};

try {
  require('../enclosing-call-site-min.js');
} catch (e) {
  console.log(e);
}

// Source maps support is disabled programmatically even without deleting the
// CJS module cache.
Module.setSourceMapsSupport(false);
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
