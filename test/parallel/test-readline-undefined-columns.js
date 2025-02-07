'use strict';

const common = require('../common');
const assert = require('assert');
const PassThrough = require('stream').PassThrough;
const readline = require('readline');

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

// Checks that tab completion still works
// when output column size is undefined

const iStream = new PassThrough();
const oStream = new PassThrough();

readline.createInterface({
  terminal: true,
  input: iStream,
  output: oStream,
  completer: function(line, cb) {
    cb(null, [['process.stdout', 'process.stdin', 'process.stderr'], line]);
  }
});

let output = '';

oStream.on('data', function(data) {
  output += data;
});

oStream.on('end', common.mustCall(() => {
  const expect = 'process.stdout\r\n' +
                 'process.stdin\r\n' +
                 'process.stderr';
  assert.match(output, new RegExp(expect));
}));

iStream.write('process.s\t');

// Completion works.
assert.match(output, /process\.std\b/);
// Completion doesn’t show all results yet.
assert.doesNotMatch(output, /stdout/);

iStream.write('\t');
oStream.end();
