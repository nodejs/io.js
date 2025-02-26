'use strict';

const common = require('../common');
const { it, describe } = require('node:test');
const assert = require('node:assert');
const tls = require('node:tls');
const https = require('node:https');
const dns = require('node:dns');
const events = require('node:events');

const fixtures = require('../common/fixtures');
const crypto = require('node:crypto');

const root = {
  cert: fixtures.readKey('ca5-cert.pem'),
  key: fixtures.readKey('ca5-key.pem')
};

const agent1 = {
  cert: fixtures.readKey('agent1-cert.pem'),
  key: fixtures.readKey('agent1-key.pem')
};

const PORT = 8443;

const sni = {
  'ca5.com': {
    cert: new crypto.X509Certificate(root.cert),
    secureContext: tls.createSecureContext(root)
  },
  'agent1.com': {
    cert: new crypto.X509Certificate(agent1.cert),
    context: tls.createSecureContext(agent1),
  },
};

const agent = new https.Agent({
  lookup: (hostname, options, cb) => {
    if (Object.keys(sni).includes(hostname))
      hostname = 'localhost';
    return dns.lookup(hostname, options, cb);
  }
});

function makeRequest(url, expectedCert) {
  return new Promise((resolve, reject) => {
    https.get(url, { rejectUnauthorized: false, agent }, common.mustCall((response) => {
      const actualCert = response.socket.getPeerX509Certificate();

      assert.deepStrictEqual(actualCert.subject, expectedCert.subject);

      response.on('data', common.mustCall((chunk) => {
        assert.strictEqual(chunk.toString(), 'Hello, World!');
        resolve();
      }));

      response.on('error', reject);
    })).on('error', reject);
  });
}

describe('Regression test for SNICallback / Certification prioritization issue', () => {
  it('should use certificates from SNICallback', async (t) => {
    let snicbCount = 0;
    const server = https.createServer({
      cert: root.cert,
      key: root.key,
      SNICallback: (servername, cb) => {
        snicbCount++;
        // This returns the secure context generated from the respective certificate
        cb(null, sni[servername].context);
      }
    }, (req, res) => {
      res.writeHead(200);
      res.end('Hello, World!');
    }).on('error', common.mustNotCall);

    server.listen(PORT);
    await events.once(server, 'listening');

    // Assert that raw IP address gets the root cert
    await makeRequest(`https://127.0.0.1:${PORT}`, sni['ca5.com'].cert);

    for (const [hostname, { cert: expectedCert }] of Object.entries(sni)) {
      // Currently, the agent1 request will fail as it receives the root cert (ca5) instead.
      await makeRequest(`https://${hostname}:${PORT}`, expectedCert);
    }

    // SNICallback should only be called for the hostname requests, not the IP one
    assert.strictEqual(snicbCount, 2);

    server.close();
  });
});
