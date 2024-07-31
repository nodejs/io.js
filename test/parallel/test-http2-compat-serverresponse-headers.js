'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const h2 = require('http2');

// Http2ServerResponse should support checking and reading custom headers

const server = h2.createServer();
server.listen(0, common.mustCall(function() {
  const port = server.address().port;
  server.once('request', common.mustCall(function(request, response) {
    const real = 'foo-bar';
    const fake = 'bar-foo';
    const denormalised = ` ${real.toUpperCase()}\n\t`;
    const expectedValue = 'abc123';

    response.setHeader(real, expectedValue);

    assert.strictEqual(response.hasHeader(real), true);
    assert.strictEqual(response.hasHeader(fake), false);
    assert.strictEqual(response.hasHeader(denormalised), true);
    assert.strictEqual(response.getHeader(real), expectedValue);
    assert.strictEqual(response.getHeader(denormalised), expectedValue);
    assert.strictEqual(response.getHeader(fake), undefined);

    response.removeHeader(fake);
    assert.strictEqual(response.hasHeader(fake), false);

    response.setHeader(real, expectedValue);
    assert.strictEqual(response.getHeader(real), expectedValue);
    assert.strictEqual(response.hasHeader(real), true);
    response.removeHeader(real);
    assert.strictEqual(response.hasHeader(real), false);

    response.setHeader(denormalised, expectedValue);
    assert.strictEqual(response.getHeader(denormalised), expectedValue);
    assert.strictEqual(response.hasHeader(denormalised), true);
    assert.strictEqual(response.hasHeader(real), true);

    response.appendHeader(real, expectedValue);
    assert.deepStrictEqual(response.getHeader(real), [
      expectedValue,
      expectedValue,
    ]);
    assert.strictEqual(response.hasHeader(real), true);

    response.removeHeader(denormalised);
    assert.strictEqual(response.hasHeader(denormalised), false);
    assert.strictEqual(response.hasHeader(real), false);

    ['hasHeader', 'getHeader', 'removeHeader'].forEach((fnName) => {
      assert.throws(
        () => response[fnName](),
        {
          code: 'ERR_INVALID_ARG_TYPE',
          name: 'TypeError',
          message: 'The "name" argument must be of type string. Received ' +
                   'undefined'
        }
      );
    });

    [
      ':status',
      ':method',
      ':path',
      ':authority',
      ':scheme',
    ].forEach((header) => assert.throws(
      () => response.setHeader(header, 'foobar'),
      {
        code: 'ERR_HTTP2_PSEUDOHEADER_NOT_ALLOWED',
        name: 'TypeError',
        message: 'Cannot set HTTP/2 pseudo-headers'
      })
    );
    assert.throws(() => {
      response.setHeader(real, null);
    }, {
      code: 'ERR_HTTP2_INVALID_HEADER_VALUE',
      name: 'TypeError',
      message: 'Invalid value "null" for header "foo-bar"'
    });
    assert.throws(() => {
      response.setHeader(real, undefined);
    }, {
      code: 'ERR_HTTP2_INVALID_HEADER_VALUE',
      name: 'TypeError',
      message: 'Invalid value "undefined" for header "foo-bar"'
    });
    assert.throws(
      () => response.setHeader(), // Header name undefined
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: 'The "name" argument must be of type string. Received ' +
                 'undefined'
      }
    );
    assert.throws(
      () => response.setHeader(''),
      {
        code: 'ERR_INVALID_HTTP_TOKEN',
        name: 'TypeError',
        message: 'Header name must be a valid HTTP token [""]'
      }
    );

    response.setHeader(real, expectedValue);
    const expectedHeaderNames = [real];
    assert.deepStrictEqual(response.getHeaderNames(), expectedHeaderNames);
    const expectedHeaders = { __proto__: null };
    expectedHeaders[real] = expectedValue;
    assert.deepStrictEqual(response.getHeaders(), expectedHeaders);

    response.getHeaders()[fake] = fake;
    assert.strictEqual(response.hasHeader(fake), false);
    assert.strictEqual(Object.getPrototypeOf(response.getHeaders()), null);

    assert.strictEqual(response.sendDate, true);
    response.sendDate = false;
    assert.strictEqual(response.sendDate, false);

    response.sendDate = true;
    assert.strictEqual(response.sendDate, true);
    response.removeHeader('Date');
    assert.strictEqual(response.sendDate, false);

    response.on('finish', common.mustCall(function() {
      assert.strictEqual(response.headersSent, true);

      assert.throws(
        () => response.setHeader(real, expectedValue),
        {
          code: 'ERR_HTTP2_HEADERS_SENT',
          name: 'Error',
          message: 'Response has already been initiated.'
        }
      );
      assert.throws(
        () => response.removeHeader(real, expectedValue),
        {
          code: 'ERR_HTTP2_HEADERS_SENT',
          name: 'Error',
          message: 'Response has already been initiated.'
        }
      );

      process.nextTick(() => {
        assert.throws(
          () => response.setHeader(real, expectedValue),
          {
            code: 'ERR_HTTP2_HEADERS_SENT',
            name: 'Error',
            message: 'Response has already been initiated.'
          }
        );
        assert.throws(
          () => response.removeHeader(real, expectedValue),
          {
            code: 'ERR_HTTP2_HEADERS_SENT',
            name: 'Error',
            message: 'Response has already been initiated.'
          }
        );

        assert.strictEqual(response.headersSent, true);
        server.close();
      });
    }));
    response.end();
  }));

  const url = `http://localhost:${port}`;
  const client = h2.connect(url, common.mustCall(function() {
    const headers = {
      ':path': '/',
      ':method': 'GET',
      ':scheme': 'http',
      ':authority': `localhost:${port}`
    };
    const request = client.request(headers);
    request.on('end', common.mustCall(function() {
      client.close();
    }));
    request.end();
    request.resume();
  }));
}));
