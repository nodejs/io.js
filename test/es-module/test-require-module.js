// Flags: --experimental-require-module
'use strict';

const common = require('../common');
const assert = require('assert');

// Test named exports.
{
  const mod = require('../fixtures/es-module-loaders/module-named-exports.mjs');
  common.expectRequiredModule(mod, { foo: 'foo', bar: 'bar' });
}

// Test ESM that import ESM.
{
  const mod = require('../fixtures/es-modules/import-esm.mjs');
  common.expectRequiredModule(mod, { hello: 'world' });
}

// Test ESM that import CJS.
{
  const mod = require('../fixtures/es-modules/cjs-exports.mjs');
  common.expectRequiredModule(mod, { });
}

// Test ESM that require() CJS.
{
  const mjs = require('../common/index.mjs');
  // Only comparing a few properties because the ESM version of test/common doesn't
  // re-export everything from the CJS version.
  assert.strictEqual(common.mustCall, mjs.mustCall);
  assert.strictEqual(common.localIPv6Hosts, mjs.localIPv6Hosts);
}

// Test "type": "module" and "main" field in package.json.
// Also, test default export.
{
  const mod = require('../fixtures/es-modules/package-type-module');
  common.expectRequiredModule(mod, { default: 'package-type-module' });
}

// Test data: import.
{
  const mod = require('../fixtures/es-modules/data-import.mjs');
  common.expectRequiredModule(mod, {
    data: { hello: 'world' },
    id: 'data:text/javascript,export default %7B%20hello%3A%20%22world%22%20%7D'
  });
}
