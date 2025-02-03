// Flags: --experimental-addon-modules
'use strict';
require('../../common');
const assert = require('node:assert');

/**
 * Test that the export condition `node-addons` can be used
 * with `*.node` files with the CJS loader.
 */

const mod = require('esm-package/binding');
assert.strictEqual(mod.hello(), 'world');
