'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

// This tests that the http2 server sends data early when it accumulates
// enough from ongoing requests to avoid DoS as mitigation for
// CVE-2019-9517 and CVE-2019-9511.
// Added by https://github.com/nodejs/node/commit/8a4a193
const fixtures = require('../common/fixtures');
const assert = require('assert');
const http2 = require('http2');

const content = fixtures.readSync('person-large.jpg');

const server = http2.createServer({
  maxSessionMemory: 1000
});
let streamCount = 0;
server.on('stream', (stream, headers) => {
  stream.respond({
    'content-type': 'image/jpeg',
    ':status': 200
  });
  stream.end(content);
  console.log('server sends content', ++streamCount);
});

server.listen(0, common.mustCall(() => {
  const client = http2.connect(`http://localhost:${server.address().port}/`);

  let endCount = 0;
  let finished = 0;
  for (let i = 0; i < 100; i++) {
    const req = client.request({ ':path': '/' }).end();
    const chunks = [];
    req.on('data', (chunk) => {
      chunks.push(chunk);
    });
    req.on('end', common.mustCall(() => {
      console.log('client receives content', ++endCount);
      assert.deepStrictEqual(Buffer.concat(chunks), content);

      if (++finished === 100) {
        client.close();
        server.close();
      }
    }));
    req.on('error', (e) => {
      console.log('client error', e);
    });
  }
}));
