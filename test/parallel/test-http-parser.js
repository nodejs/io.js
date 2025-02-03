// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const { mustCall, mustNotCall } = require('../common');
const assert = require('assert');

const { methods, HTTPParser } = require('_http_common');
const { REQUEST, RESPONSE } = HTTPParser;

const kOnHeaders = HTTPParser.kOnHeaders | 0;
const kOnHeadersComplete = HTTPParser.kOnHeadersComplete | 0;
const kOnBody = HTTPParser.kOnBody | 0;
const kOnMessageComplete = HTTPParser.kOnMessageComplete | 0;

// The purpose of this test is not to check HTTP compliance but to test the
// binding. Tests for pathological http messages should be submitted
// upstream to https://github.com/joyent/http-parser for inclusion into
// deps/http-parser/test.c


function newParser(type) {
  const parser = new HTTPParser();
  parser.initialize(type, {});

  parser.headers = [];
  parser.url = '';

  parser[kOnHeaders] = function(headers, url) {
    parser.headers = parser.headers.concat(headers);
    parser.url += url;
  };

  parser[kOnHeadersComplete] = function() {
  };

  parser[kOnBody] = mustNotCall('kOnBody should not be called');

  parser[kOnMessageComplete] = function() {
  };

  return parser;
}


function expectBody(expected) {
  return mustCall(function(buf) {
    const body = String(buf);
    assert.strictEqual(body, expected);
  });
}


//
// Simple request test.
//
{
  const request = Buffer.from('GET /hello HTTP/1.1\r\n\r\n');

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url) => {
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
    assert.strictEqual(method, methods.indexOf('GET'));
    assert.strictEqual(url || parser.url, '/hello');
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser.execute(request, 0, request.length);

  //
  // Check that if we throw an error in the callbacks that error will be
  // thrown from parser.execute()
  //

  parser[kOnHeadersComplete] = function() {
    throw new Error('hello world');
  };

  parser.initialize(REQUEST, {});

  assert.throws(
    () => { parser.execute(request, 0, request.length); },
    { name: 'Error', message: 'hello world' }
  );
}


//
// Simple response test.
//
{
  const request = Buffer.from(
    'HTTP/1.1 200 OK\r\n' +
    'Content-Type: text/plain\r\n' +
    'Content-Length: 4\r\n' +
    '\r\n' +
    'pong'
  );

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url, statusCode, statusMessage) => {
    assert.strictEqual(method, undefined);
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
    assert.strictEqual(statusCode, 200);
    assert.strictEqual(statusMessage, 'OK');
  };

  const onBody = (buf) => {
    const body = String(buf);
    assert.strictEqual(body, 'pong');
  };

  const parser = newParser(RESPONSE);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser[kOnBody] = mustCall(onBody);
  parser.execute(request, 0, request.length);
}


//
// Response with no headers.
//
{
  const request = Buffer.from(
    'HTTP/1.0 200 Connection established\r\n\r\n');

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url, statusCode, statusMessage) => {
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 0);
    assert.strictEqual(method, undefined);
    assert.strictEqual(statusCode, 200);
    assert.strictEqual(statusMessage, 'Connection established');
    assert.deepStrictEqual(headers || parser.headers, []);
  };

  const parser = newParser(RESPONSE);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser.execute(request, 0, request.length);
}


//
// Trailing headers.
//
{
  const request = Buffer.from(
    'POST /it HTTP/1.1\r\n' +
    'Transfer-Encoding: chunked\r\n' +
    '\r\n' +
    '4\r\n' +
    'ping\r\n' +
    '0\r\n' +
    'Vary: *\r\n' +
    'Content-Type: text/plain\r\n' +
    '\r\n'
  );

  let seen_body = false;

  const onHeaders = (headers) => {
    assert.ok(seen_body); // Trailers should come after the body
    assert.deepStrictEqual(headers,
                           ['Vary', '*', 'Content-Type', 'text/plain']);
  };

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url) => {
    assert.strictEqual(method, methods.indexOf('POST'));
    assert.strictEqual(url || parser.url, '/it');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
    // Expect to see trailing headers now
    parser[kOnHeaders] = mustCall(onHeaders);
  };

  const onBody = (buf) => {
    const body = String(buf);
    assert.strictEqual(body, 'ping');
    seen_body = true;
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser[kOnBody] = mustCall(onBody);
  parser.execute(request, 0, request.length);
}


