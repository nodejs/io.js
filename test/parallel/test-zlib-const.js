'use strict';
require('../common');
const assert = require('assert');

const zlib = require('zlib');

assert.strictEqual(zlib.constants.Z_OK, 0,
                   [
                     'Expected Z_OK to be 0;',
                     `got ${zlib.constants.Z_OK}`,
                   ].join(' '));

assert.throws(() => { zlib.constants.Z_OK = 1; },
              TypeError, 'zlib.constants.Z_OK should be immutable');

assert.strictEqual(zlib.codes.Z_OK, 0,
                   `Expected Z_OK to be 0; got ${zlib.codes.Z_OK}`);
assert.throws(() => { zlib.codes.Z_OK = 1; },
              TypeError, 'zlib.codes.Z_OK should be immutable');

assert.throws(() => { zlib.codes = { Z_OK: 1 }; },
              TypeError, 'zlib.codes should be immutable');

assert.ok(Object.isFrozen(zlib.codes),
          [
            'Expected zlib.codes to be frozen, but Object.isFrozen',
            `returned ${Object.isFrozen(zlib.codes)}`,
          ].join(' '));
