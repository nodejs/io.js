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
require('../common');
const assert = require('assert');
const net = require('net');
const http = require('http');
const url = require('url');
const qs = require('querystring');

const invalid_options = [ 'foo', 42, true, [] ];

invalid_options.forEach((option) => {
  assert.throws(() => {
    new http.Server(option);
  }, {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

let request_number = 0;
let requests_sent = 0;
let server_response = '';
let client_got_eof = false;

const server = http.createServer(function(req, res) {
  res.id = request_number;
  req.id = request_number++;

  assert.strictEqual(res.req, req);

  if (req.id === 0) {
    assert.strictEqual(req.method, 'GET');
    assert.strictEqual(url.parse(req.url).pathname, '/hello');
    assert.strictEqual(qs.parse(url.parse(req.url).query).hello, 'world');
    assert.strictEqual(qs.parse(url.parse(req.url).query).foo, 'b==ar');
  }

  if (req.id === 1) {
    assert.strictEqual(req.method, 'POST');
    assert.strictEqual(url.parse(req.url).pathname, '/quit');
  }

  if (req.id === 2) {
    assert.strictEqual(req.headers['x-x'], 'foo');
  }

  if (req.id === 3) {
    assert.strictEqual(req.headers['x-x'], 'bar');
    this.close();
  }

  setTimeout(function() {
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.write(url.parse(req.url).pathname);
    res.end();
  }, 1);

});
server.listen(0);

server.httpAllowHalfOpen = true;

server.on('listening', function() {
  const c = net.createConnection(this.address().port);

  c.setEncoding('utf8');

  c.on('connect', function() {
    c.write(
      'GET /hello?hello=world&foo=b==ar HTTP/1.1\r\n' +
      'Host: example.com\r\n\r\n');
    requests_sent += 1;
  });

  c.on('data', function(chunk) {
    server_response += chunk;

    if (requests_sent === 1) {
      c.write(
        'POST /quit HTTP/1.1\r\n' +
        'Host: example.com\r\n\r\n'
      );
      requests_sent += 1;
    }

    if (requests_sent === 2) {
      c.write('GET / HTTP/1.1\r\nX-X: foo\r\nHost: example.com\r\n\r\n' +
              'GET / HTTP/1.1\r\nX-X: bar\r\nHost: example.com\r\n\r\n');
      // Note: we are making the connection half-closed here
      // before we've gotten the response from the server. This
      // is a pretty bad thing to do and not really supported
      // by many http servers. Node supports it optionally if
      // you set server.httpAllowHalfOpen=true, which we've done
      // above.
      c.end();
      assert.strictEqual(c.readyState, 'readOnly');
      requests_sent += 2;
    }

  });

  c.on('end', function() {
    client_got_eof = true;
  });

  c.on('close', function() {
    assert.strictEqual(c.readyState, 'closed');
  });
});

process.on('exit', function() {
  assert.strictEqual(request_number, 4);
  assert.strictEqual(requests_sent, 4);

  const hello = new RegExp('/hello');
  assert.match(server_response, hello);

  const quit = new RegExp('/quit');
  assert.match(server_response, quit);

  assert.strictEqual(client_got_eof, true);
  assert.strictEqual(server.close(), server);
});
