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
const tls = require('tls');
const fixtures = require('../common/fixtures');

const testCases = [
  { ca: ['ca1-cert'],
    key: 'agent2-key',
    cert: 'agent2-cert',
    servers: [
      { ok: true, key: 'agent1-key', cert: 'agent1-cert' },
      { ok: false, key: 'agent2-key', cert: 'agent2-cert' },
      { ok: false, key: 'agent3-key', cert: 'agent3-cert' },
    ] },

  { ca: [],
    key: 'agent2-key',
    cert: 'agent2-cert',
    servers: [
      { ok: false, key: 'agent1-key', cert: 'agent1-cert' },
      { ok: false, key: 'agent2-key', cert: 'agent2-cert' },
      { ok: false, key: 'agent3-key', cert: 'agent3-cert' },
    ] },

  { ca: ['ca1-cert', 'ca2-cert'],
    key: 'agent2-key',
    cert: 'agent2-cert',
    servers: [
      { ok: true, key: 'agent1-key', cert: 'agent1-cert' },
      { ok: false, key: 'agent2-key', cert: 'agent2-cert' },
      { ok: true, key: 'agent3-key', cert: 'agent3-cert' },
    ] },
];


function loadPEM(n) {
  return fixtures.readKey(`${n}.pem`);
}

let successfulTests = 0;

function testServers(index, servers, clientOptions, cb) {
  const serverOptions = servers[index];
  if (!serverOptions) {
    cb();
    return;
  }

  const ok = serverOptions.ok;

  serverOptions.key &&= loadPEM(serverOptions.key);

  serverOptions.cert &&= loadPEM(serverOptions.cert);

  const server = tls.createServer(serverOptions, common.mustCall(function(s) {
    s.end('hello world\n');
  }));

  server.listen(0, common.mustCall(function() {
    let b = '';

    console.error('connecting...');
    clientOptions.port = this.address().port;
    const client = tls.connect(clientOptions, common.mustCall(function() {
      const authorized = client.authorized ||
          (client.authorizationError === 'ERR_TLS_CERT_ALTNAME_INVALID');

      console.error(`expected: ${ok} authed: ${authorized}`);

      assert.strictEqual(authorized, ok);
      server.close();
    }));

    client.on('data', function(d) {
      b += d.toString();
    });

    client.on('end', common.mustCall(function() {
      assert.strictEqual(b, 'hello world\n');
    }));

    client.on('close', common.mustCall(function() {
      testServers(index + 1, servers, clientOptions, cb);
    }));
  }));
}


function runTest(testIndex) {
  const tcase = testCases[testIndex];
  if (!tcase) return;

  const clientOptions = {
    port: undefined,
    ca: tcase.ca.map(loadPEM),
    key: loadPEM(tcase.key),
    cert: loadPEM(tcase.cert),
    rejectUnauthorized: false
  };


  testServers(0, tcase.servers, clientOptions, common.mustCall(function() {
    successfulTests++;
    runTest(testIndex + 1);
  }));
}


runTest(0);


process.on('exit', function() {
  console.log(`successful tests: ${successfulTests}`);
  assert.strictEqual(successfulTests, testCases.length);
});
