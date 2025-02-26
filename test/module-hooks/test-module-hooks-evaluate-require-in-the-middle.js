'use strict';

require('../common');
const assert = require('assert');
const { load, hook } = require('../fixtures/module-hooks/load-with-require-in-the-middle');

// This is a basic mock of https://www.npmjs.com/package/require-in-the-middle
// to test that the module.registerHooks() can be used to replace the
// CJS loader monkey-patching approach.

const foo = load('foo');
assert.deepStrictEqual(foo, { '$key': 'foo', '_version': '1.0.0' });

hook.unhook();

const bar = load('bar');
assert.deepStrictEqual(bar, { '$key': 'bar' });
