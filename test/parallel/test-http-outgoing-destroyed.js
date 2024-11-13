'use strict';
const common = require('../common');
const http = require('http');
const assert = require('assert');

{
  const server = http.createServer(common.mustCall((req, res) => {
    assert.strictEqual(res.closed, false);
    req.pipe(res);
    res.on('error', common.mustNotCall());
    res.on('close', common.mustCall(() => {
      assert.strictEqual(res.closed, true);
      res.end('asd');
      process.nextTick(() => {
        server.close();
      });
    }));
  })).listen(0, () => {
    http
      .request({
        port: server.address().port,
        method: 'PUT'
      })
      .on('response', (res) => {
        res.destroy();
      })
      .write('asd');
  });
}

{
  const server = http.createServer(common.mustCall((req, res) => {
    assert.strictEqual(res.closed, false);
    req.pipe(res);
    res.on('error', common.mustNotCall());
    res.on('close', common.mustCall(() => {
      assert.strictEqual(res.closed, true);
      process.nextTick(() => {
        server.close();
      });
    }));
    const err = new Error('Destroy test');
    res.destroy(err);
    assert.strictEqual(res.errored, err);
  })).listen(0, () => {
    http
      .request({
        port: server.address().port,
        method: 'PUT'
      })
      .on('error', common.mustCall())
      .write('asd');
  });
}

{
  const server = http.createServer(common.mustCall((req, res) => {
    assert.strictEqual(res.closed, false);
    res.end();
    res.destroy();
    // Make sure not to emit 'error' after .destroy().
    res.end('asd');
    assert.strictEqual(res.errored, undefined);
  })).listen(0, () => {
    http
      .request({
        port: server.address().port,
        method: 'GET'
      })
      .on('response', common.mustCall((res) => {
        res.resume().on('end', common.mustCall(() => {
          server.close();
        }));
      }))
      .end();
  });
}
