'use strict';

require('../common');

const assert = require('node:assert');
const url = require('node:url');
const { test } = require('node:test');

test('format invalid input', () => {
  const throwsObjsAndReportTypes = [
    undefined,
    null,
    true,
    false,
    0,
    function() {},
    Symbol('foo'),
  ];

  for (const urlObject of throwsObjsAndReportTypes) {
    assert.throws(() => {
      url.format(urlObject);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
    });
  }
  assert.strictEqual(url.format(''), '');
  assert.strictEqual(url.format({}), '');
});
