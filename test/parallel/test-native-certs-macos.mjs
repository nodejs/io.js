// Flags: --use-system-ca

import * as common from '../common/index.mjs';
import assert from 'node:assert/strict';
import https from 'node:https';
import fixtures from '../common/fixtures.js';
import { it, beforeEach, afterEach, describe } from 'node:test';
import { once } from 'events';

const handleRequest = (req, res) => {
  const path = req.url;
  switch (path) {
    case '/hello-world':
      res.writeHead(200);
      res.end('hello world\n');
      break;
    default:
      assert(false, `Unexpected path: ${path}`);
  }
};

describe('use-system-ca', { skip: !common.isMacOS }, function() {
  let server;

  beforeEach(async function() {
    server = https.createServer({
      key: fixtures.readKey('agent8-key.pem'),
      cert: fixtures.readKey('agent8-cert.pem'),
    }, handleRequest);
    server.listen(0);
    await once(server, 'listening');
  });

  it('can connect successfully with a trusted certificate', async function() {
    // Requires trusting the CA certificate first (which needs an interactive GUI approval, e.g. TouchID):
    // security add-trusted-cert \
    // -k /Users/$USER/Library/Keychains/login.keychain-db test/fixtures/keys/fake-startcom-root-cert.pem
    // To remove:
    // security delete-certificate -c 'StartCom Certification Authority' \
    // -t /Users/$USER/Library/Keychains/login.keychain-db
    await fetch(`https://localhost:${server.address().port}/hello-world`);
  });

  afterEach(async function() {
    server?.close();
  });
});
