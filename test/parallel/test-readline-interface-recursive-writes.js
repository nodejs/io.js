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
});

let recursionDepth = 0;

// Minimal reproduction for #46731
const testInput = ' \n}\n';
const numberOfExpectedLines = testInput.match(/\n/g).length;

rli.on('line', () => {
  // Abort in case of infinite loop
  if (recursionDepth > numberOfExpectedLines) {
    return;
  }
  recursionDepth++;
  // Write something recursively to readline
  rli.write('foo');
});


rli.write(testInput);

assert.strictEqual(recursionDepth, numberOfExpectedLines);
