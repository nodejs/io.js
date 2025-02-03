'use strict';
const common = require('../common');
const assert = require('assert');
const Stream = require('stream');
const repl = require('repl');

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

const tests = [
  testSloppyMode,
  testStrictMode,
  testAutoMode,
  testStrictModeTerminal,
];

tests.forEach(function(test) {
  test();
});

function testSloppyMode() {
  const cli = initRepl(repl.REPL_MODE_SLOPPY);

  cli.input.emit('data', 'x = 3\n');
  assert.strictEqual(cli.output.accumulator.join(''), '> 3\n> ');
  cli.output.accumulator.length = 0;

  cli.input.emit('data', 'let y = 3\n');
  assert.strictEqual(cli.output.accumulator.join(''), 'undefined\n> ');
}

function testStrictMode() {
  const cli = initRepl(repl.REPL_MODE_STRICT);

  cli.input.emit('data', 'x = 3\n');
  assert.match(cli.output.accumulator.join(''),
               /ReferenceError: x is not defined/);
  cli.output.accumulator.length = 0;

  cli.input.emit('data', 'let y = 3\n');
  assert.strictEqual(cli.output.accumulator.join(''), 'undefined\n> ');
}

function testStrictModeTerminal() {
  if (!process.features.inspector) {
    console.warn('Test skipped: V8 inspector is disabled');
    return;
  }
  // Verify that ReferenceErrors are reported in strict mode previews.
  const cli = initRepl(repl.REPL_MODE_STRICT, {
    terminal: true
  });

  cli.input.emit('data', 'xyz ');
  assert.ok(
    cli.output.accumulator.includes('\n// ReferenceError: xyz is not defined')
  );
}

function testAutoMode() {
  const cli = initRepl(repl.REPL_MODE_MAGIC);

  cli.input.emit('data', 'x = 3\n');
  assert.strictEqual(cli.output.accumulator.join(''), '> 3\n> ');
  cli.output.accumulator.length = 0;

  cli.input.emit('data', 'let y = 3\n');
  assert.strictEqual(cli.output.accumulator.join(''), 'undefined\n> ');
}

function initRepl(mode, options) {
  const input = new Stream();
  input.write = input.pause = input.resume = () => {};
  input.readable = true;

  const output = new Stream();
  output.write = output.pause = output.resume = function(buf) {
    output.accumulator.push(buf);
  };
  output.accumulator = [];
  output.writable = true;

  return repl.start({
    input: input,
    output: output,
    useColors: false,
    terminal: false,
    replMode: mode,
    ...options
  });
}
