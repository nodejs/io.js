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

const fixtures = require('../common/fixtures');
const { hasOpenSSL3 } = require('../common/crypto');

const assert = require('assert');
const tls = require('tls');
const errorMessageRegex = hasOpenSSL3 ?
  /^Error: error:05800074:x509 certificate routines::key values mismatch$/ :
  /^Error: error:0B080074:x509 certificate routines:X509_check_private_key:key values mismatch$/;

const options = {
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent2-cert.pem')
};

assert.throws(function() {
  tls.createSecureContext(options);
}, errorMessageRegex);
