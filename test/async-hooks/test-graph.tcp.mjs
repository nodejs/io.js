import { hasIPv6, skip, mustCall } from '../common/index.mjs';
if (!hasIPv6)
  skip('IPv6 support required');

import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';
import { createServer, connect } from 'net';
import process from 'process';

const hooks = initHooks();
hooks.enable();

const server = createServer(mustCall(onconnection))
  .on('listening', mustCall(onlistening));

server.listen(0);

connect({ port: server.address().port, host: '::1' },
            mustCall(onconnected));

function onlistening() {}

function onconnected() {}

function onconnection(c) {
  c.end();
  this.close(mustCall(onserverClosed));
}

function onserverClosed() {}

process.on('exit', onexit);

function onexit() {
  hooks.disable();

  verifyGraph(
    hooks,
    [ { type: 'TCPSERVERWRAP', id: 'tcpserver:1', triggerAsyncId: null },
      { type: 'TCPWRAP', id: 'tcp:1', triggerAsyncId: null },
      { type: 'TCPCONNECTWRAP',
        id: 'tcpconnect:1', triggerAsyncId: 'tcp:1' },
      { type: 'TCPWRAP', id: 'tcp:2', triggerAsyncId: 'tcpserver:1' },
      { type: 'SHUTDOWNWRAP', id: 'shutdown:1', triggerAsyncId: 'tcp:2' } ]
  );
}
