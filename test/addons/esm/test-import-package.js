// Flags: --experimental-addon-modules
'use strict';
const common = require('../../common');
const assert = require('node:assert');

/**
 * Test that the export condition `node-addons` can be used
 * with `*.node` files with the ESM loader.
 */

import('esm-package/binding')
  .then((mod) => {
    assert.strictEqual(mod.default.hello(), 'world');
  })
  .then(common.mustCall());
