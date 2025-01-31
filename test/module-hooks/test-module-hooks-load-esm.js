'use strict';

// This tests a pirates-like load hook works.

const common = require('../common');
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

(async () => {
  const revert = addHook(hook, { exts: ['.js'], matcher });

  {
    const foo = await loader.import('foo-esm');
    const filename = fixtures.path('module-hooks', 'node_modules', 'foo-esm', 'foo-esm.js');
    assert.deepStrictEqual(matcherArgs, [filename]);
    const code = readFileSync(filename, 'utf-8');
    assert.deepStrictEqual(hookArgs, [{ code, filename }]);
    assert.deepStrictEqual({ ...foo }, { hello: 'foo-esm' });
  }

  matcherArgs.splice(0, 1);
  hookArgs.splice(0, 1);

  revert();

  // Later loads are unaffected.

  {
    const bar = await loader.import('bar-esm');
    assert.deepStrictEqual(matcherArgs, []);
    assert.deepStrictEqual(hookArgs, []);
    assert.deepStrictEqual({ ...bar }, { $key: 'bar-esm' });
  }

})().catch(common.mustNotCall());
