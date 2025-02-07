'use strict';
// Flags: --expose-gc

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const http2 = require('http2');
const tls = require('tls');
const fixtures = require('../common/fixtures');
const assert = require('assert');

const registry = new FinalizationRegistry(common.mustCall((name) => {
  assert(name, 'session');
}));

const server = http2.createSecureServer({
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem'),
});

let firstServerStream;


server.on('secureConnection', (s) => {
  console.log('secureConnection');
  s.on('end', () => {
    console.log(s.destroyed); // false !!
    s.destroy();
    firstServerStream.session.destroy();

    firstServerStream = null;

    setImmediate(() => {
      globalThis.gc();
      globalThis.gc();

      server.close();
    });
  });
});

server.on('session', (s) => {
  registry.register(s, 'session');
});

server.on('stream', (stream) => {
  console.log('stream...');
  stream.write('a'.repeat(1024));
  firstServerStream = stream;
  setImmediate(() => console.log('Draining setImmediate after writing'));
});


server.listen(() => {
  client();
});


const h2fstStream = [
  'UFJJICogSFRUUC8yLjANCg0KU00NCg0K',
  // http message (1st stream:)
  'AAAABAAAAAAA',
  'AAAPAQUAAAABhIJBiqDkHROdCbjwHgeG',
];
function client() {
  const client = tls.connect({
    port: server.address().port,
    host: 'localhost',
    rejectUnauthorized: false,
    ALPNProtocols: ['h2']
  }, () => {
    client.end(Buffer.concat(h2fstStream.map((s) => Buffer.from(s, 'base64'))), (err) => {
      assert.ifError(err);
    });
  });

  client.on('error', (error) => {
    console.error('Connection error:', error);
  });
}
