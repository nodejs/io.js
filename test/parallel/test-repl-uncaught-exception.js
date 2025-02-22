'use strict';

const common = require('../common');
const ArrayStream = require('../common/arraystream');
const assert = require('assert');
const repl = require('repl');

const results = [];

// Function to run a single test case
async function runTest({ command, expected, useColors = false }) {
  const result = { output: '' };

  // Custom stream to capture REPL output
  const replOutput = new ArrayStream();
  replOutput.write = (data) => { result.output += data.replace('\r', ''); };

  // Start REPL instance
  const replInstance = repl.start({
    prompt: '',
    input: new ArrayStream(),
    output: replOutput,
    terminal: false,
    useColors,
  });

  // Execute the command
  replInstance.write(`${command}\n`);

  // Validate output
  assert.strictEqual(result.output, expected);

  // Store REPL instance for future cleanup
  result.replInstance = replInstance;
  results.push(result);
}

// Test cases
const testCases = [
  { useColors: true, command: 'x', expected: 'Uncaught ReferenceError: x is not defined\n' },
  { useColors: true, command: 'throw { foo: "test" }', expected: "Uncaught { foo: \x1B[32m'test'\x1B[39m }\n" },
  { command: 'x;\n', expected: 'Uncaught ReferenceError: x is not defined\n' },
];

// Execute tests
testCases.forEach(runTest);

// Verify all tests ran
assert.strictEqual(results.length, testCases.length);

// Check 'uncaughtException' listener count
assert.strictEqual(process.listenerCount('uncaughtException'), 1);

// Test uncaught exception handling
const errorToThrow = new Error('Thrown');
process.once('uncaughtException', common.mustCall((err) => {
  assert.strictEqual(err, errorToThrow);
}));

// Trigger uncaught exception
process.nextTick(() => { throw errorToThrow; });

// Cleanup
setTimeout(common.mustCall(() => {
  results.forEach(({ replInstance }) => {
    replInstance.close();
  });
  setTimeout(common.mustCall(() => {
    results.forEach(({ output }) => {
      assert.doesNotMatch(output, /Uncaught Error: Thrown/);
    });
    assert.strictEqual(process.listenerCount('uncaughtException'), 0);
  }), 100);
}), 100);
