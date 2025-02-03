'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('node:assert');
const tls = require('node:tls');
const { fork } = require('node:child_process');
const fixtures = require('../common/fixtures');

const tests = [
  {
    get clientOptions() {
      const secureContext = tls.createSecureContext();
      secureContext.context.addCACert(
        fixtures.readKey('ca1-cert.pem')
      );

      return {
        secureContext
      };
    }
  },
  {
    clientOptions: {
      crl: fixtures.readKey('ca2-crl.pem')
    }
  },
  {
    clientOptions: {
      pfx: fixtures.readKey('agent1.pfx'),
      passphrase: 'sample'
    }
  },
];

if (process.argv[2]) {
  const testNumber = parseInt(process.argv[2], 10);
  assert(testNumber >= 0 && testNumber < tests.length);

  const test = tests[testNumber];

  const clientOptions = {
    ...test.clientOptions,
    port: process.argv[3],
    checkServerIdentity: common.mustCall()
  };

  const client = tls.connect(clientOptions, common.mustCall(() => {
    client.end('hi');
  }));
} else {
  const serverOptions = {
    key: fixtures.readKey('agent3-key.pem'),
    cert: fixtures.readKey('agent3-cert.pem')
  };

  for (const testNumber in tests) {
    const server = tls.createServer(serverOptions, common.mustCall((socket) => {
      socket.end('bye');
      server.close();
    }));

    server.listen(0, common.mustCall(() => {
      const env = {
        ...process.env,
        NODE_EXTRA_CA_CERTS: fixtures.path('keys', 'ca2-cert.pem')
      };

      const args = [
        testNumber,
        server.address().port,
      ];

      fork(__filename, args, { env }).on('exit', common.mustCall((status) => {
        assert.strictEqual(status, 0);
      }));
    }));
  }
}
