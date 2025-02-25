import { mustCall } from '../common/index.mjs';
import { strictEqual, ok } from 'assert';
import { createHook } from 'async_hooks';
import { createServer, get } from 'http';

// Verify that resource emitted for an HTTPParser is not reused.
// Verify that correct create/destroy events are emitted.

const reused = Symbol('reused');

const reusedParser = [];
const incomingMessageParser = [];
const clientRequestParser = [];
const dupDestroys = [];
const destroyed = [];

createHook({
  init(asyncId, type, triggerAsyncId, resource) {
    switch (type) {
      case 'HTTPINCOMINGMESSAGE':
        incomingMessageParser.push(asyncId);
        break;
      case 'HTTPCLIENTREQUEST':
        clientRequestParser.push(asyncId);
        break;
    }

    if (resource[reused]) {
      reusedParser.push(
        `resource reused: ${asyncId}, ${triggerAsyncId}, ${type}`
      );
    }
    resource[reused] = true;
  },
  destroy(asyncId) {
    if (destroyed.includes(asyncId)) {
      dupDestroys.push(asyncId);
    } else {
      destroyed.push(asyncId);
    }
  }
}).enable();

const server = createServer((req, res) => {
  res.end();
});

server.listen(0, mustCall(() => {
  const PORT = server.address().port;
  const url = `http://127.0.0.1:${PORT}`;
  get(url, mustCall(() => {
    server.close(mustCall(() => {
      server.listen(PORT, mustCall(() => {
        get(url, mustCall(() => {
          server.close(mustCall(() => {
            setTimeout(mustCall(verify), 200);
          }));
        }));
      }));
    }));
  }));
}));

function verify() {
  strictEqual(reusedParser.length, 0);

  strictEqual(incomingMessageParser.length, 2);
  strictEqual(clientRequestParser.length, 2);

  strictEqual(dupDestroys.length, 0);
  incomingMessageParser.forEach((id) => ok(destroyed.includes(id)));
  clientRequestParser.forEach((id) => ok(destroyed.includes(id)));
}
