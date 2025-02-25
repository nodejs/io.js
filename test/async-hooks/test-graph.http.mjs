import { hasIPv6, skip, mustCall } from '../common/index.mjs';
if (!hasIPv6)
  skip('IPv6 support required');

import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';
import { createServer, get } from 'http';
import process from 'process';

const hooks = initHooks();
hooks.enable();

const server = createServer(mustCall((req, res) => {
  res.writeHead(200, { 'Connection': 'close' });
  res.end();
  server.close(mustCall());
}));
server.listen(0, mustCall(() => {
  get({
    host: '::1',
    family: 6,
    port: server.address().port
  }, mustCall());
}));

process.on('exit', () => {
  hooks.disable();

  verifyGraph(
    hooks,
    [ { type: 'TCPSERVERWRAP',
        id: 'tcpserver:1',
        triggerAsyncId: null },
      { type: 'TCPWRAP', id: 'tcp:1', triggerAsyncId: 'tcpserver:1' },
      { type: 'TCPCONNECTWRAP',
        id: 'tcpconnect:1',
        triggerAsyncId: 'tcp:1' },
      { type: 'HTTPCLIENTREQUEST',
        id: 'httpclientrequest:1',
        triggerAsyncId: 'tcpserver:1' },
      { type: 'TCPWRAP', id: 'tcp:2', triggerAsyncId: 'tcpserver:1' },
      { type: 'HTTPINCOMINGMESSAGE',
        id: 'httpincomingmessage:1',
        triggerAsyncId: 'tcp:2' },
      { type: 'Timeout',
        id: 'timeout:1',
        triggerAsyncId: null },
      { type: 'SHUTDOWNWRAP',
        id: 'shutdown:1',
        triggerAsyncId: 'tcp:2' } ]
  );
});
