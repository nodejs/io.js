'use strict';
const common = require('../common');
const assert = require('assert');
const { Worker, isMainThread } = require('worker_threads');

if (isMainThread) {
  const w = new Worker(__filename, { stdout: true });
  const expected = 'hello world';

  let data = '';
  w.stdout.setEncoding('utf8');
  w.stdout.on('data', (chunk) => {
    data += chunk;
  });

  w.on('exit', common.mustCall(() => {
    assert.strictEqual(data, expected);
  }));
} else {
  process.on('exit', () => {
    process.stdout.write(' ');
    process.stdout.write('world');
  });
  process.stdout.write('hello');
}
