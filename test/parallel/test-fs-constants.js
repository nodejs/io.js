'use strict';
const { expectWarning } = require('../common');
const fs = require('fs');
const assert = require('assert');

// Check if the two constants accepted by chmod() on Windows are defined.
assert.notStrictEqual(fs.constants.S_IRUSR, undefined);
assert.notStrictEqual(fs.constants.S_IWUSR, undefined);

// Check for runtime deprecation warning, there should be no setter
const { F_OK, R_OK, W_OK, X_OK } = fs.constants;

assert.throws(() => { fs.F_OK = 'overwritten'; }, { name: 'TypeError' });
assert.throws(() => { fs.R_OK = 'overwritten'; }, { name: 'TypeError' });
assert.throws(() => { fs.W_OK = 'overwritten'; }, { name: 'TypeError' });
assert.throws(() => { fs.X_OK = 'overwritten'; }, { name: 'TypeError' });

expectWarning(
  'DeprecationWarning',
  'fs.F_OK is deprecated, use fs.constants.F_OK instead',
  'DEP0176'
);

assert.strictEqual(fs.F_OK, F_OK);
assert.strictEqual(fs.R_OK, R_OK);
assert.strictEqual(fs.W_OK, W_OK);
assert.strictEqual(fs.X_OK, X_OK);
