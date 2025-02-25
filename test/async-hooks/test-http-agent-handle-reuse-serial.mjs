// Flags: --expose-internals
import { mustCall, mustCallAtLeast } from '../common/index.mjs';
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

let asyncIdAtFirstReq;
let asyncIdAtSecondReq;

// Make sure a single socket is transparently reused for 2 requests.
const agent = new Agent({
  keepAlive: true,
  keepAliveMsecs: Infinity,
  maxSockets: 1
});

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

  // First request. This is useless except for adding a socket to the
  // agent’s pool for reuse.
  const r1 = request({
    agent, port, method: 'POST'
  }, mustCall((res) => {
    // Remember which socket we used.
    const socket = res.socket;
    asyncIdAtFirstReq = socket[async_id_symbol];
    ok(asyncIdAtFirstReq > 0, `${asyncIdAtFirstReq} > 0`);
    // Check that request and response share their socket.
    strictEqual(r1.socket, socket);

    res.on('data', mustCallAtLeast(() => {}));
    res.on('end', mustCall(() => {
      // setImmediate() to give the agent time to register the freed socket.
      setImmediate(mustCall(() => {
        // The socket is free for reuse now.
        strictEqual(socket[async_id_symbol], -1);

        // Second request. To re-create the exact conditions from the
        // referenced issue, we use a POST request without chunked encoding
        // (hence the Content-Length header) and call .end() after the
        // response header has already been received.
        const r2 = request({
          agent, port, method: 'POST', headers: {
            'Content-Length': payload.length
          }
        }, mustCall((res) => {
          asyncIdAtSecondReq = res.socket[async_id_symbol];
          ok(asyncIdAtSecondReq > 0, `${asyncIdAtSecondReq} > 0`);
          strictEqual(r2.socket, socket);

          // Empty payload, to hit the “right” code path.
          r2.end('');

          res.on('data', mustCallAtLeast(() => {}));
          res.on('end', mustCall(() => {
            // Clean up to let the event loop stop.
            server.close();
            agent.destroy();
          }));
        }));

        // Schedule a payload to be written immediately, but do not end the
        // request just yet.
        r2.write(payload);
      }));
    }));
  }));
  r1.end(payload);
}));


process.on('exit', onExit);

function onExit() {
  hooks.disable();
  hooks.sanityCheck();
  const activities = hooks.activities;

  // Verify both invocations
  const first = activities.filter((x) => x.uid === asyncIdAtFirstReq)[0];
  checkInvocations(first, { init: 1, destroy: 1 }, 'when process exits');

  const second = activities.filter((x) => x.uid === asyncIdAtSecondReq)[0];
  checkInvocations(second, { init: 1, destroy: 1 }, 'when process exits');

  // Verify reuse handle has been wrapped
  strictEqual(first.type, second.type);
  ok(first.handle !== second.handle, 'Resource reused');
  ok(first.handle === second.handle.handle,
            'Resource not wrapped correctly');
}
