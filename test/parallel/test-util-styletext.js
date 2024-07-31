'use strict';
require('../common');
const assert = require('assert');
const util = require('util');

[
  undefined,
  null,
  false,
  5n,
  5,
  Symbol(),
  () => {},
  {},
].forEach((invalidOption) => {
  assert.throws(() => {
    util.styleText(invalidOption, 'test');
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
  });
  assert.throws(() => {
    util.styleText('red', invalidOption);
  }, {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

assert.throws(() => {
  util.styleText('invalid', 'text');
}, {
  code: 'ERR_INVALID_ARG_VALUE',
});

assert.strictEqual(util.styleText('red', 'test'), '\u001b[31mtest\u001b[39m');

assert.strictEqual(util.styleText(['bold', 'red'], 'test'), '\u001b[1m\u001b[31mtest\u001b[39m\u001b[22m');
assert.strictEqual(util.styleText(['bold', 'red'], 'test'), util.styleText('bold', util.styleText('red', 'test')));

assert.throws(() => {
  util.styleText(['invalid'], 'text');
}, {
  code: 'ERR_INVALID_ARG_VALUE',
});
