// Flags: --experimental-require-module
'use strict';

const common = require('../common');

{
  const mod = require('../fixtures/es-modules/loose.js');
  common.expectRequiredModule(mod, { default: 'module' });
}

{
  const mod = require('../fixtures/es-modules/package-without-type/noext-esm');
  common.expectRequiredModule(mod, { default: 'module' });
}
