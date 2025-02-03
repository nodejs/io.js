'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const async_hooks = require('async_hooks');

const storage = new async_hooks.AsyncLocalStorage();

const {
  HTTP2_HEADER_CONTENT_TYPE,
  HTTP2_HEADER_PATH,
  HTTP2_HEADER_STATUS,
} = http2.constants;

const server = http2.createServer();
server.on('stream', (stream) => {
  stream.respond({
    [HTTP2_HEADER_CONTENT_TYPE]: 'text/plain; charset=utf-8',
    [HTTP2_HEADER_STATUS]: 200
  });
  stream.on('error', common.mustNotCall());
  stream.end('data');
});

server.listen(0, async () => {
  const client = storage.run({ id: 0 }, () => http2.connect(`http://localhost:${server.address().port}`));

  async function doReq(id) {
    const req = client.request({ [HTTP2_HEADER_PATH]: '/' });

    req.on('response', common.mustCall((headers) => {
      assert.strictEqual(headers[HTTP2_HEADER_STATUS], 200);
      assert.strictEqual(id, storage.getStore().id);
    }));
    req.on('data', common.mustCall((data) => {
      assert.strictEqual(data.toString(), 'data');
      assert.strictEqual(id, storage.getStore().id);
    }));
    req.on('end', common.mustCall(() => {
      assert.strictEqual(id, storage.getStore().id);
      server.close();
      client.close();
    }));
  }

  function doReqWith(id) {
    storage.run({ id }, () => doReq(id));
  }

  doReqWith(1);
  doReqWith(2);
});
