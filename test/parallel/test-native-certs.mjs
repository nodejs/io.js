// Flags: --use-system-ca

import * as common from '../common/index.mjs';
import assert from 'node:assert/strict';
import https from 'node:https';
import fixtures from '../common/fixtures.js';
import { it, beforeEach, afterEach, describe } from 'node:test';
import { once } from 'events';

if (!common.hasCrypto) {
  common.skip('requires crypto');
}

// To run this test, the system needs to be configured to trust
// the CA certificate first (which needs an interactive GUI approval, e.g. TouchID):
// On macOS:
//   1. To add the certificate:
//    $ security add-trusted-cert \
//       -k /Users/$USER/Library/Keychains/login.keychain-db \
//       test/fixtures/keys/fake-startcom-root-cert.pem
//    $ security add-certificates \
//        -k /Users/$USER/Library/Keychains/login.keychain-db \
//        test/fixtures/keys/intermediate-ca.pem
//    $ security add-certificates \
//        -k /Users/$USER/Library/Keychains/login.keychain-db \
//        test/fixtures/keys/non-trusted-intermediate-ca.pem
//   2. To remove the certificate:
//     $ security delete-certificate -c 'StartCom Certification Authority' \
//         -t /Users/$USER/Library/Keychains/login.keychain-db
//     $ security delete-certificate -c 'NodeJS-Test-Intermediate-CA' \
//         -t /Users/$USER/Library/Keychains/login.keychain-db
//     $ security delete-certificate -c 'NodeJS-Non-Trusted-Test-Intermediate-CA' \
//         -t /Users/$USER/Library/Keychains/login.keychain-db
//
// On Windows:
//   1. To add the certificate in PowerShell (remember the thumbprint printed):
//    $ Import-Certificate -FilePath .\test\fixtures\keys\fake-startcom-root-cert.cer \
//       -CertStoreLocation Cert:\CurrentUser\Root
//    $ Import-Certificate -FilePath .\test\fixtures\keys\intermediate-ca.pem \
//       -CertStoreLocation Cert:\CurrentUser\CA
//    $ Import-Certificate -FilePath .\test\fixtures\keys\non-trusted-intermediate-ca.pem \
//       -CertStoreLocation Cert:\CurrentUser\CA
//   2. To remove the certificate by the thumbprint:
//    $  $thumbprint = (Get-ChildItem -Path Cert:\CurrentUser\Root | \
//          Where-Object { $_.Subject -match "StartCom Certification Authority" }).Thumbprint
//    $  Remove-Item -Path "Cert:\CurrentUser\Root\$thumbprint"
//    $  $thumbprint = (Get-ChildItem -Path Cert:\CurrentUser\CA | \
//          Where-Object { $_.Subject -match "NodeJS-Test-Intermediate-CA" }).Thumbprint
//    $  Remove-Item -Path "Cert:\CurrentUser\CA\$thumbprint"
//    $  $thumbprint = (Get-ChildItem -Path Cert:\CurrentUser\CA | \
//          Where-Object { $_.Subject -match "NodeJS-Non-Trusted-Test-Intermediate-CA" }).Thumbprint
//    $  Remove-Item -Path "Cert:\CurrentUser\CA\$thumbprint"
//
// On Debian/Ubuntu:
//   1. To add the certificate:
//     $ sudo cp test/fixtures/keys/fake-startcom-root-cert.pem \
//       /usr/local/share/ca-certificates/fake-startcom-root-cert.crt
//     $ sudo cp test/fixtures/keys/intermediate-ca.pem \
//       /usr/local/share/ca-certificates/intermediate-ca.crt
//     $ sudo cp test/fixtures/keys/non-trusted-intermediate-ca.pem \
//       /usr/local/share/ca-certificates/non-trusted-intermediate-ca.crt
//     $ sudo update-ca-certificates
//   2. To remove the certificate
//     $ sudo rm /usr/local/share/ca-certificates/fake-startcom-root-cert.crt \
//       /usr/local/share/ca-certificates/intermediate-ca.crt \
//       /usr/local/share/ca-certificates/non-trusted-intermediate-ca.crt
//     $ sudo update-ca-certificates --fresh
//
// For other Unix-like systems, consult their manuals, there are usually
// file-based processes similar to the Debian/Ubuntu one but with different
// file locations and update commands.
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

describe('use-system-ca', function() {

  async function setupServer(key, cert) {
    const theServer = https.createServer({
      key: fixtures.readKey(key),
      cert: fixtures.readKey(cert),
    }, handleRequest);
    theServer.listen(0);
    await once(theServer, 'listening');

    return theServer;
  }

  describe('signed with a root certificate', () => {
    let server;

    beforeEach(async function() {
      server = await setupServer('agent8-key.pem', 'agent8-cert.pem');
    });

    it('can connect successfully', async function() {
      await fetch(`https://localhost:${server.address().port}/hello-world`);
    });

    afterEach(async function() {
      server?.close();
    });
  });

  describe('signed with an intermediate CA certificate', () => {
    let server;

    beforeEach(async function() {
      server = await setupServer('leaf-from-intermediate-key.pem', 'leaf-from-intermediate-cert.pem');
    });

    it('can connect successfully', async function() {
      await fetch(`https://localhost:${server.address().port}/hello-world`);
    });

    afterEach(async function() {
      server?.close();
    });
  });

  describe('signed with a trusted intermediate but not trusted root CA certificate', () => {
    let server;

    beforeEach(async function() {
      server = await setupServer(
        'non-trusted-leaf-from-intermediate-key.pem',
        'non-trusted-leaf-from-intermediate-cert.pem'
      );
    });

    it('can connect successfully', async function() {
      try {
        await fetch(`https://localhost:${server.address().port}/hello-world`);
      } catch (err) {
        if (common.isWindows) {
          assert.strictEqual(err.cause.code, 'UNABLE_TO_GET_ISSUER_CERT');
        } else {
          assert.strictEqual(err.cause.code, 'UNABLE_TO_VERIFY_LEAF_SIGNATURE');
        }
      }
    });

    afterEach(async function() {
      server?.close();
    });
  });

});
