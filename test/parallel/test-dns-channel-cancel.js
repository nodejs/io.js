'use strict';
const common = require('../common');
const { Resolver } = require('dns');
const assert = require('assert');
const dgram = require('dgram');

const server = dgram.createSocket('udp4');
const resolver = new Resolver();

const desiredQueries = 11;
let finishedQueries = 0;

server.bind(0, common.mustCall(async () => {
  resolver.setServers([`127.0.0.1:${server.address().port}`]);

  const callback = common.mustCall((err, res) => {
    assert.strictEqual(err.code, 'ECANCELLED');
    assert.strictEqual(err.syscall, 'queryA');
    assert.strictEqual(err.hostname, `example${finishedQueries}.org`);

    finishedQueries++;
    if (finishedQueries === desiredQueries) {
      server.close();
    }
  }, desiredQueries);

  const next = (...args) => {
    callback(...args);

    server.once('message', () => {
      resolver.cancel();
    });

    // Multiple queries
    for (let i = 1; i < desiredQueries; i++) {
      resolver.resolve4(`example${i}.org`, callback);
    }
  };

  server.once('message', () => {
    resolver.cancel();
  });

  // Single query
  resolver.resolve4('example0.org', next);
}));
