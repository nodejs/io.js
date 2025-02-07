'use strict';
const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const assert = require('assert');
const { spawn } = require('child_process');
const tls = require('tls');
const fixtures = require('../common/fixtures');

function loadPEM(n) {
  return fixtures.readKey(`${n}.pem`);
}

const serverIP = common.localhostIPv4;

function checkResults(result, expected) {
  assert.strictEqual(result.server.ALPN, expected.server.ALPN);
  assert.strictEqual(result.client.ALPN, expected.client.ALPN);
}

function runTest(clientsOptions, serverOptions, cb) {
  serverOptions.key = loadPEM('agent2-key');
  serverOptions.cert = loadPEM('agent2-cert');
  const results = [];
  let clientIndex = 0;
  let serverIndex = 0;
  const server = tls.createServer(serverOptions, function(c) {
    results[serverIndex++].server = { ALPN: c.alpnProtocol };
  });

  server.listen(0, serverIP, function() {
    connectClient(clientsOptions);
  });

  function connectClient(options) {
    const opt = options.shift();
    opt.port = server.address().port;
    opt.host = serverIP;
    opt.rejectUnauthorized = false;

    results[clientIndex] = {};

    function startNextClient() {
      if (options.length) {
        clientIndex++;
        connectClient(options);
      } else {
        server.close();
        server.on('close', () => {
          cb(results);
        });
      }
    }

    const client = tls.connect(opt, function() {
      results[clientIndex].client = { ALPN: client.alpnProtocol };
      client.end();
      startNextClient();
    }).on('error', function(err) {
      results[clientIndex].client = { error: err };
      startNextClient();
    });
  }

}

// Server: ALPN, Client: ALPN
function Test1() {
  const serverOptions = {
    ALPNProtocols: ['a', 'b', 'c'],
  };

  const clientsOptions = [{
    ALPNProtocols: ['a', 'b', 'c'],
  }, {
    ALPNProtocols: ['c', 'b', 'e'],
  }, {
    ALPNProtocols: ['x', 'y', 'c'],
  }];

  runTest(clientsOptions, serverOptions, function(results) {
    // 'a' is selected by ALPN
    checkResults(results[0],
                 { server: { ALPN: 'a' },
                   client: { ALPN: 'a' } });
    // 'b' is selected by ALPN
    checkResults(results[1],
                 { server: { ALPN: 'b' },
                   client: { ALPN: 'b' } });
    // Nothing is selected by ALPN
    checkResults(results[2],
                 { server: { ALPN: 'c' },
                   client: { ALPN: 'c' } });
    // execute next test
    Test2();
  });
}

// Server: ALPN, Client: Nothing
function Test2() {
  const serverOptions = {
    ALPNProtocols: ['a', 'b', 'c'],
  };

  const clientsOptions = [{}, {}, {}];

  runTest(clientsOptions, serverOptions, function(results) {
    // Nothing is selected by ALPN
    checkResults(results[0],
                 { server: { ALPN: false },
                   client: { ALPN: false } });
    // Nothing is selected by ALPN
    checkResults(results[1],
                 { server: { ALPN: false },
                   client: { ALPN: false } });
    // Nothing is selected by ALPN
    checkResults(results[2],
                 { server: { ALPN: false },
                   client: { ALPN: false } });
    // execute next test
    Test3();
  });
}

// Server: Nothing, Client: ALPN
function Test3() {
  const serverOptions = {};

  const clientsOptions = [{
    ALPNrotocols: ['a', 'b', 'c'],
  }, {
    ALPNProtocols: ['c', 'b', 'e'],
  }, {
    ALPNProtocols: ['first-priority-unsupported', 'x', 'y'],
  }];

  runTest(clientsOptions, serverOptions, function(results) {
    // nothing is selected
    checkResults(results[0], { server: { ALPN: false },
                               client: { ALPN: false } });
    // nothing is selected
    checkResults(results[1], { server: { ALPN: false },
                               client: { ALPN: false } });
    // nothing is selected
    checkResults(results[2],
                 { server: { ALPN: false },
                   client: { ALPN: false } });
    // execute next test
    Test4();
  });
}

// Server: Nothing, Client: Nothing
function Test4() {
  const serverOptions = {};

  const clientsOptions = [{}, {}, {}];

  runTest(clientsOptions, serverOptions, function(results) {
    // nothing is selected
    checkResults(results[0], { server: { ALPN: false },
                               client: { ALPN: false } });
    // nothing is selected
    checkResults(results[1], { server: { ALPN: false },
                               client: { ALPN: false } });
    // nothing is selected
    checkResults(results[2],
                 { server: { ALPN: false },
                   client: { ALPN: false } });
  });

  TestFatalAlert();
}

