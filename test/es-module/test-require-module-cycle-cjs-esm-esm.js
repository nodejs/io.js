// Flags: --experimental-require-module
'use strict';

// This tests that ESM <-> ESM cycle is allowed in a require()-d graph.
const common = require('../common');
const cycle = require('../fixtures/es-modules/cjs-esm-esm-cycle/c.cjs');

common.expectRequiredModule(cycle, { b: 5 });
