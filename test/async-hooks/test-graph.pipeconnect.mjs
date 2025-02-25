import { PIPE, mustCall } from '../common/index.mjs';
import process from 'process';
import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';

import { createServer, connect } from 'net';

import { refresh } from '../common/tmpdir.js';
refresh();

const hooks = initHooks();
hooks.enable();

const server = createServer((c) => {
  c.end();
  server.close();
}).listen(PIPE, mustCall(onlisten));

function onlisten() {
  connect(PIPE, mustCall(onconnect));
}

function onconnect() {}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  verifyGraph(
    hooks,
    [ { type: 'PIPESERVERWRAP', id: 'pipeserver:1', triggerAsyncId: null },
      { type: 'PIPEWRAP', id: 'pipe:1', triggerAsyncId: 'pipeserver:1' },
      { type: 'PIPECONNECTWRAP', id: 'pipeconnect:1',
        triggerAsyncId: 'pipe:1' },
      { type: 'PIPEWRAP', id: 'pipe:2', triggerAsyncId: 'pipeserver:1' },
      { type: 'SHUTDOWNWRAP', id: 'shutdown:1', triggerAsyncId: 'pipe:2' } ]
  );
}
