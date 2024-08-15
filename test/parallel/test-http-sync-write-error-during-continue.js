'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');
const { duplexPair } = require('stream');

// Regression test for the crash reported in
// https://github.com/nodejs/node/issues/15102 (httpParser.finish() is called
// during httpParser.execute()):

{
  const [ clientSide, serverSide ] = duplexPair();

  serverSide.on('data', common.mustCall((data) => {
    assert.strictEqual(data.toString('utf8'), `\
GET / HTTP/1.1
Expect: 100-continue
Host: localhost:80
Connection: close

`.replace(/\n/g, '\r\n'));

    setImmediate(() => {
      serverSide.write('HTTP/1.1 100 Continue\r\n\r\n');
    });
  }));

  const req = http.request({
    createConnection: common.mustCall(() => clientSide),
    headers: {
      'Expect': '100-continue'
    }
  });
  req.on('continue', common.mustCall((res) => {
    let sync = true;

    clientSide._writev = null;
    clientSide._write = common.mustCall((chunk, enc, cb) => {
      assert(sync);
      // On affected versions of Node.js, the error would be emitted on `req`
      // synchronously (i.e. before commit f663b31cc2aec), which would cause
      // parser.finish() to be called while we are here in the 'continue'
      // callback, which is inside a parser.execute() call.

      assert.strictEqual(chunk.length, 4);
      clientSide.destroy(new Error('sometimes the code just doesn’t work'), cb);
    });
    req.on('error', common.mustCall());
    req.end('data');

    sync = false;
  }));
}
