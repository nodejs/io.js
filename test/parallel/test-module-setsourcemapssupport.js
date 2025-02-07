'use strict';
require('../common');
const assert = require('node:assert');
const Module = require('node:module');

// This test verifies that the `Module.setSourceMapsSupport` throws on invalid
// argument inputs.

{
  const unexpectedValues = [
    undefined,
    null,
    1,
    {},
    () => {},
  ];
  for (const it of unexpectedValues) {
    assert.throws(() => {
      Module.setSourceMapsSupport(it);
    }, /ERR_INVALID_ARG_TYPE/);
  }
}

{
  const unexpectedValues = [
    null,
    1,
    {},
    () => {},
  ];
  for (const it of unexpectedValues) {
    assert.throws(() => {
      Module.setSourceMapsSupport(true, {
        nodeModules: it,
      });
    }, /ERR_INVALID_ARG_TYPE/);
    assert.throws(() => {
      Module.setSourceMapsSupport(true, {
        generatedCode: it,
      });
    }, /ERR_INVALID_ARG_TYPE/);
  }
}
