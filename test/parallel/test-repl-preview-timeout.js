'use strict';

const common = require('../common');
const ArrayStream = require('../common/arraystream');
const assert = require('assert');
const repl = require('repl');

common.skipIfInspectorDisabled();

const inputStream = new ArrayStream();
const outputStream = new ArrayStream();
repl.start({
  input: inputStream,
  output: outputStream,
  useGlobal: false,
  terminal: true,
  useColors: true
});

let output = '';
outputStream.write = (chunk) => output += chunk;

// Input without '\n' triggering actual run.
const input = 'while (true) {}';
inputStream.emit('data', input);
// No preview available when timed out.
assert.strictEqual(output, input);
