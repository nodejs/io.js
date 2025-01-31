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
if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const assert = require('assert');
const stream = require('stream');
const crypto = require('crypto');
const { hasOpenSSL3 } = require('../common/crypto');

if (!crypto.getFips()) {
  // Small stream to buffer converter
  class Stream2buffer extends stream.Writable {
    constructor(callback) {
      super();

      this._buffers = [];
      this.once('finish', function() {
        callback(null, Buffer.concat(this._buffers));
      });
    }

    _write(data, encoding, done) {
      this._buffers.push(data);
      return done(null);
    }
  }

  // Create an md5 hash of "Hallo world"
  const hasher1 = crypto.createHash('md5');
  hasher1.pipe(new Stream2buffer(common.mustCall(function end(err, hash) {
    assert.strictEqual(err, null);
    assert.strictEqual(
      hash.toString('hex'), '06460dadb35d3d503047ce750ceb2d07'
    );
  })));
  hasher1.end('Hallo world');

  // Simpler check for unpipe, setEncoding, pause and resume
  crypto.createHash('md5').unpipe({});
  crypto.createHash('md5').setEncoding('utf8');
  crypto.createHash('md5').pause();
  crypto.createHash('md5').resume();
}

// Decipher._flush() should emit an error event, not an exception.
const key = Buffer.from('48fb56eb10ffeb13fc0ef551bbca3b1b', 'hex');
const badkey = Buffer.from('12341234123412341234123412341234', 'hex');
const iv = Buffer.from('6d358219d1f488f5f4eb12820a66d146', 'hex');
const cipher = crypto.createCipheriv('aes-128-cbc', key, iv);
const decipher = crypto.createDecipheriv('aes-128-cbc', badkey, iv);

cipher.pipe(decipher)
  .on('error', common.expectsError(hasOpenSSL3 ? {
    message: /bad decrypt/,
    library: 'Provider routines',
    reason: 'bad decrypt',
  } : {
    message: /bad decrypt/,
    function: 'EVP_DecryptFinal_ex',
    library: 'digital envelope routines',
    reason: 'bad decrypt',
  }));

cipher.end('Papaya!');  // Should not cause an unhandled exception.
