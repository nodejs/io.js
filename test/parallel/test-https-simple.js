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
const fixtures = require('../common/fixtures');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const https = require('https');

// Assert that the IP-as-servername deprecation warning does not occur.
process.on('warning', common.mustNotCall());

const options = {
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem')
};

const body = 'hello world\n';

const serverCallback = common.mustCall(function(req, res) {
  res.writeHead(200, { 'content-type': 'text/plain' });
  res.end(body);
});

const invalid_options = [ 'foo', 42, true, [] ];
for (const option of invalid_options) {
  assert.throws(() => {
    new https.Server(option);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  });
}

const server = https.createServer(options, serverCallback);

server.listen(0, common.mustCall(() => {
  let tests = 0;

  function done() {
    if (--tests === 0)
      server.close();
  }

  // Do a request ignoring the unauthorized server certs
  const port = server.address().port;

  const options = {
    hostname: '127.0.0.1',
    port: port,
    path: '/',
    method: 'GET',
    rejectUnauthorized: false
  };
  tests++;
  const req = https.request(options, common.mustCall((res) => {
    let responseBody = '';
    res.on('data', function(d) {
      responseBody = responseBody + d;
    });

    res.on('end', common.mustCall(() => {
      assert.strictEqual(responseBody, body);
      done();
    }));
  }));
  req.end();

  // Do a request that errors due to the invalid server certs
  options.rejectUnauthorized = true;
  tests++;
  const checkCertReq = https.request(options, common.mustNotCall()).end();

  checkCertReq.on('error', common.mustCall((e) => {
    assert.strictEqual(e.code, 'UNABLE_TO_VERIFY_LEAF_SIGNATURE');
    done();
  }));
}));
