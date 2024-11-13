'use strict';

const common = require('../common');
const assert = require('assert');
const { REPLServer } = require('repl');
const { Stream } = require('stream');

if (process.features.inspector)
  common.skip('test is for node compiled with --without-inspector only');

// Ignore terminal settings. This is so the test can be run intact if TERM=dumb.
process.env.TERM = '';
const PROMPT = 'repl > ';

class REPLStream extends Stream {
  readable = true;
  writable = true;

  constructor() {
    super();
    this.lines = [''];
  }
  run(data) {
    for (const entry of data) {
      this.emit('data', entry);
    }
    this.emit('data', '\n');
  }
  write(chunk) {
    const chunkLines = chunk.toString('utf8').split('\n');
    this.lines[this.lines.length - 1] += chunkLines[0];
    if (chunkLines.length > 1) {
      this.lines.push(...chunkLines.slice(1));
    }
    this.emit('line');
    return true;
  }
  wait() {
    this.lines = [''];
    return new Promise((resolve, reject) => {
      const onError = (err) => {
        this.removeListener('line', onLine);
        reject(err);
      };
      const onLine = () => {
        if (this.lines[this.lines.length - 1].includes(PROMPT)) {
          this.removeListener('error', onError);
          this.removeListener('line', onLine);
          resolve(this.lines);
        }
      };
      this.once('error', onError);
      this.on('line', onLine);
    });
  }
  pause() { }
  resume() { }
}

function runAndWait(cmds, repl) {
  const promise = repl.inputStream.wait();
  for (const cmd of cmds) {
    repl.inputStream.run(cmd);
  }
  return promise;
}

const repl = new REPLServer({
  prompt: PROMPT,
  stream: new REPLStream(),
  ignoreUndefined: true,
  useColors: true,
  terminal: true,
});

repl.inputStream.run([
  'function foo(x) { return x; }',
  'function koo() { console.log("abc"); }',
  'a = undefined;',
  'const r = 5;',
]);

const testCases = [{
  input: 'foo',
  preview: [
    'foo\r',
    '\x1B[36m[Function: foo]\x1B[39m',
  ]
}, {
  input: 'r',
  preview: [
    'r\r',
    '\x1B[33m5\x1B[39m',
  ]
}, {
  input: 'koo',
  preview: [
    'koo\r',
    '\x1B[36m[Function: koo]\x1B[39m',
  ]
}, {
  input: 'a',
  preview: ['a\r'] // No "undefined" preview.
}, {
  input: " { b: 1 }['b'] === 1",
  preview: [
    " { b: 1 }['b'] === 1\r",
    '\x1B[33mtrue\x1B[39m',
  ]
}, {
  input: "{ b: 1 }['b'] === 1;",
  preview: [
    "{ b: 1 }['b'] === 1;\r",
    '\x1B[33mfalse\x1B[39m',
  ]
}, {
  input: '{ a: true }',
  preview: [
    '{ a: true }\r',
    '{ a: \x1B[33mtrue\x1B[39m }',
  ]
}, {
  input: '{ a: true };',
  preview: [
    '{ a: true };\r',
    '\x1B[33mtrue\x1B[39m',
  ]
}, {
  input: ' \t { a: true};',
  preview: [
    '  { a: true};\r',
    '\x1B[33mtrue\x1B[39m',
  ]
}, {
  input: '1n + 2n',
  preview: [
    '1n + 2n\r',
    '\x1B[33m3n\x1B[39m',
  ]
}, {
  input: '{};1',
  preview: [
    '{};1\r',
    '\x1B[33m1\x1B[39m',
  ],
}];

async function runTest() {
  for (const { input, preview } of testCases) {
    const toBeRun = input.split('\n');
    let lines = await runAndWait(toBeRun, repl);
    // Remove error messages. That allows the code to run in different
    // engines.
    // eslint-disable-next-line no-control-regex
    lines = lines.map((line) => line.replace(/Error: .+?\x1B/, ''));
    assert.strictEqual(lines.pop(), '\x1B[1G\x1B[0Jrepl > \x1B[8G');
    assert.deepStrictEqual(lines, preview);
  }
}

runTest().then(common.mustCall());
