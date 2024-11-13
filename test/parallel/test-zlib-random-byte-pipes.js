// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const stream = require('stream');
const zlib = require('zlib');

const Stream = stream.Stream;

// Emit random bytes, and keep a shasum
class RandomReadStream extends Stream {
  constructor(opt) {
    super();

    this.readable = true;
    this._paused = false;
    this._processing = false;

    this._hasher = crypto.createHash('sha1');
    opt ||= {};

    // base block size.
    opt.block ||= 256 * 1024;

    // Total number of bytes to emit
    opt.total ||= 256 * 1024 * 1024;
    this._remaining = opt.total;

    // How variable to make the block sizes
    opt.jitter ||= 1024;

    this._opt = opt;

    this._process = this._process.bind(this);

    process.nextTick(this._process);
  }

  pause() {
    this._paused = true;
    this.emit('pause');
  }

  resume() {
    // console.error("rrs resume");
    this._paused = false;
    this.emit('resume');
    this._process();
  }

  _process() {
    if (this._processing) return;
    if (this._paused) return;

    this._processing = true;

    if (!this._remaining) {
      this._hash = this._hasher.digest('hex').toLowerCase().trim();
      this._processing = false;

      this.emit('end');
      return;
    }

    // Figure out how many bytes to output
    // if finished, then just emit end.
    let block = this._opt.block;
    const jitter = this._opt.jitter;
    if (jitter) {
      block += Math.ceil(Math.random() * jitter - (jitter / 2));
    }
    block = Math.min(block, this._remaining);
    const buf = Buffer.allocUnsafe(block);
    for (let i = 0; i < block; i++) {
      buf[i] = Math.random() * 256;
    }

    this._hasher.update(buf);

    this._remaining -= block;

    this._processing = false;

    this.emit('data', buf);
    process.nextTick(this._process);
  }
}

// A filter that just verifies a shasum
class HashStream extends Stream {
  constructor() {
    super();
    this.readable = this.writable = true;
    this._hasher = crypto.createHash('sha1');
  }

  write(c) {
    // Simulate the way that an fs.ReadStream returns false
    // on *every* write, only to resume a moment later.
    this._hasher.update(c);
    process.nextTick(() => this.resume());
    return false;
  }

  resume() {
    this.emit('resume');
    process.nextTick(() => this.emit('drain'));
  }

  end(c) {
    if (c) {
      this.write(c);
    }
    this._hash = this._hasher.digest('hex').toLowerCase().trim();
    this.emit('data', this._hash);
    this.emit('end');
  }
}

for (const [ createCompress, createDecompress ] of [
  [ zlib.createGzip, zlib.createGunzip ],
  [ zlib.createBrotliCompress, zlib.createBrotliDecompress ],
]) {
  const inp = new RandomReadStream({ total: 1024, block: 256, jitter: 16 });
  const out = new HashStream();
  const gzip = createCompress();
  const gunz = createDecompress();

  inp.pipe(gzip).pipe(gunz).pipe(out);

  out.on('data', common.mustCall((c) => {
    assert.strictEqual(c, inp._hash, `Hash '${c}' equals '${inp._hash}'.`);
  }));
}
