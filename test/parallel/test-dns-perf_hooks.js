'use strict';

const common = require('../common');
const assert = require('assert');
const dns = require('dns');
const { PerformanceObserver } = require('perf_hooks');

const entries = [];
const obs = new PerformanceObserver((items) => {
  entries.push(...items.getEntries());
});

obs.observe({ type: 'dns' });

let count = 0;

function inc() {
  count++;
}

// If DNS resolution fails, skip it
// https://github.com/nodejs/node/issues/44003
dns.lookup('localhost', common.mustCall((err) => { !err && inc(); }));
dns.lookupService('127.0.0.1', 80, common.mustCall((err) => { !err && inc(); }));
dns.resolveAny('localhost', common.mustCall((err) => { !err && inc(); }));

dns.promises.lookup('localhost').then(inc).catch(() => {});
dns.promises.lookupService('127.0.0.1', 80).then(inc).catch(() => {});
dns.promises.resolveAny('localhost').then(inc).catch(() => {});

process.on('exit', () => {
  assert.strictEqual(entries.length, count);
  entries.forEach((entry) => {
    assert.strictEqual(!!entry.name, true);
    assert.strictEqual(entry.entryType, 'dns');
    assert.strictEqual(typeof entry.startTime, 'number');
    assert.strictEqual(typeof entry.duration, 'number');
    assert.strictEqual(typeof entry.detail, 'object');
    switch (entry.name) {
      case 'lookup':
        assert.strictEqual(typeof entry.detail.hostname, 'string');
        assert.strictEqual(typeof entry.detail.family, 'number');
        assert.strictEqual(typeof entry.detail.hints, 'number');
        assert.strictEqual(typeof entry.detail.verbatim, 'boolean');
        assert.strictEqual(typeof entry.detail.order, 'string');
        assert.strictEqual(Array.isArray(entry.detail.addresses), true);
        break;
      case 'lookupService':
        assert.strictEqual(typeof entry.detail.host, 'string');
        assert.strictEqual(typeof entry.detail.port, 'number');
        assert.strictEqual(typeof entry.detail.hostname, 'string');
        assert.strictEqual(typeof entry.detail.service, 'string');
        break;
      case 'queryAny':
        assert.strictEqual(typeof entry.detail.host, 'string');
        assert.strictEqual(typeof entry.detail.ttl, 'boolean');
        assert.strictEqual(Array.isArray(entry.detail.result), true);
        break;
    }
  });
});
