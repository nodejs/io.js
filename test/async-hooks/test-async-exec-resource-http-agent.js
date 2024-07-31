'use strict';

const common = require('../common');

const assert = require('node:assert');
const {
  executionAsyncResource,
  executionAsyncId,
  createHook,
} = require('node:async_hooks');
const http = require('node:http');

const hooked = {};
createHook({
  init(asyncId, type, triggerAsyncId, resource) {
    hooked[asyncId] = resource;
  },
}).enable();

const agent = new http.Agent({
  maxSockets: 1,
});

const server = http.createServer((req, res) => {
  res.end('ok');
});

server.listen(0, common.mustCall(() => {
  assert.strictEqual(executionAsyncResource(), hooked[executionAsyncId()]);

  http.get({ agent, port: server.address().port }, common.mustCall(() => {
    assert.strictEqual(executionAsyncResource(), hooked[executionAsyncId()]);
    server.close();
    agent.destroy();
  }));
}));
