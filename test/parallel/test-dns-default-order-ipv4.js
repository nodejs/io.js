// Flags: --expose-internals --dns-result-order=ipv4first
'use strict';
const common = require('../common');
const assert = require('assert');
const { internalBinding } = require('internal/test/binding');
const cares = internalBinding('cares_wrap');
const { promisify } = require('util');

// Test that --dns-result-order=ipv4first works as expected.

const originalGetaddrinfo = cares.getaddrinfo;
const calls = [];
cares.getaddrinfo = common.mustCallAtLeast((...args) => {
  calls.push(args);
  originalGetaddrinfo(...args);
}, 1);

const dns = require('dns');
const dnsPromises = dns.promises;

// We want to test the parameter of order only so that we
// ignore possible errors here.
function allowFailed(fn) {
  return fn.catch((_err) => {
    //
  });
}

(async () => {
  let callsLength = 0;
  const checkParameter = (expected) => {
    assert.strictEqual(calls.length, callsLength + 1);
    const order = calls[callsLength][4];
    assert.strictEqual(order, expected);
    callsLength += 1;
  };

  await allowFailed(promisify(dns.lookup)('example.org'));
  checkParameter(cares.DNS_ORDER_IPV4_FIRST);

  await allowFailed(dnsPromises.lookup('example.org'));
  checkParameter(cares.DNS_ORDER_IPV4_FIRST);

  await allowFailed(promisify(dns.lookup)('example.org', {}));
  checkParameter(cares.DNS_ORDER_IPV4_FIRST);

  await allowFailed(dnsPromises.lookup('example.org', {}));
  checkParameter(cares.DNS_ORDER_IPV4_FIRST);
})().then(common.mustCall());
