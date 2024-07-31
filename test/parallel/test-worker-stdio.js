'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const util = require('util');
const { Writable } = require('stream');
const { Worker, isMainThread } = require('worker_threads');

class BufferingWritable extends Writable {
  constructor() {
    super();
    this.chunks = [];
  }

  _write(chunk, enc, cb) {
    this.chunks.push(chunk);
    cb();
  }

  get buffer() {
    return Buffer.concat(this.chunks);
  }
}

if (isMainThread) {
  const original = new BufferingWritable();
  const passed = new BufferingWritable();

  const w = new Worker(__filename, { stdin: true, stdout: true });
  const source = fs.createReadStream(process.execPath, { end: 1_000_000 });
  source.pipe(w.stdin);
  source.pipe(original);
  w.stdout.pipe(passed);

  passed.on('finish', common.mustCall(() => {
    assert.strictEqual(original.buffer.compare(passed.buffer), 0,
                       `Original: ${util.inspect(original.buffer)}, ` +
                       `Actual: ${util.inspect(passed.buffer)}`);
  }));
} else {
  process.stdin.pipe(process.stdout);
}
