// Flags: --expose-internals
import { mustCallAtLeast, mustCall } from '../common/index.mjs';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { ok, strictEqual } from 'assert';
import internalAsyncHooks from 'internal/async_hooks';
import process from 'process';

import { Agent, createServer, request } from 'http';

const { async_id_symbol } = internalAsyncHooks.symbols;

// Checks that the async resource used in init in case of a reused handle
// is not reused. Test is based on parallel\test-async-hooks-http-agent.js.

const hooks = initHooks();
hooks.enable();

const reqAsyncIds = [];
let socket;
let responses = 0;

// Make sure a single socket is transparently reused for 2 requests.
const agent = new Agent({
  keepAlive: true,
  keepAliveMsecs: Infinity,
  maxSockets: 1
});

const verifyRequest = (idx) => (res) => {
  reqAsyncIds[idx] = res.socket[async_id_symbol];
  ok(reqAsyncIds[idx] > 0, `${reqAsyncIds[idx]} > 0`);
  if (socket) {
    // Check that both requests share their socket.
    strictEqual(res.socket, socket);
  } else {
    socket = res.socket;
  }

  res.on('data', mustCallAtLeast(() => {}));
  res.on('end', mustCall(() => {
    if (++responses === 2) {
      // Clean up to let the event loop stop.
      server.close();
      agent.destroy();
    }
  }));
};

const server = createServer(mustCall((req, res) => {
  req.once('data', mustCallAtLeast(() => {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.write('foo');
  }));
  req.on('end', mustCall(() => {
    res.end('bar');
  }));
}, 2)).listen(0, mustCall(() => {
  const port = server.address().port;
  const payload = 'hello world';

  // First request.
  const r1 = request({
    agent, port, method: 'POST'
  }, mustCall(verifyRequest(0)));
  r1.end(payload);

  // Second request. Sent in parallel with the first one.
  const r2 = request({
    agent, port, method: 'POST'
  }, mustCall(verifyRequest(1)));
  r2.end(payload);
}));


process.on('exit', onExit);

function onExit() {
  hooks.disable();
  hooks.sanityCheck();
  const activities = hooks.activities;

  // Verify both invocations
  const first = activities.filter((x) => x.uid === reqAsyncIds[0])[0];
  checkInvocations(first, { init: 1, destroy: 1 }, 'when process exits');

  const second = activities.filter((x) => x.uid === reqAsyncIds[1])[0];
  checkInvocations(second, { init: 1, destroy: 1 }, 'when process exits');

  // Verify reuse handle has been wrapped
  strictEqual(first.type, second.type);
  ok(first.handle !== second.handle, 'Resource reused');
  ok(first.handle === second.handle.handle,
            'Resource not wrapped correctly');
}
