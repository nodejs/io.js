// Flags: --expose-gc --expose-internals
'use strict';
const common = require('../common');
const http = require('http');
const async_hooks = require('async_hooks');
const { duplexPair } = require('stream');

// Regression test for https://github.com/nodejs/node/issues/30122
// When a domain is attached to an http Agent’s ReusedHandle object, that
// domain should be kept alive through the ReusedHandle and that in turn
// through the actual underlying handle.

// Consistency check: There is a ReusedHandle being used, and it emits events.
// We also use this async hook to manually trigger GC just before the domain’s
// own `before` hook runs, in order to reproduce the bug above (the ReusedHandle
// being collected and the domain with it while the handle is still alive).
const checkInitCalled = common.mustCall();
const checkBeforeCalled = common.mustCallAtLeast();
let reusedHandleId;
async_hooks.createHook({
  init(id, type, triggerId, resource) {
    if (resource.constructor.name === 'ReusedHandle') {
      reusedHandleId = id;
      checkInitCalled();
    }
  },
  before(id) {
    if (id === reusedHandleId) {
      globalThis.gc();
      checkBeforeCalled();
    }
  }
}).enable();

// We use a DuplexPair rather than TLS sockets to keep the domain from being
// attached to too many objects that use strong references (timers, the network
// socket handle, etc.) and wrap the client side in a JSStreamSocket so we don’t
// have to implement the whole _handle API ourselves.
const [ serverSide, clientSide ] = duplexPair();
const JSStreamSocket = require('internal/js_stream_socket');
const wrappedClientSide = new JSStreamSocket(clientSide);

// Consistency check: We use asyncReset exactly once.
wrappedClientSide._handle.asyncReset =
  common.mustCall(wrappedClientSide._handle.asyncReset);

// Dummy server implementation, could be any server for this test...
const server = http.createServer(common.mustCall((req, res) => {
  res.writeHead(200, {
    'Content-Type': 'text/plain'
  });
  res.end('Hello, world!');
}, 2));
server.emit('connection', serverSide);

// HTTP Agent that only returns the fake connection.
class TestAgent extends http.Agent {
  createConnection = common.mustCall(() => wrappedClientSide);
}
const agent = new TestAgent({ keepAlive: true, maxSockets: 1 });

function makeRequest(cb) {
  const req = http.request({ agent }, common.mustCall((res) => {
    res.resume();
    res.on('end', cb);
  }));
  req.end('');
}

// The actual test starts here:

const domain = require('domain');
// Create the domain in question and a dummy “noDomain” domain that we use to
// avoid attaching new async resources to the original domain.
const d = domain.create();
const noDomain = domain.create();

d.run(common.mustCall(() => {
  // Create a first request only so that we can get a “re-used” socket later.
  makeRequest(common.mustCall(() => {
    // Schedule the second request.
    setImmediate(common.mustCall(() => {
      makeRequest(common.mustCall(() => {
        // The `setImmediate()` is run inside of `noDomain` so that it doesn’t
        // keep the actual target domain alive unnecessarily.
        noDomain.run(common.mustCall(() => {
          setImmediate(common.mustCall(() => {
            // This emits an async event on the reused socket, so it should
            // run the domain’s `before` hooks.
            // This should *not* throw an error because the domain was garbage
            // collected too early.
            serverSide.end();
          }));
        }));
      }));
    }));
  }));
}));