//
// Test header ordering.
//
{
  const request = Buffer.from(
    'GET / HTTP/1.0\r\n' +
    'X-Filler: 1337\r\n' +
    'X-Filler:   42\r\n' +
    'X-Filler2:  42\r\n' +
    '\r\n'
  );

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method) => {
    assert.strictEqual(method, methods.indexOf('GET'));
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 0);
    assert.deepStrictEqual(
      headers || parser.headers,
      ['X-Filler', '1337', 'X-Filler', '42', 'X-Filler2', '42']);
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser.execute(request, 0, request.length);
}


//
// Test large number of headers
//
{
  // 256 X-Filler headers
  const lots_of_headers = 'X-Filler: 42\r\n'.repeat(256);

  const request = Buffer.from(
    'GET /foo/bar/baz?quux=42#1337 HTTP/1.0\r\n' +
    lots_of_headers +
    '\r\n'
  );

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url) => {
    assert.strictEqual(method, methods.indexOf('GET'));
    assert.strictEqual(url || parser.url, '/foo/bar/baz?quux=42#1337');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 0);

    headers ||= parser.headers;

    assert.strictEqual(headers.length, 2 * 256); // 256 key/value pairs
    for (let i = 0; i < headers.length; i += 2) {
      assert.strictEqual(headers[i], 'X-Filler');
      assert.strictEqual(headers[i + 1], '42');
    }
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser.execute(request, 0, request.length);
}


//
// Test request body
//
{
  const request = Buffer.from(
    'POST /it HTTP/1.1\r\n' +
    'Content-Type: application/x-www-form-urlencoded\r\n' +
    'Content-Length: 15\r\n' +
    '\r\n' +
    'foo=42&bar=1337'
  );

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url) => {
    assert.strictEqual(method, methods.indexOf('POST'));
    assert.strictEqual(url || parser.url, '/it');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
  };

  const onBody = (buf) => {
    const body = String(buf);
    assert.strictEqual(body, 'foo=42&bar=1337');
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser[kOnBody] = mustCall(onBody);
  parser.execute(request, 0, request.length);
}


//
// Test chunked request body
//
{
  const request = Buffer.from(
    'POST /it HTTP/1.1\r\n' +
    'Content-Type: text/plain\r\n' +
    'Transfer-Encoding: chunked\r\n' +
    '\r\n' +
    '3\r\n' +
    '123\r\n' +
    '6\r\n' +
    '123456\r\n' +
    'A\r\n' +
    '1234567890\r\n' +
    '0\r\n'
  );

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url) => {
    assert.strictEqual(method, methods.indexOf('POST'));
    assert.strictEqual(url || parser.url, '/it');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
  };

  let body_part = 0;
  const body_parts = ['123', '123456', '1234567890'];

  const onBody = (buf) => {
    const body = String(buf);
    assert.strictEqual(body, body_parts[body_part++]);
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser[kOnBody] = mustCall(onBody, body_parts.length);
  parser.execute(request, 0, request.length);
}


//
// Test chunked request body spread over multiple buffers (packets)
//
{
  let request = Buffer.from(
    'POST /it HTTP/1.1\r\n' +
    'Content-Type: text/plain\r\n' +
    'Transfer-Encoding: chunked\r\n' +
    '\r\n' +
    '3\r\n' +
    '123\r\n' +
    '6\r\n' +
    '123456\r\n'
  );

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url) => {
    assert.strictEqual(method, methods.indexOf('POST'));
    assert.strictEqual(url || parser.url, '/it');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
  };

  let body_part = 0;
  const body_parts =
          ['123', '123456', '123456789', '123456789ABC', '123456789ABCDEF'];

  const onBody = (buf) => {
    const body = String(buf);
    assert.strictEqual(body, body_parts[body_part++]);
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser[kOnBody] = mustCall(onBody, body_parts.length);
  parser.execute(request, 0, request.length);

  request = Buffer.from(
    '9\r\n' +
    '123456789\r\n' +
    'C\r\n' +
    '123456789ABC\r\n' +
    'F\r\n' +
    '123456789ABCDEF\r\n' +
    '0\r\n'
  );

  parser.execute(request, 0, request.length);
}


