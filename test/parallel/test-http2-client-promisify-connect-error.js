'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const util = require('util');

const server = http2.createServer();

server.listen(0, common.mustCall(() => {
  const port = server.address().port;
  server.close(() => {
    const connect = util.promisify(http2.connect);
    connect(`http://localhost:${port}`)
      .then(common.mustNotCall('Promise should not be resolved'))
      .catch(common.mustCall((err) => {
        assert(err instanceof Error);
        assert.strictEqual(err.code, 'ECONNREFUSED');
      }));
  });
}));
