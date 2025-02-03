'use strict';
const common = require('../common');
const ArrayStream = require('../common/arraystream');
const assert = require('assert');

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

const readline = require('readline');
const rli = new readline.Interface({
  terminal: true,
  input: new ArrayStream(),
  output: new ArrayStream(),
});

// Minimal reproduction for #47305
const testInput = '{\n}';

let accum = '';

rli.output.write = (data) => accum += data.replace('\r', '');

rli.write(testInput);

assert.strictEqual(accum, testInput);
