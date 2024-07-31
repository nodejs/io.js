'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');

// Http2ServerResponse.writeHead should support arrays and nested arrays

{
  const server = http2.createServer();
  server.listen(0, common.mustCall(() => {
    const port = server.address().port;

    server.once('request', common.mustCall((request, response) => {
      const returnVal = response.writeHead(200, [
        ['foo', 'bar'],
        ['foo', 'baz'],
        ['ABC', 123],
      ]);
      assert.strictEqual(returnVal, response);
      response.end(common.mustCall(() => { server.close(); }));
    }));

    const client = http2.connect(`http://localhost:${port}`, common.mustCall(() => {
      const request = client.request();

      request.on('response', common.mustCall((headers) => {
        assert.strictEqual(headers.foo, 'bar, baz');
        assert.strictEqual(headers.abc, '123');
        assert.strictEqual(headers[':status'], 200);
      }, 1));
      request.on('end', common.mustCall(() => {
        client.close();
      }));
      request.end();
      request.resume();
    }));
  }));
}

{
  const server = http2.createServer();
  server.listen(0, common.mustCall(() => {
    const port = server.address().port;

    server.once('request', common.mustCall((request, response) => {
      const returnVal = response.writeHead(200, ['foo', 'bar', 'foo', 'baz', 'ABC', 123]);
      assert.strictEqual(returnVal, response);
      response.end(common.mustCall(() => { server.close(); }));
    }));

    const client = http2.connect(`http://localhost:${port}`, common.mustCall(() => {
      const request = client.request();

      request.on('response', common.mustCall((headers) => {
        assert.strictEqual(headers.foo, 'bar, baz');
        assert.strictEqual(headers.abc, '123');
        assert.strictEqual(headers[':status'], 200);
      }, 1));
      request.on('end', common.mustCall(() => {
        client.close();
      }));
      request.end();
      request.resume();
    }));
  }));
}

{
  const server = http2.createServer();
  server.listen(0, common.mustCall(() => {
    const port = server.address().port;

    server.once('request', common.mustCall((request, response) => {
      try {
        response.writeHead(200, ['foo', 'bar', 'ABC', 123, 'extra']);
      } catch (err) {
        assert.strictEqual(err.code, 'ERR_INVALID_ARG_VALUE');
      }

      response.end(common.mustCall(() => { server.close(); }));
    }));

    const client = http2.connect(`http://localhost:${port}`, common.mustCall(() => {
      const request = client.request();

      request.on('end', common.mustCall(() => {
        client.close();
      }));
      request.end();
      request.resume();
    }));
  }));
}
