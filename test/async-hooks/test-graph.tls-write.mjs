import { hasCrypto, skip, hasIPv6, mustCall } from '../common/index.mjs';
if (!hasCrypto)
  skip('missing crypto');

if (!hasIPv6)
  skip('IPv6 support required');

import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';
import { createServer, connect } from 'tls';
import { readKey } from '../common/fixtures.mjs';
import process from 'process';

const hooks = initHooks();
hooks.enable();

//
// Creating server and listening on port
//
const server = createServer({
    cert: readKey('rsa_cert.crt'),
    key: readKey('rsa_private.pem')
  })
  .on('listening', mustCall(onlistening))
  .on('secureConnection', mustCall(onsecureConnection))
  .listen(0);

function onlistening() {
  //
  // Creating client and connecting it to server
  //
  connect(server.address().port, { rejectUnauthorized: false })
    .on('secureConnect', mustCall(onsecureConnect));
}

function onsecureConnection() {}

function onsecureConnect() {
  // end() client socket, which causes slightly different hook events than
  // destroy(), but with TLS1.3 destroy() rips the connection down before the
  // server completes the handshake.
  this.end();

  // Closing server
  server.close(mustCall(onserverClosed));
}

function onserverClosed() {}

process.on('exit', onexit);

function onexit() {
  hooks.disable();

  verifyGraph(
    hooks,
    [ { type: 'TCPSERVERWRAP', id: 'tcpserver:1', triggerAsyncId: null },
      { type: 'TCPWRAP', id: 'tcp:1', triggerAsyncId: 'tcpserver:1' },
      { type: 'TLSWRAP', id: 'tls:1', triggerAsyncId: 'tcpserver:1' },
      { type: 'GETADDRINFOREQWRAP',
        id: 'getaddrinforeq:1', triggerAsyncId: 'tls:1' },
      { type: 'TCPCONNECTWRAP',
        id: 'tcpconnect:1', triggerAsyncId: 'tcp:1' },
      { type: 'TCPWRAP', id: 'tcp:2', triggerAsyncId: 'tcpserver:1' },
      { type: 'TLSWRAP', id: 'tls:2', triggerAsyncId: 'tcpserver:1' },
    ]
  );
}