function TestFatalAlert() {
  const server = tls.createServer({
    ALPNProtocols: ['foo'],
    key: loadPEM('agent2-key'),
    cert: loadPEM('agent2-cert')
  }, common.mustNotCall());

  server.listen(0, serverIP, common.mustCall(() => {
    const { port } = server.address();

    // The Node.js client will just report ECONNRESET (older OpenSSL) or
    // ERR_SSL_TLSV1_ALERT_NO_APPLICATION_PROTOCOL because the connection
    // is severed before the TLS handshake completes.
    tls.connect({
      host: serverIP,
      port,
      rejectUnauthorized: false,
      ALPNProtocols: ['bar']
    }, common.mustNotCall()).on('error', common.mustCall((err) => {
      const allowedErrors = ['ECONNRESET', 'ERR_SSL_TLSV1_ALERT_NO_APPLICATION_PROTOCOL'];
      assert.ok(allowedErrors.includes(err.code), `'${err.code}' was not one of ${allowedErrors}.`);

      // OpenSSL's s_client should output the TLS alert number, which is 120
      // for the 'no_application_protocol' alert.
      const { opensslCli } = require('../common/crypto');
      if (opensslCli) {
        const addr = `${serverIP}:${port}`;
        let stderr = '';
        spawn(opensslCli, ['s_client', '--alpn', 'bar', addr], {
          stdio: ['ignore', 'ignore', 'pipe']
        }).stderr
          .setEncoding('utf8')
          .on('data', (chunk) => stderr += chunk)
          .on('close', common.mustCall(() => {
            assert.match(stderr, /SSL alert number 120/);
            server.close();
            TestALPNCallback();
          }));
      } else {
        server.close();
        TestALPNCallback();
      }
    }));
  }));
}

function TestALPNCallback() {
  // Server always selects the client's 2nd preference:
  const serverOptions = {
    ALPNCallback: common.mustCall(({ protocols }) => {
      return protocols[1];
    }, 2)
  };

  const clientsOptions = [{
    ALPNProtocols: ['a', 'b', 'c'],
  }, {
    ALPNProtocols: ['a'],
  }];

  runTest(clientsOptions, serverOptions, function(results) {
    // Callback picks 2nd preference => picks 'b'
    checkResults(results[0],
                 { server: { ALPN: 'b' },
                   client: { ALPN: 'b' } });

    // Callback picks 2nd preference => undefined => ALPN rejected:
    assert.strictEqual(results[1].server, undefined);
    const allowedErrors = ['ECONNRESET', 'ERR_SSL_TLSV1_ALERT_NO_APPLICATION_PROTOCOL'];
    assert.ok(allowedErrors.includes(results[1].client.error.code), `'${results[1].client.error.code}' was not one of ${allowedErrors}.`);

    TestBadALPNCallback();
  });
}

function TestBadALPNCallback() {
  // Server always returns a fixed invalid value:
  const serverOptions = {
    ALPNCallback: common.mustCall(() => 'http/5')
  };

  const clientsOptions = [{
    ALPNProtocols: ['http/1', 'h2'],
  }];

  process.once('uncaughtException', common.mustCall((error) => {
    assert.strictEqual(error.code, 'ERR_TLS_ALPN_CALLBACK_INVALID_RESULT');
  }));

  runTest(clientsOptions, serverOptions, function(results) {
    // Callback returns 'http/5' => doesn't match client ALPN => error & reset
    assert.strictEqual(results[0].server, undefined);
    const allowedErrors = ['ECONNRESET', 'ERR_SSL_TLSV1_ALERT_NO_APPLICATION_PROTOCOL'];
    assert.ok(allowedErrors.includes(results[0].client.error.code), `'${results[0].client.error.code}' was not one of ${allowedErrors}.`);

    TestALPNOptionsCallback();
  });
}

function TestALPNOptionsCallback() {
  // Server sets two incompatible ALPN options:
  assert.throws(() => tls.createServer({
    ALPNCallback: () => 'a',
    ALPNProtocols: ['b', 'c']
  }), (error) => error.code === 'ERR_TLS_ALPN_CALLBACK_WITH_PROTOCOLS');
}

Test1();