//
// Stress test.
//
{
  const request = Buffer.from(
    'POST /helpme HTTP/1.1\r\n' +
    'Content-Type: text/plain\r\n' +
    'Transfer-Encoding: chunked\r\n' +
    '\r\n' +
    '3\r\n' +
    '123\r\n' +
    '6\r\n' +
    '123456\r\n' +
    '9\r\n' +
    '123456789\r\n' +
    'C\r\n' +
    '123456789ABC\r\n' +
    'F\r\n' +
    '123456789ABCDEF\r\n' +
    '0\r\n'
  );

  function test(a, b) {
    const onHeadersComplete = (versionMajor, versionMinor, headers,
                               method, url) => {
      assert.strictEqual(method, methods.indexOf('POST'));
      assert.strictEqual(url || parser.url, '/helpme');
      assert.strictEqual(versionMajor, 1);
      assert.strictEqual(versionMinor, 1);
    };

    let expected_body = '123123456123456789123456789ABC123456789ABCDEF';

    const onBody = (buf) => {
      const chunk = String(buf);
      assert.strictEqual(expected_body.indexOf(chunk), 0);
      expected_body = expected_body.slice(chunk.length);
    };

    const parser = newParser(REQUEST);
    parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
    parser[kOnBody] = onBody;
    parser.execute(a, 0, a.length);
    parser.execute(b, 0, b.length);

    assert.strictEqual(expected_body, '');
  }

  for (let i = 1; i < request.length - 1; ++i) {
    const a = request.slice(0, i);
    const b = request.slice(i);
    test(a, b);
  }
}


//
// Byte by byte test.
//
{
  const request = Buffer.from(
    'POST /it HTTP/1.1\r\n' +
    'Content-Type: text/plain\r\n' +
    'Transfer-Encoding: chunked\r\n' +
    '\r\n' +
    '3\r\n' +
    '123\r\n' +
    '6\r\n' +
    '123456\r\n' +
    '9\r\n' +
    '123456789\r\n' +
    'C\r\n' +
    '123456789ABC\r\n' +
    'F\r\n' +
    '123456789ABCDEF\r\n' +
    '0\r\n'
  );

  const onHeadersComplete = (versionMajor, versionMinor, headers,
                             method, url) => {
    assert.strictEqual(method, methods.indexOf('POST'));
    assert.strictEqual(url || parser.url, '/it');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
    assert.deepStrictEqual(
      headers || parser.headers,
      ['Content-Type', 'text/plain', 'Transfer-Encoding', 'chunked']);
  };

  let expected_body = '123123456123456789123456789ABC123456789ABCDEF';

  const onBody = (buf) => {
    const chunk = String(buf);
    assert.strictEqual(expected_body.indexOf(chunk), 0);
    expected_body = expected_body.slice(chunk.length);
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = mustCall(onHeadersComplete);
  parser[kOnBody] = onBody;

  for (let i = 0; i < request.length; ++i) {
    parser.execute(request, i, 1);
  }

  assert.strictEqual(expected_body, '');
}


//
// Test parser reinit sequence.
//
{
  const req1 = Buffer.from(
    'PUT /this HTTP/1.1\r\n' +
    'Content-Type: text/plain\r\n' +
    'Transfer-Encoding: chunked\r\n' +
    '\r\n' +
    '4\r\n' +
    'ping\r\n' +
    '0\r\n'
  );

  const req2 = Buffer.from(
    'POST /that HTTP/1.0\r\n' +
    'Content-Type: text/plain\r\n' +
    'Content-Length: 4\r\n' +
    '\r\n' +
    'pong'
  );

  const onHeadersComplete1 = (versionMajor, versionMinor, headers,
                              method, url) => {
    assert.strictEqual(method, methods.indexOf('PUT'));
    assert.strictEqual(url, '/this');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 1);
    assert.deepStrictEqual(
      headers,
      ['Content-Type', 'text/plain', 'Transfer-Encoding', 'chunked']);
  };

  const onHeadersComplete2 = (versionMajor, versionMinor, headers,
                              method, url) => {
    assert.strictEqual(method, methods.indexOf('POST'));
    assert.strictEqual(url, '/that');
    assert.strictEqual(versionMajor, 1);
    assert.strictEqual(versionMinor, 0);
    assert.deepStrictEqual(
      headers,
      ['Content-Type', 'text/plain', 'Content-Length', '4']
    );
  };

  const parser = newParser(REQUEST);
  parser[kOnHeadersComplete] = onHeadersComplete1;
  parser[kOnBody] = expectBody('ping');
  parser.execute(req1, 0, req1.length);

  parser.initialize(REQUEST, req2);
  parser[kOnBody] = expectBody('pong');
  parser[kOnHeadersComplete] = onHeadersComplete2;
  parser.execute(req2, 0, req2.length);
}

// Test parser 'this' safety
// https://github.com/joyent/node/issues/6690
assert.throws(function() {
  const request = Buffer.from('GET /hello HTTP/1.1\r\n\r\n');

  const parser = newParser(REQUEST);
  const notparser = { execute: parser.execute };
  notparser.execute(request, 0, request.length);
}, TypeError);
