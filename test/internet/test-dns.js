// Flags: --expose-internals
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
const common = require('../common');
const { addresses } = require('../common/internet');
const { internalBinding } = require('internal/test/binding');
const { getSystemErrorName } = require('util');
const assert = require('assert');
const dns = require('dns');
const net = require('net');
const isIPv4 = net.isIPv4;
const isIPv6 = net.isIPv6;
const util = require('util');
const dnsPromises = dns.promises;

let expected = 0;
let completed = 0;
let running = false;
const queue = [];


function TEST(f) {
  function next() {
    const f = queue.shift();
    if (f) {
      running = true;
      console.log(f.name);
      f(done);
    }
  }

  function done() {
    running = false;
    completed++;
    process.nextTick(next);
  }

  expected++;
  queue.push(f);

  if (!running) {
    next();
  }
}


function checkWrap(req) {
  assert.strictEqual(typeof req, 'object');
}


TEST(function test_reverse_bogus(done) {
  dnsPromises.reverse('bogus ip')
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'EINVAL');
      assert.strictEqual(getSystemErrorName(err.errno), 'EINVAL');
    }));

  assert.throws(() => {
    dns.reverse('bogus ip', common.mustNotCall());
  }, /^Error: getHostByAddr EINVAL bogus ip$/);
  done();
});

TEST(async function test_resolve4_ttl(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.strictEqual(typeof item, 'object');
      assert.strictEqual(typeof item.ttl, 'number');
      assert.strictEqual(typeof item.address, 'string');
      assert.ok(item.ttl >= 0);
      assert.ok(isIPv4(item.address));
    }
  }

  validateResult(await dnsPromises.resolve4(addresses.INET4_HOST, {
    ttl: true,
  }));

  const req = dns.resolve4(addresses.INET4_HOST, {
    ttl: true,
  }, function(err, result) {
    assert.ifError(err);
    validateResult(result);
    done();
  });

  checkWrap(req);
});

TEST(async function test_resolve6_ttl(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.strictEqual(typeof item, 'object');
      assert.strictEqual(typeof item.ttl, 'number');
      assert.strictEqual(typeof item.address, 'string');
      assert.ok(item.ttl >= 0);
      assert.ok(isIPv6(item.address));
    }
  }

  validateResult(await dnsPromises.resolve6(addresses.INET6_HOST, {
    ttl: true,
  }));

  const req = dns.resolve6(addresses.INET6_HOST, {
    ttl: true,
  }, function(err, result) {
    assert.ifError(err);
    validateResult(result);
    done();
  });

  checkWrap(req);
});

