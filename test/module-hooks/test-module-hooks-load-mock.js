'use strict';

// This tests a pirates-like load hook works.

require('../common');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const { readFileSync } = require('fs');

const loader = require('../fixtures/module-hooks/load-from-this-dir');
const { addHook } = require('../fixtures/module-hooks/add-hook');

const matcherArgs = [];
function matcher(filename) {
  matcherArgs.push(filename);
  return true;
}

const hookArgs = [];
function hook(code, filename) {
  hookArgs.push({ code, filename });
  return code.replace('$key', 'hello');
}

const revert = addHook(hook, { exts: ['.js'], matcher });

{
  const foo = loader.require('foo');
  const filename = fixtures.path('module-hooks', 'node_modules', 'foo', 'foo.js');
  assert.deepStrictEqual(matcherArgs, [filename]);
  const code = readFileSync(filename, 'utf-8');
  assert.deepStrictEqual(hookArgs, [{ code, filename }]);
  assert.deepStrictEqual(foo, { hello: 'foo' });
}

matcherArgs.splice(0, 1);
hookArgs.splice(0, 1);

revert();

// Later loads are unaffected.

{
  const bar = loader.require('bar');
  assert.deepStrictEqual(matcherArgs, []);
  assert.deepStrictEqual(hookArgs, []);
  assert.deepStrictEqual(bar, { $key: 'bar' });
}
