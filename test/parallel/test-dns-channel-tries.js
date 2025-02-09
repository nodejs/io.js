'use strict';
const common = require('../common');
const assert = require('assert');
const dgram = require('dgram');
const dns = require('dns');

const TRIES = 1;
const IMPRECISION_MS = 500;

// Tests for dns.Resolver option `tries`.
// This will roughly test if a single try fails after the set `timeout`.

for (const ctor of [dns.Resolver, dns.promises.Resolver]) {
  for (const tries of [null, true, false, '', '2']) {
    assert.throws(() => new ctor({ tries }), {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
    });
  }

  for (const tries of [-2, 0, 4.2, 2 ** 31]) {
    assert.throws(() => new ctor({ tries }), {
      code: 'ERR_OUT_OF_RANGE',
      name: 'RangeError',
    });
  }

  for (const tries of [2, 1, 5]) new ctor({ tries });
}

for (let timeout of [-1, 1000, 1500, 50]) {
  const server = dgram.createSocket('udp4');
  server.bind(0, '127.0.0.1', common.mustCall(() => {
    const resolver = new dns.Resolver({ tries: TRIES, timeout });
    resolver.setServers([`127.0.0.1:${server.address().port}`]);

    const timeStart = performance.now();
    resolver.resolve4('nodejs.org', common.mustCall((err) => {
      assert.throws(() => { throw err; }, {
        code: 'ETIMEOUT',
        name: 'Error',
      });
      const timeEnd = performance.now();

      // c-ares default (-1): 5000ms
      if (timeout === -1) timeout = 5000;

      // Adding 500ms due to imprecisions
      const elapsedMs = (timeEnd - timeStart);
      assert(elapsedMs <= timeout + IMPRECISION_MS, `The expected timeout did not occur. Expecting: ${timeout + IMPRECISION_MS} But got: ${elapsedMs}`);

      server.close();
    }));
  }));
}

for (let timeout of [-1, 1000, 1500, 50]) {
  const server = dgram.createSocket('udp4');
  server.bind(0, '127.0.0.1', common.mustCall(() => {
    const resolver = new dns.promises.Resolver({ tries: TRIES, timeout });
    resolver.setServers([`127.0.0.1:${server.address().port}`]);
    const timeStart = performance.now();
    resolver.resolve4('nodejs.org').catch(common.mustCall((err) => {
      assert.throws(() => { throw err; }, {
        code: 'ETIMEOUT',
        name: 'Error',
      });
      const timeEnd = performance.now();

      // c-ares default (-1): 5000ms
      if (timeout === -1) timeout = 5000;

      // Adding 500ms due to imprecisions
      const elapsedMs = (timeEnd - timeStart);
      assert(elapsedMs <= timeout + IMPRECISION_MS, `The expected timeout did not occur. Expecting: ${timeout + IMPRECISION_MS} But got: ${elapsedMs}`);

      server.close();
    }));
  }));
}
