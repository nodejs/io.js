'use strict';
const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

// This test verifies the behavior of the tls setSecureContext() method.
// It also verifies that existing connections are not disrupted when the
// secure context is changed.

const assert = require('assert');
const events = require('events');
const https = require('https');
const timers = require('timers/promises');
const { hasOpenSSL3 } = require('../common/crypto');
const fixtures = require('../common/fixtures');
const credentialOptions = [
  {
    key: fixtures.readKey('agent1-key.pem'),
    cert: fixtures.readKey('agent1-cert.pem'),
    ca: fixtures.readKey('ca1-cert.pem')
  },
  {
    key: fixtures.readKey('agent2-key.pem'),
    cert: fixtures.readKey('agent2-cert.pem'),
    ca: fixtures.readKey('ca2-cert.pem')
  },
];
let firstResponse;

const server = https.createServer(credentialOptions[0], (req, res) => {
  const id = +req.headers.id;

  if (id === 1) {
    firstResponse = res;
    firstResponse.write('multi-');
    return;
  } else if (id === 4) {
    firstResponse.write('success-');
  }

  res.end('success');
});

server.listen(0, common.mustCall(() => {
  const { port } = server.address();
  const firstRequest = makeRequest(port, 1);

  (async function makeRemainingRequests() {
    // Wait until the first request is guaranteed to have been handled.
    while (!firstResponse) {
      await timers.setImmediate();
    }

    assert.strictEqual(await makeRequest(port, 2), 'success');

    server.setSecureContext(credentialOptions[1]);
    firstResponse.write('request-');
    const errorMessageRegex = hasOpenSSL3 ?
      /^Error: self-signed certificate$/ :
      /^Error: self signed certificate$/;
    await assert.rejects(makeRequest(port, 3), errorMessageRegex);

    server.setSecureContext(credentialOptions[0]);
    assert.strictEqual(await makeRequest(port, 4), 'success');

    server.setSecureContext(credentialOptions[1]);
    firstResponse.end('fun!');
    await assert.rejects(makeRequest(port, 5), errorMessageRegex);

    assert.strictEqual(await firstRequest, 'multi-request-success-fun!');
    server.close();
  })().then(common.mustCall());
}));

async function makeRequest(port, id) {
  const options = {
    rejectUnauthorized: true,
    ca: credentialOptions[0].ca,
    servername: 'agent1',
    headers: { id },
    agent: new https.Agent()
  };

  const req = https.get(`https://localhost:${port}`, options);

  let errored = false;
  req.on('error', () => errored = true);
  req.on('finish', () => assert.strictEqual(errored, false));

  const [res] = await events.once(req, 'response');
  res.setEncoding('utf8');
  let response = '';
  for await (const chunk of res) response += chunk;
  return response;
}
