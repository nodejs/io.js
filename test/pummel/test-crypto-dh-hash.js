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
  common.skip('node compiled without OpenSSL.');
}

if (common.isPi) {
  common.skip('Too slow for Raspberry Pi devices');
}

const { hasOpenSSL3 } = require('../common/crypto');

if (!hasOpenSSL3) {
  common.skip('Too slow when dynamically linked against OpenSSL 1.1.1');
}

const assert = require('assert');
const crypto = require('crypto');

const hashes = {
  modp1: '630e9acd2cc63f7e80d8507624ba60ac0757201a',
  modp2: '18f7aa964484137f57bca64b21917a385b6a0b60',
  modp5: 'c0a8eec0c2c8a5ec2f9c26f9661eb339a010ec61',
  modp14: 'af5455606fe74cec49782bb374e4c63c9b1d132c',
  modp15: '7bdd39e5cdbb9748113933e5c2623b559c534e74',
  modp16: 'daea5277a7ad0116e734a8e0d2f297ef759d1161',
  modp17: '3b62aaf0142c2720f0bf26a9589b0432c00eadc1',
  modp18: 'a870b491bbbec9b131ae9878d07449d32e54f160',
};

for (const name in hashes) {
  const group = crypto.getDiffieHellman(name);
  const prime = group.getPrime('hex');
  const hash1 = hashes[name];
  const hash2 = crypto.createHash('sha1')
                    .update(prime.toUpperCase()).digest('hex');
  assert.strictEqual(hash1, hash2);
  assert.strictEqual(group.getGenerator('hex'), '02');
}
