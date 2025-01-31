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

const { opensslCli } = require('../common/crypto');

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const assert = require('assert');
const tls = require('tls');
const fixtures = require('../common/fixtures');

const key = fixtures.readKey('rsa_private.pem');
const cert = fixtures.readKey('rsa_cert.crt');

{
  // Node.js should not allow setting negative timeouts since new versions of
  // OpenSSL do not handle those as users might expect

  for (const sessionTimeout of [-1, -100, -(2 ** 31)]) {
    assert.throws(() => {
      tls.createServer({
        key: key,
        cert: cert,
        ca: [cert],
        sessionTimeout,
        maxVersion: 'TLSv1.2',
      });
    }, {
      code: 'ERR_OUT_OF_RANGE',
      message: 'The value of "options.sessionTimeout" is out of range. It ' +
               `must be >= 0 && <= ${2 ** 31 - 1}. Received ${sessionTimeout}`,
    });
  }
}

if (!opensslCli) {
  common.skip('node compiled without OpenSSL CLI.');
}

doTest();

// This test consists of three TLS requests --
// * The first one should result in a new connection because we don't have
//   a valid session ticket.
// * The second one should result in connection resumption because we used
//   the session ticket we saved from the first connection.
// * The third one should result in a new connection because the ticket
//   that we used has expired by now.

function doTest() {
  const fs = require('fs');
  const spawn = require('child_process').spawn;

  const SESSION_TIMEOUT = 1;

  const options = {
    key: key,
    cert: cert,
    ca: [cert],
    sessionTimeout: SESSION_TIMEOUT,
    maxVersion: 'TLSv1.2',
  };

  // We need to store a sample session ticket in the fixtures directory because
  // `s_client` behaves incorrectly if we do not pass in both the `-sess_in`
  // and the `-sess_out` flags, and the `-sess_in` argument must point to a
  // file containing a proper serialization of a session ticket.
  // To avoid a source control diff, we copy the ticket to a temporary file.

  const sessionFileName = (function() {
    const ticketFileName = 'tls-session-ticket.txt';
    const tmpPath = tmpdir.resolve(ticketFileName);
    fs.writeFileSync(tmpPath, fixtures.readSync(ticketFileName));
    return tmpPath;
  }());

  // Expects a callback -- cb(connectionType : enum ['New'|'Reused'])

  function Client(cb) {
    const flags = [
      's_client',
      '-connect', `localhost:${common.PORT}`,
      '-sess_in', sessionFileName,
      '-sess_out', sessionFileName,
    ];
    const client = spawn(opensslCli, flags, {
      stdio: ['ignore', 'pipe', 'ignore']
    });

    let clientOutput = '';
    client.stdout.on('data', (data) => {
      clientOutput += data.toString();
    });
    client.on('exit', (code) => {
      let connectionType;
      const grepConnectionType = (line) => {
        const matches = line.match(/(New|Reused), /);
        if (matches) {
          connectionType = matches[1];
          return true;
        }
      };
      const lines = clientOutput.split('\n');
      if (!lines.some(grepConnectionType)) {
        throw new Error('unexpected output from openssl client');
      }
      assert.strictEqual(code, 0);
      cb(connectionType);
    });
  }

  const server = tls.createServer(options, (cleartext) => {
    cleartext.on('error', (er) => {
      if (er.code !== 'ECONNRESET')
        throw er;
    });
    cleartext.end();
  });

  server.listen(common.PORT, () => {
    Client((connectionType) => {
      assert.strictEqual(connectionType, 'New');
      Client((connectionType) => {
        assert.strictEqual(connectionType, 'Reused');
        setTimeout(() => {
          Client((connectionType) => {
            assert.strictEqual(connectionType, 'New');
            server.close();
          });
        }, (SESSION_TIMEOUT + 1) * 1000);
      });
    });
  });
}
