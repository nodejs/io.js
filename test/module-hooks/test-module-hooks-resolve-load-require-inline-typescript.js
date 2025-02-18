'use strict';
// Flags: --no-experimental-strip-types --no-experimental-transform-types
// This tests that a mini TypeScript loader works with resolve and
// load hooks when TypeScript support is disabled.

require('../common');
const assert = require('assert');

// Test inline require().
require('../fixtures/module-hooks/register-typescript-hooks.js');
const { UserAccount, UserType } = require('../fixtures/module-hooks/user.ts');
assert.strictEqual((new UserAccount('foo', 1, UserType.Admin).name), 'foo');