TEST(async function test_resolveMx(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.strictEqual(typeof item, 'object');
      assert.ok(item.exchange);
      assert.strictEqual(typeof item.exchange, 'string');
      assert.strictEqual(typeof item.priority, 'number');
    }
  }

  validateResult(await dnsPromises.resolveMx(addresses.MX_HOST));

  const req = dns.resolveMx(addresses.MX_HOST, function(err, result) {
    assert.ifError(err);
    validateResult(result);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveMx_failure(done) {
  dnsPromises.resolveMx(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveMx(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});

TEST(async function test_resolveNs(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.ok(item);
      assert.strictEqual(typeof item, 'string');
    }
  }

  validateResult(await dnsPromises.resolveNs(addresses.NS_HOST));

  const req = dns.resolveNs(addresses.NS_HOST, function(err, names) {
    assert.ifError(err);
    validateResult(names);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveNs_failure(done) {
  dnsPromises.resolveNs(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveNs(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});

TEST(async function test_resolveSrv(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.strictEqual(typeof item, 'object');
      assert.ok(item.name);
      assert.strictEqual(typeof item.name, 'string');
      assert.strictEqual(typeof item.port, 'number');
      assert.strictEqual(typeof item.priority, 'number');
      assert.strictEqual(typeof item.weight, 'number');
    }
  }

  validateResult(await dnsPromises.resolveSrv(addresses.SRV_HOST));

  const req = dns.resolveSrv(addresses.SRV_HOST, function(err, result) {
    assert.ifError(err);
    validateResult(result);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveSrv_failure(done) {
  dnsPromises.resolveSrv(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveSrv(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});

TEST(async function test_resolvePtr(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.ok(item);
      assert.strictEqual(typeof item, 'string');
    }
  }

  validateResult(await dnsPromises.resolvePtr(addresses.PTR_HOST));

  const req = dns.resolvePtr(addresses.PTR_HOST, function(err, result) {
    assert.ifError(err);
    validateResult(result);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolvePtr_failure(done) {
  dnsPromises.resolvePtr(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolvePtr(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});

TEST(async function test_resolveNaptr(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.strictEqual(typeof item, 'object');
      assert.strictEqual(typeof item.flags, 'string');
      assert.strictEqual(typeof item.service, 'string');
      assert.strictEqual(typeof item.regexp, 'string');
      assert.strictEqual(typeof item.replacement, 'string');
      assert.strictEqual(typeof item.order, 'number');
      assert.strictEqual(typeof item.preference, 'number');
    }
  }

  validateResult(await dnsPromises.resolveNaptr(addresses.NAPTR_HOST));

  const req = dns.resolveNaptr(addresses.NAPTR_HOST, function(err, result) {
    assert.ifError(err);
    validateResult(result);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveNaptr_failure(done) {
  dnsPromises.resolveNaptr(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveNaptr(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});

TEST(async function test_resolveSoa(done) {
  function validateResult(result) {
    assert.strictEqual(typeof result, 'object');
    assert.strictEqual(typeof result.nsname, 'string');
    assert.ok(result.nsname.length > 0);
    assert.strictEqual(typeof result.hostmaster, 'string');
    assert.ok(result.hostmaster.length > 0);
    assert.strictEqual(typeof result.serial, 'number');
    assert.ok((result.serial > 0) && (result.serial < 4294967295));
    assert.strictEqual(typeof result.refresh, 'number');
    assert.ok((result.refresh > 0) && (result.refresh < 2147483647));
    assert.strictEqual(typeof result.retry, 'number');
    assert.ok((result.retry > 0) && (result.retry < 2147483647));
    assert.strictEqual(typeof result.expire, 'number');
    assert.ok((result.expire > 0) && (result.expire < 2147483647));
    assert.strictEqual(typeof result.minttl, 'number');
    assert.ok((result.minttl >= 0) && (result.minttl < 2147483647));
  }

  validateResult(await dnsPromises.resolveSoa(addresses.SOA_HOST));

  const req = dns.resolveSoa(addresses.SOA_HOST, function(err, result) {
    assert.ifError(err);
    validateResult(result);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveSoa_failure(done) {
  dnsPromises.resolveSoa(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveSoa(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});

TEST(async function test_resolveCaa(done) {
  function validateResult(result) {
    assert.ok(Array.isArray(result),
              `expected array, got ${util.inspect(result)}`);
    assert.strictEqual(result.length, 1);
    assert.strictEqual(typeof result[0].critical, 'number');
    assert.strictEqual(result[0].critical, 0);
    assert.strictEqual(result[0].issue, 'pki.goog');
  }

  validateResult(await dnsPromises.resolveCaa(addresses.CAA_HOST));

  const req = dns.resolveCaa(addresses.CAA_HOST, function(err, records) {
    assert.ifError(err);
    validateResult(records);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveCaa_failure(done) {
  dnsPromises.resolveTxt(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveCaa(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});

TEST(async function test_resolveCname(done) {
  function validateResult(result) {
    assert.ok(result.length > 0);

    for (const item of result) {
      assert.ok(item);
      assert.strictEqual(typeof item, 'string');
    }
  }

  validateResult(await dnsPromises.resolveCname(addresses.CNAME_HOST));

  const req = dns.resolveCname(addresses.CNAME_HOST, function(err, names) {
    assert.ifError(err);
    validateResult(names);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveCname_failure(done) {
  dnsPromises.resolveCname(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveCname(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});


TEST(async function test_resolveTxt(done) {
  function validateResult(result) {
    assert.ok(Array.isArray(result[0]));
    assert.strictEqual(result.length, 1);
    assert(result[0][0].startsWith('v=spf1'));
  }

  validateResult(await dnsPromises.resolveTxt(addresses.TXT_HOST));

  const req = dns.resolveTxt(addresses.TXT_HOST, function(err, records) {
    assert.ifError(err);
    validateResult(records);
    done();
  });

  checkWrap(req);
});

TEST(function test_resolveTxt_failure(done) {
  dnsPromises.resolveTxt(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOTFOUND');
    }));

  const req = dns.resolveTxt(addresses.NOT_FOUND, function(err, result) {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');

    assert.strictEqual(result, undefined);

    done();
  });

  checkWrap(req);
});


TEST(function test_lookup_failure(done) {
  dnsPromises.lookup(addresses.NOT_FOUND, 4)
    .then(common.mustNotCall())
    .catch(common.expectsError({ code: dns.NOTFOUND }));

  const req = dns.lookup(addresses.NOT_FOUND, 4, (err) => {
    assert.ok(err instanceof Error);
    assert.strictEqual(err.code, dns.NOTFOUND);
    assert.strictEqual(err.code, 'ENOTFOUND');
    assert.doesNotMatch(err.message, /ENOENT/);
    assert.ok(err.message.includes(addresses.NOT_FOUND));

    done();
  });

  checkWrap(req);
});


TEST(async function test_lookup_ip_all(done) {
  function validateResult(result) {
    assert.ok(Array.isArray(result));
    assert.ok(result.length > 0);
    assert.strictEqual(result[0].address, '127.0.0.1');
    assert.strictEqual(result[0].family, 4);
  }

  validateResult(await dnsPromises.lookup('127.0.0.1', { all: true }));

  const req = dns.lookup(
    '127.0.0.1',
    { all: true },
    function(err, ips, family) {
      assert.ifError(err);
      assert.strictEqual(family, undefined);
      validateResult(ips);
      done();
    },
  );

  checkWrap(req);
});


TEST(function test_lookup_ip_all_promise(done) {
  const req = util.promisify(dns.lookup)('127.0.0.1', { all: true })
    .then(function(ips) {
      assert.ok(Array.isArray(ips));
      assert.ok(ips.length > 0);
      assert.strictEqual(ips[0].address, '127.0.0.1');
      assert.strictEqual(ips[0].family, 4);

      done();
    });

  checkWrap(req);
});


TEST(function test_lookup_ip_promise(done) {
  util.promisify(dns.lookup)('127.0.0.1')
    .then(function({ address, family }) {
      assert.strictEqual(address, '127.0.0.1');
      assert.strictEqual(family, 4);

      done();
    });
});


TEST(async function test_lookup_null_all(done) {
  assert.deepStrictEqual(await dnsPromises.lookup(null, { all: true }), []);

  const req = dns.lookup(null, { all: true }, (err, ips) => {
    assert.ifError(err);
    assert.ok(Array.isArray(ips));
    assert.strictEqual(ips.length, 0);

    done();
  });

  checkWrap(req);
});


TEST(async function test_lookup_all_mixed(done) {
  function validateResult(result) {
    assert.ok(Array.isArray(result));
    assert.ok(result.length > 0);

    result.forEach(function(ip) {
      if (isIPv4(ip.address))
        assert.strictEqual(ip.family, 4);
      else if (isIPv6(ip.address))
        assert.strictEqual(ip.family, 6);
      else
        assert.fail('unexpected IP address');
    });
  }

  validateResult(await dnsPromises.lookup(addresses.INET_HOST, { all: true }));

  const req = dns.lookup(addresses.INET_HOST, {
    all: true,
  }, function(err, ips) {
    assert.ifError(err);
    validateResult(ips);
    done();
  });

  checkWrap(req);
});


TEST(function test_lookupservice_invalid(done) {
  dnsPromises.lookupService('1.2.3.4', 80)
    .then(common.mustNotCall())
    .catch(common.expectsError({ code: 'ENOTFOUND' }));

  const req = dns.lookupService('1.2.3.4', 80, (err) => {
    assert(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');
    assert.match(err.message, /1\.2\.3\.4/);

    done();
  });

  checkWrap(req);
});


TEST(function test_reverse_failure(done) {
  dnsPromises.reverse('203.0.113.0')
    .then(common.mustNotCall())
    .catch(common.expectsError({
      code: 'ENOTFOUND',
      hostname: '203.0.113.0',
    }));

  // 203.0.113.0/24 are addresses reserved for (RFC) documentation use only
  const req = dns.reverse('203.0.113.0', function(err) {
    assert(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');  // Silly error code...
    assert.strictEqual(err.hostname, '203.0.113.0');
    assert.match(err.message, /203\.0\.113\.0/);

    done();
  });

  checkWrap(req);
});


TEST(function test_lookup_failure(done) {
  dnsPromises.lookup(addresses.NOT_FOUND)
    .then(common.mustNotCall())
    .catch(common.expectsError({
      code: 'ENOTFOUND',
      hostname: addresses.NOT_FOUND,
    }));

  const req = dns.lookup(addresses.NOT_FOUND, (err) => {
    assert(err instanceof Error);
    assert.strictEqual(err.code, 'ENOTFOUND');  // Silly error code...
    assert.strictEqual(err.hostname, addresses.NOT_FOUND);
    assert.ok(err.message.includes(addresses.NOT_FOUND));

    done();
  });

  checkWrap(req);
});


TEST(function test_resolve_failure(done) {
  const req = dns.resolve4(addresses.NOT_FOUND, (err) => {
    assert(err instanceof Error);

    switch (err.code) {
      case 'ENOTFOUND':
      case 'ESERVFAIL':
        break;
      default:
        assert.strictEqual(err.code, 'ENOTFOUND');  // Silly error code...
        break;
    }

    assert.strictEqual(err.hostname, addresses.NOT_FOUND);
    assert.ok(err.message.includes(addresses.NOT_FOUND));

    done();
  });

  checkWrap(req);
});


let getaddrinfoCallbackCalled = false;

console.log(`looking up ${addresses.INET4_HOST}..`);

const cares = internalBinding('cares_wrap');
const req = new cares.GetAddrInfoReqWrap();
cares.getaddrinfo(req, addresses.INET4_HOST, 4,
  /* hints */ 0, /* order */ cares.DNS_ORDER_VERBATIM);

req.oncomplete = function(err, domains) {
  assert.strictEqual(err, 0);
  console.log(`${addresses.INET4_HOST} = ${domains}`);
  assert.ok(Array.isArray(domains));
  assert.ok(domains.length >= 1);
  assert.strictEqual(typeof domains[0], 'string');
  getaddrinfoCallbackCalled = true;
};

process.on('exit', function() {
  console.log(`${completed} tests completed`);
  assert.strictEqual(running, false);
  assert.strictEqual(completed, expected);
  assert.ok(getaddrinfoCallbackCalled);
});

// Should not throw.
dns.lookup(addresses.INET6_HOST, 6, common.mustCall());
dns.lookup(addresses.INET_HOST, {}, common.mustCall());
dns.lookupService('0.0.0.0', '0', common.mustCall());
dns.lookupService('0.0.0.0', 0, common.mustCall());
(async function() {
  await dnsPromises.lookup(addresses.INET6_HOST, 6);
  await dnsPromises.lookup(addresses.INET_HOST, {});
})().then(common.mustCall());
