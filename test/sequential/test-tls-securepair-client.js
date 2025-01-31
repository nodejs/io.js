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

if (!opensslCli) {
  common.skip('node compiled without OpenSSL CLI.');
}

if (common.isWindows) {
  common.skip('test does not work on Windows'); // ...but it should!
}

const net = require('net');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const tls = require('tls');
const spawn = require('child_process').spawn;

const useIPv4 = !common.hasIPv6;

test1();

// simple/test-tls-securepair-client
function test1() {
  test('keys/rsa_private.pem', 'keys/rsa_cert.crt', null, test2);
}

// simple/test-tls-ext-key-usage
function test2() {
  function check(pair) {
    // "TLS Web Client Authentication"
    assert.strictEqual(pair.cleartext.getPeerCertificate().ext_key_usage.length,
                       1);
    assert.strictEqual(pair.cleartext.getPeerCertificate().ext_key_usage[0],
                       '1.3.6.1.5.5.7.3.2');
  }
  test('keys/agent4-key.pem', 'keys/agent4-cert.pem', check);
}

function test(keyPath, certPath, check, next) {
  const key = fixtures.readSync(keyPath).toString();
  const cert = fixtures.readSync(certPath).toString();

  const server = spawn(opensslCli, ['s_server',
                                    '-accept', 0,
                                    '-cert', fixtures.path(certPath),
                                    '-key', fixtures.path(keyPath),
                                    ...(useIPv4 ? ['-4'] : []),
  ]);
  server.stdout.pipe(process.stdout);
  server.stderr.pipe(process.stdout);


  let state = 'WAIT-ACCEPT';

  let serverStdoutBuffer = '';
  server.stdout.setEncoding('utf8');
  server.stdout.on('data', function(s) {
    serverStdoutBuffer += s;
    console.log(state);
    switch (state) {
      case 'WAIT-ACCEPT': {
        const matches = serverStdoutBuffer.match(/ACCEPT .*?:(\d+)/);
        if (matches) {
          const port = matches[1];
          state = 'WAIT-HELLO';
          startClient(port);
        }
        break;
      }
      case 'WAIT-HELLO':
        if (/hello/.test(serverStdoutBuffer)) {

          // End the current SSL connection and exit.
          // See s_server(1ssl).
          server.stdin.write('Q');

          state = 'WAIT-SERVER-CLOSE';
        }
        break;

      default:
        break;
    }
  });


  const timeout = setTimeout(function() {
    server.kill();
    process.exit(1);
  }, 5000);

  let gotWriteCallback = false;
  let serverExitCode = -1;

  server.on('exit', function(code) {
    serverExitCode = code;
    clearTimeout(timeout);
    if (next) next();
  });


  function startClient(port) {
    const s = new net.Stream();

    const sslcontext = tls.createSecureContext({ key, cert });
    sslcontext.context.setCiphers('RC4-SHA:AES128-SHA:AES256-SHA');

    const pair = tls.createSecurePair(sslcontext, false);

    assert.ok(pair.encrypted.writable);
    assert.ok(pair.cleartext.writable);

    pair.encrypted.pipe(s);
    s.pipe(pair.encrypted);

    s.connect(port);

    s.on('connect', function() {
      console.log('client connected');
      setTimeout(function() {
        pair.cleartext.write('hello\r\n', function() {
          gotWriteCallback = true;
        });
      }, 500);
    });

    pair.on('secure', function() {
      console.log('client: connected+secure!');
      console.log('client pair.cleartext.getPeerCertificate(): %j',
                  pair.cleartext.getPeerCertificate());
      console.log('client pair.cleartext.getCipher(): %j',
                  pair.cleartext.getCipher());
      if (check) check(pair);
    });

    pair.cleartext.on('data', function(d) {
      console.log('cleartext: %s', d.toString());
    });

    s.on('close', function() {
      console.log('client close');
    });

    pair.encrypted.on('error', function(err) {
      console.log(`encrypted error: ${err}`);
    });

    s.on('error', function(err) {
      console.log(`socket error: ${err}`);
    });

    pair.on('error', function(err) {
      console.log(`secure error: ${err}`);
    });
  }


  process.on('exit', function() {
    assert.strictEqual(serverExitCode, 0);
    assert.strictEqual(state, 'WAIT-SERVER-CLOSE');
    assert.ok(gotWriteCallback);
  });
}
