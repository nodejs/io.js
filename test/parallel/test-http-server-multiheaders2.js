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
// Verify that the HTTP server implementation handles multiple instances
// of the same header as per RFC2616: joining the handful of fields by ', '
// that support it, and dropping duplicates for other fields.

require('../common');
const assert = require('assert');
const http = require('http');

const multipleAllowed = [
  'Accept',
  'Accept-Charset',
  'Accept-Encoding',
  'Accept-Language',
  'Connection',
  'Cookie',
  'DAV', // GH-2750
  'Pragma', // GH-715
  'Link', // GH-1187
  'WWW-Authenticate', // GH-1083
  'Proxy-Authenticate', // GH-4052
  'Sec-Websocket-Extensions', // GH-2764
  'Sec-Websocket-Protocol', // GH-2764
  'Via', // GH-6660

  // not a special case, just making sure it's parsed correctly
  'X-Forwarded-For',

  // Make sure that unspecified headers is treated as multiple
  'Some-Random-Header',
  'X-Some-Random-Header',
];

const multipleForbidden = [
  'Content-Type',
  'User-Agent',
  'Referer',
  'Host',
  'Authorization',
  'Proxy-Authorization',
  'If-Modified-Since',
  'If-Unmodified-Since',
  'From',
  'Location',
  'Max-Forwards',

  // Special case, tested differently
  // 'Content-Length',
];

const server = http.createServer(function(req, res) {
  for (const header of multipleForbidden) {
    assert.strictEqual(req.headers[header.toLowerCase()], 'foo',
                       `header parsed incorrectly: ${header}`);
  }
  for (const header of multipleAllowed) {
    const sep = (header.toLowerCase() === 'cookie' ? '; ' : ', ');
    assert.strictEqual(req.headers[header.toLowerCase()], `foo${sep}bar`,
                       `header parsed incorrectly: ${header}`);
  }

  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('EOF');

  server.close();
});

function makeHeader(value) {
  return function(header) {
    return [header, value];
  };
}

const headers = []
  .concat(multipleAllowed.map(makeHeader('foo')))
  .concat(multipleForbidden.map(makeHeader('foo')))
  .concat(multipleAllowed.map(makeHeader('bar')))
  .concat(multipleForbidden.map(makeHeader('bar')));

server.listen(0, function() {
  http.get({
    host: 'localhost',
    port: this.address().port,
    path: '/',
    headers: headers,
  });
});
