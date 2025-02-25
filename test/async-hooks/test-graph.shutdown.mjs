import { hasIPv6, skip, mustCall } from '../common/index.mjs';
import process from 'process';

if (!hasIPv6)
  skip('IPv6 support required');

import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';
import { createServer, connect } from 'net';

const hooks = initHooks();
hooks.enable();

const server = createServer(onconnection)
  .on('listening', mustCall(onlistening));
server.listen();
function onlistening() {
  connect(server.address().port, mustCall(onconnected));
}

function onconnection(c) {
  c.end();
  this.close(onserverClosed);
}

function onconnected() {}

function onserverClosed() {}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  verifyGraph(
    hooks,
    [ { type: 'TCPSERVERWRAP', id: 'tcpserver:1', triggerAsyncId: null },
      { type: 'TCPWRAP', id: 'tcp:1', triggerAsyncId: 'tcpserver:1' },
      { type: 'GETADDRINFOREQWRAP',
        id: 'getaddrinforeq:1', triggerAsyncId: 'tcp:1' },
      { type: 'TCPCONNECTWRAP',
        id: 'tcpconnect:1', triggerAsyncId: 'tcp:1' },
      { type: 'TCPWRAP', id: 'tcp:2', triggerAsyncId: 'tcpserver:1' },
      { type: 'SHUTDOWNWRAP', id: 'shutdown:1', triggerAsyncId: 'tcp:2' } ]
  );
}
