// Flags: --experimental-require-module
'use strict';

require('../common');
const assert = require('assert');
const { isModuleNamespaceObject } = require('util/types');

// If only "require" exports are defined, return "require" exports.
{
  const mod = require('../fixtures/es-modules/exports-require-only/load.cjs');
  assert.deepStrictEqual({ ...mod }, { type: 'cjs' });
  assert(!isModuleNamespaceObject(mod));
}

// If only "import" exports are defined, throw ERR_PACKAGE_PATH_NOT_EXPORTED
// instead of falling back to it, because the request comes from require().
assert.throws(() => {
  require('../fixtures/es-modules/exports-import-only/load.cjs');
}, {
  code: 'ERR_PACKAGE_PATH_NOT_EXPORTED'
});

// If both are defined, "require" is used.
{
  const mod = require('../fixtures/es-modules/exports-both/load.cjs');
  assert.deepStrictEqual({ ...mod }, { type: 'cjs' });
  assert(!isModuleNamespaceObject(mod));
}

// If "import" and "default" are defined, "default" is used.
{
  const mod = require('../fixtures/es-modules/exports-import-default/load.cjs');
  assert.deepStrictEqual({ ...mod }, { type: 'cjs' });
  assert(!isModuleNamespaceObject(mod));
}
