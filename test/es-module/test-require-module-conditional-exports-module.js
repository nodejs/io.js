// Flags: --experimental-require-module
'use strict';

require('../common');
const assert = require('assert');

const loader = require('../fixtures/es-modules/module-condition/require.cjs');

assert.strictEqual(loader.require('import-module-require').resolved, 'module');
assert.strictEqual(loader.require('module-and-import').resolved, 'module');
assert.strictEqual(loader.require('module-and-require').resolved, 'module');
assert.strictEqual(loader.require('module-import-require').resolved, 'module');
assert.strictEqual(loader.require('module-only').resolved, 'module');
assert.strictEqual(loader.require('module-require-import').resolved, 'module');
assert.strictEqual(loader.require('require-module-import').resolved, 'require');
