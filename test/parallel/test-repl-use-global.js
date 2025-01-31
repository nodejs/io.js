'use strict';

// Flags: --expose-internals

const common = require('../common');
const stream = require('stream');
const repl = require('internal/repl');
const assert = require('assert');

// Array of [useGlobal, expectedResult] pairs
const globalTestCases = [
  [false, 'undefined'],
  [true, '\'tacos\''],
  [undefined, 'undefined'],
];

const globalTest = (useGlobal, cb, output) => (err, repl) => {
  if (err)
    return cb(err);

  let str = '';
  output.on('data', (data) => (str += data));
  globalThis.lunch = 'tacos';
  repl.write('globalThis.lunch;\n');
  repl.close();
  delete globalThis.lunch;
  cb(null, str.trim());
};

// Test how the global object behaves in each state for useGlobal
for (const [option, expected] of globalTestCases) {
  runRepl(option, globalTest, common.mustSucceed((output) => {
    assert.strictEqual(output, expected);
  }));
}

// Test how shadowing the process object via `let`
// behaves in each useGlobal state. Note: we can't
// actually test the state when useGlobal is true,
// because the exception that's generated is caught
// (see below), but errors are printed, and the test
// suite is aware of it, causing a failure to be flagged.
//
const processTestCases = [false, undefined];
const processTest = (useGlobal, cb, output) => (err, repl) => {
  if (err)
    return cb(err);

  let str = '';
  output.on('data', (data) => (str += data));

  // If useGlobal is false, then `let process` should work
  repl.write('let process;\n');
  repl.write('21 * 2;\n');
  repl.close();
  cb(null, str.trim());
};

for (const option of processTestCases) {
  runRepl(option, processTest, common.mustSucceed((output) => {
    assert.strictEqual(output, 'undefined\n42');
  }));
}

function runRepl(useGlobal, testFunc, cb) {
  const inputStream = new stream.PassThrough();
  const outputStream = new stream.PassThrough();
  const opts = {
    input: inputStream,
    output: outputStream,
    useGlobal: useGlobal,
    useColors: false,
    terminal: false,
    prompt: ''
  };

  repl.createInternalRepl(
    process.env,
    opts,
    testFunc(useGlobal, cb, opts.output));
}
