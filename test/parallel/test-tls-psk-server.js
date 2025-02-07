'use strict';
const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { opensslCli } = require('../common/crypto');

if (!opensslCli) {
  common.skip('missing openssl cli');
}

const assert = require('assert');

const tls = require('tls');
const spawn = require('child_process').spawn;

const CIPHERS = 'PSK+HIGH';
const KEY = 'd731ef57be09e5204f0b205b60627028';
const IDENTITY = 'TestUser';

const server = tls.createServer({
  ciphers: CIPHERS,
  pskIdentityHint: IDENTITY,
  pskCallback(socket, identity) {
    assert.ok(socket instanceof tls.TLSSocket);
    assert.ok(typeof identity === 'string');
    if (identity === IDENTITY)
      return Buffer.from(KEY, 'hex');
  }
});

server.on('connection', common.mustCall());

server.on('secureConnection', (socket) => {
  socket.write('hello\r\n');

  socket.on('data', (data) => {
    socket.write(data);
  });
});

let gotHello = false;
let sentWorld = false;
let gotWorld = false;

server.listen(0, () => {
  const client = spawn(opensslCli, [
    's_client',
    '-connect', `127.0.0.1:${server.address().port}`,
    '-cipher', CIPHERS,
    '-psk', KEY,
    '-psk_identity', IDENTITY,
  ]);

  let out = '';

  client.stdout.setEncoding('utf8');
  client.stdout.on('data', (d) => {
    out += d;

    if (!gotHello && /hello/.test(out)) {
      gotHello = true;
      client.stdin.write('world\r\n');
      sentWorld = true;
    }

    if (!gotWorld && /world/.test(out)) {
      gotWorld = true;
      client.stdin.end();
    }
  });

  client.on('exit', common.mustCall((code) => {
    assert.ok(gotHello);
    assert.ok(sentWorld);
    assert.ok(gotWorld);
    assert.strictEqual(code, 0);
    server.close();
  }));
});
