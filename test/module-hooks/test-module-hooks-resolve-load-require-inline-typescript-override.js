'use strict';
// Flags: --no-experimental-transform-types
// This tests that a mini TypeScript loader works with resolve and
// load hooks when overriding --experimental-strip-types in CJS.

require('../common');
const assert = require('assert');

require('../fixtures/module-hooks/register-typescript-hooks.js');
// Test inline require(), if override fails, this should fail too because enum is
// not supported when --experimental-transform-types is disabled.
const { UserAccount, UserType } = require('../fixtures/module-hooks/user.ts');
assert.strictEqual((new UserAccount('foo', 1, UserType.Admin).name), 'foo');
