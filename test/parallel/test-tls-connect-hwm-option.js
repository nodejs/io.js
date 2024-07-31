'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');
const fixtures = require('../common/fixtures');

const pem = (n) => fixtures.readKey(`${n}.pem`);

let clients = 0;

const server = tls.createServer({
  key: pem('agent1-key'),
  cert: pem('agent1-cert')
}, common.mustCall(() => {
  if (--clients === 0)
    server.close();
}, 3));

server.listen(0, common.mustCall(() => {
  clients++;
  const highBob = tls.connect({
    port: server.address().port,
    rejectUnauthorized: false,
    highWaterMark: 128000,
  }, common.mustCall(() => {
    assert.strictEqual(highBob.readableHighWaterMark, 128000);
    highBob.end();
  }));

  clients++;
  const defaultHighBob = tls.connect({
    port: server.address().port,
    rejectUnauthorized: false,
    highWaterMark: undefined,
  }, common.mustCall(() => {
    assert.strictEqual(defaultHighBob.readableHighWaterMark, process.platform === 'win32' ? 16 * 1024 : 64 * 1024);
    defaultHighBob.end();
  }));

  clients++;
  const zeroHighBob = tls.connect({
    port: server.address().port,
    rejectUnauthorized: false,
    highWaterMark: 0,
  }, common.mustCall(() => {
    assert.strictEqual(zeroHighBob.readableHighWaterMark, 0);
    zeroHighBob.end();
  }));
}));
