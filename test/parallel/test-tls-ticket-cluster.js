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
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');
const cluster = require('cluster');
const fixtures = require('../common/fixtures');

const workerCount = 4;
const expectedReqCount = 16;

if (cluster.isPrimary) {
  let listeningCount = 0;
  let reusedCount = 0;
  let reqCount = 0;
  let lastSession = null;
  let workerPort = null;

  function shoot() {
    console.error('[primary] connecting',
                  workerPort, 'session?', !!lastSession);
    const c = tls.connect(workerPort, {
      session: lastSession,
      rejectUnauthorized: false
    }, () => {
      c.on('end', c.end);
    }).on('close', () => {
      // Wait for close to shoot off another connection. We don't want to shoot
      // until a new session is allocated, if one will be. The new session is
      // not guaranteed on secureConnect (it depends on TLS1.2 vs TLS1.3), but
      // it is guaranteed to happen before the connection is closed.
      if (++reqCount === expectedReqCount) {
        Object.keys(cluster.workers).forEach(function(id) {
          cluster.workers[id].send('die');
        });
      } else {
        shoot();
      }
    }).once('session', (session) => {
      assert(!lastSession);
      lastSession = session;
    });

    c.resume(); // See close_notify comment in server
  }

  function fork() {
    const worker = cluster.fork();
    worker.on('message', ({ msg, port }) => {
      console.error('[primary] got %j', msg);
      if (msg === 'reused') {
        ++reusedCount;
      } else if (msg === 'listening' && ++listeningCount === workerCount) {
        workerPort = port;
        shoot();
      }
    });

    worker.on('exit', () => {
      console.error('[primary] worker died');
    });
  }
  for (let i = 0; i < workerCount; i++) {
    fork();
  }

  process.on('exit', () => {
    assert.strictEqual(reqCount, expectedReqCount);
    assert.strictEqual(reusedCount + 1, reqCount);
  });
  return;
}

const key = fixtures.readKey('rsa_private.pem');
const cert = fixtures.readKey('rsa_cert.crt');

const options = { key, cert };

const server = tls.createServer(options, (c) => {
  console.error('[worker] connection reused?', c.isSessionReused());
  if (c.isSessionReused()) {
    process.send({ msg: 'reused' });
  } else {
    process.send({ msg: 'not-reused' });
  }
  // Used to just .end(), but that means client gets close_notify before
  // NewSessionTicket. Send data until that problem is solved.
  c.end('x');
});

server.listen(0, () => {
  const { port } = server.address();
  process.send({
    msg: 'listening',
    port,
  });
});

process.on('message', function listener(msg) {
  console.error('[worker] got %j', msg);
  if (msg === 'die') {
    server.close(() => {
      console.error('[worker] server close');

      process.exit();
    });
  }
});

process.on('exit', () => {
  console.error('[worker] exit');
});
