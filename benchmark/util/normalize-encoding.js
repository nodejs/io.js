'use strict';

const common = require('../common.js');
const assert = require('assert');

const groupedInputs = {
  group_common: ['undefined', 'utf8', 'utf-8', 'base64',
                 'binary', 'latin1', 'ucs2'],
  group_upper: ['UTF-8', 'UTF8', 'UCS2',
                'UTF16LE', 'BASE64', 'UCS2'],
  group_uncommon: ['foo'],
  group_misc: ['', 'utf16le', 'hex', 'HEX', 'BINARY'],
};

const inputs = [
  '', 'utf8', 'utf-8', 'UTF-8', 'UTF8', 'Utf8',
  'ucs2', 'UCS2', 'utf16le', 'UTF16LE',
  'binary', 'BINARY', 'latin1', 'base64', 'BASE64',
  'hex', 'HEX', 'foo', 'undefined',
];

const bench = common.createBenchmark(main, {
  input: inputs.concat(Object.keys(groupedInputs)),
  n: [1e6],
}, {
  flags: '--expose-internals',
});

function getInput(input) {
  switch (input) {
    case 'group_common':
      return groupedInputs.group_common;
    case 'group_upper':
      return groupedInputs.group_upper;
    case 'group_uncommon':
      return groupedInputs.group_uncommon;
    case 'group_misc':
      return groupedInputs.group_misc;
    case 'undefined':
      return [undefined];
    default:
      return [input];
  }
}

function main({ input, n }) {
  const { normalizeEncoding } = require('internal/util');
  const inputs = getInput(input);
  let noDead = '';

  bench.start();
  for (let i = 0; i < n; ++i) {
    for (let j = 0; j < inputs.length; ++j) {
      noDead = normalizeEncoding(inputs[j]);
    }
  }
  bench.end(n);
  assert.ok(noDead === undefined || noDead.length > 0);
}
