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

const assert = require('assert');
const crypto = require('crypto');

[ 'modp1', 'modp2', 'modp5', 'modp14', 'modp15', 'modp16', 'modp17' ]
.forEach((name) => {
  // modp1 is 768 bits, FIPS requires >= 1024
  if (name === 'modp1' && crypto.getFips()) {
    return;
  }
  const group1 = crypto.getDiffieHellman(name);
  const group2 = crypto.getDiffieHellman(name);
  group1.generateKeys();
  group2.generateKeys();
  const key1 = group1.computeSecret(group2.getPublicKey());
  const key2 = group2.computeSecret(group1.getPublicKey());
  assert.deepStrictEqual(key1, key2);
});
