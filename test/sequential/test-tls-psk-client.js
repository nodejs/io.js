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
const net = require('net');
const { spawn } = require('child_process');

const CIPHERS = 'PSK+HIGH';
const KEY = 'd731ef57be09e5204f0b205b60627028';
const IDENTITY = 'Client_identity';  // Hardcoded by `openssl s_server`
const useIPv4 = !common.hasIPv6;

const server = spawn(opensslCli, [
  's_server',
  '-accept', common.PORT,
  '-cipher', CIPHERS,
  '-psk', KEY,
  '-psk_hint', IDENTITY,
  '-nocert',
  '-rev',
  ...(useIPv4 ? ['-4'] : []),
], { encoding: 'utf8' });
let serverErr = '';
let serverOut = '';
server.stderr.on('data', (data) => serverErr += data);
server.stdout.on('data', (data) => serverOut += data);
server.on('error', common.mustNotCall());
server.on('exit', (code, signal) => {
  // Server is expected to be terminated by cleanUp().
  assert.strictEqual(code, null,
                     `'${server.spawnfile} ${server.spawnargs.join(' ')}' unexpected exited with output:\n${serverOut}\n${serverErr}`);
  assert.strictEqual(signal, 'SIGTERM');
});

const cleanUp = (err) => {
  clearTimeout(timeout);
  if (err)
    console.log('Failed:', err);
  server.kill();
  process.exitCode = err ? 1 : 0;
};

const timeout = setTimeout(() => cleanUp('Timed out'), 5000);

function waitForPort(port, cb) {
  const socket = net.connect(common.PORT, () => {
    socket.on('data', () => {});
    socket.end();
    socket.on('end', cb);
  });
  socket.on('error', (e) => {
    if (e.code === 'ENOENT' || e.code === 'ECONNREFUSED') {
      setTimeout(() => waitForPort(port, cb), 1000);
    } else {
      cb(e);
    }
  });
}

waitForPort(common.PORT, common.mustCall((err) => {
  if (err) {
    cleanUp(err);
    return;
  }

  const message = 'hello';
  const reverse = message.split('').reverse().join('');
  runClient(message, common.mustCall((err, data) => {
    try {
      if (!err) assert.strictEqual(data.trim(), reverse);
    } finally {
      cleanUp(err);
    }
  }));
}));

function runClient(message, cb) {
  const s = tls.connect(common.PORT, {
    ciphers: CIPHERS,
    checkServerIdentity: () => {},
    pskCallback(hint) {
      // 'hint' will be null in TLS1.3.
      if (hint === null || hint === IDENTITY) {
        return {
          identity: IDENTITY,
          psk: Buffer.from(KEY, 'hex')
        };
      }
    }
  });
  s.on('secureConnect', common.mustCall(() => {
    let data = '';
    s.on('data', common.mustCallAtLeast((d) => {
      data += d;
    }));
    s.on('end', common.mustCall(() => {
      cb(null, data);
    }));
    s.end(message);
  }));
  s.on('error', (e) => {
    cb(e);
  });
}
