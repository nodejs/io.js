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
const dnstools = require('../common/dns');
const assert = require('assert');

const dns = require('dns');
const dnsPromises = dns.promises;
const dgram = require('dgram');

const existing = dns.getServers();
assert(existing.length > 0);

// Verify that setServers() handles arrays with holes and other oddities
{
  const servers = [];

  servers[0] = '127.0.0.1';
  servers[2] = '0.0.0.0';
  dns.setServers(servers);

  assert.deepStrictEqual(dns.getServers(), ['127.0.0.1', '0.0.0.0']);
}

{
  const servers = ['127.0.0.1', '192.168.1.1'];

  servers[3] = '127.1.0.1';
  servers[4] = '127.1.0.1';
  servers[5] = '127.1.1.1';

  Object.defineProperty(servers, 2, {
    enumerable: true,
    get: () => {
      servers.length = 3;
      return '0.0.0.0';
    }
  });

  dns.setServers(servers);
  assert.deepStrictEqual(dns.getServers(), [
    '127.0.0.1',
    '192.168.1.1',
    '0.0.0.0',
  ]);
}

{
  // Various invalidities, all of which should throw a clean error.
  const invalidServers = [
    ' ',
    '\n',
    '\0',
    '1'.repeat(3 * 4),
    // Check for REDOS issues.
    ':'.repeat(100000),
    '['.repeat(100000),
    '['.repeat(100000) + ']'.repeat(100000) + 'a',
  ];
  invalidServers.forEach((serv) => {
    assert.throws(
      () => {
        dns.setServers([serv]);
      },
      {
        name: 'TypeError',
        code: 'ERR_INVALID_IP_ADDRESS'
      }
    );
  });
}

const goog = [
  '8.8.8.8',
  '8.8.4.4',
];
dns.setServers(goog);
assert.deepStrictEqual(dns.getServers(), goog);
assert.throws(() => dns.setServers(['foobar']), {
  code: 'ERR_INVALID_IP_ADDRESS',
  name: 'TypeError',
  message: 'Invalid IP address: foobar'
});
assert.throws(() => dns.setServers(['127.0.0.1:va']), {
  code: 'ERR_INVALID_IP_ADDRESS',
  name: 'TypeError',
  message: 'Invalid IP address: 127.0.0.1:va'
});
assert.deepStrictEqual(dns.getServers(), goog);

const goog6 = [
  '2001:4860:4860::8888',
  '2001:4860:4860::8844',
];
dns.setServers(goog6);
assert.deepStrictEqual(dns.getServers(), goog6);

goog6.push('4.4.4.4');
dns.setServers(goog6);
assert.deepStrictEqual(dns.getServers(), goog6);

const ports = [
  '4.4.4.4:53',
  '[2001:4860:4860::8888]:53',
  '103.238.225.181:666',
  '[fe80::483a:5aff:fee6:1f04]:666',
  '[fe80::483a:5aff:fee6:1f04]',
];
const portsExpected = [
  '4.4.4.4',
  '2001:4860:4860::8888',
  '103.238.225.181:666',
  '[fe80::483a:5aff:fee6:1f04]:666',
  'fe80::483a:5aff:fee6:1f04',
];
dns.setServers(ports);
assert.deepStrictEqual(dns.getServers(), portsExpected);

dns.setServers([]);
assert.deepStrictEqual(dns.getServers(), []);

{
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "rrtype" argument must be of type string. ' +
             'Received an instance of Array'
  };
  assert.throws(() => {
    dns.resolve('example.com', [], common.mustNotCall());
  }, errObj);
  assert.throws(() => {
    dnsPromises.resolve('example.com', []);
  }, errObj);
}
{
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "name" argument must be of type string. ' +
             'Received undefined'
  };
  assert.throws(() => {
    dnsPromises.resolve();
  }, errObj);
}

// dns.lookup should accept only falsey and string values
{
  const errorReg = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: /^The "hostname" argument must be of type string\. Received .*/
  };

  assert.throws(() => dns.lookup({}, common.mustNotCall()), errorReg);

  assert.throws(() => dns.lookup([], common.mustNotCall()), errorReg);

  assert.throws(() => dns.lookup(true, common.mustNotCall()), errorReg);

  assert.throws(() => dns.lookup(1, common.mustNotCall()), errorReg);

  assert.throws(() => dns.lookup(common.mustNotCall(), common.mustNotCall()),
                errorReg);

  assert.throws(() => dnsPromises.lookup({}), errorReg);
  assert.throws(() => dnsPromises.lookup([]), errorReg);
  assert.throws(() => dnsPromises.lookup(true), errorReg);
  assert.throws(() => dnsPromises.lookup(1), errorReg);
  assert.throws(() => dnsPromises.lookup(common.mustNotCall()), errorReg);
}

// dns.lookup should accept falsey values
{
  const checkCallback = (err, address, family) => {
    assert.ifError(err);
    assert.strictEqual(address, null);
    assert.strictEqual(family, 4);
  };

  ['', null, undefined, 0, NaN].forEach(async (value) => {
    const res = await dnsPromises.lookup(value);
    assert.deepStrictEqual(res, { address: null, family: 4 });
    dns.lookup(value, common.mustCall(checkCallback));
  });
}

{
  // Make sure that dns.lookup throws if hints does not represent a valid flag.
  // (dns.V4MAPPED | dns.ADDRCONFIG | dns.ALL) + 1 is invalid because:
  // - it's different from dns.V4MAPPED and dns.ADDRCONFIG and dns.ALL.
  // - it's different from any subset of them bitwise ored.
  // - it's different from 0.
  // - it's an odd number different than 1, and thus is invalid, because
  // flags are either === 1 or even.
  const hints = (dns.V4MAPPED | dns.ADDRCONFIG | dns.ALL) + 1;
  const err = {
    code: 'ERR_INVALID_ARG_VALUE',
    name: 'TypeError',
    message: /The argument 'hints' is invalid\. Received \d+/
  };

  assert.throws(() => {
    dnsPromises.lookup('nodejs.org', { hints });
  }, err);
  assert.throws(() => {
    dns.lookup('nodejs.org', { hints }, common.mustNotCall());
  }, err);
}

assert.throws(() => dns.lookup('nodejs.org'), {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});

assert.throws(() => dns.lookup('nodejs.org', 4), {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});

assert.throws(() => dns.lookup('', {
  family: 'nodejs.org',
  hints: dns.ADDRCONFIG | dns.V4MAPPED | dns.ALL,
}), {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});

dns.lookup('', { family: 4, hints: 0 }, common.mustCall());

dns.lookup('', {
  family: 6,
  hints: dns.ADDRCONFIG
}, common.mustCall());

dns.lookup('', { hints: dns.V4MAPPED }, common.mustCall());

dns.lookup('', {
  hints: dns.ADDRCONFIG | dns.V4MAPPED
}, common.mustCall());

dns.lookup('', {
  hints: dns.ALL
}, common.mustCall());

dns.lookup('', {
  hints: dns.V4MAPPED | dns.ALL
}, common.mustCall());

dns.lookup('', {
  hints: dns.ADDRCONFIG | dns.V4MAPPED | dns.ALL
}, common.mustCall());

dns.lookup('', {
  hints: dns.ADDRCONFIG | dns.V4MAPPED | dns.ALL,
  family: 'IPv4'
}, common.mustCall());

dns.lookup('', {
  hints: dns.ADDRCONFIG | dns.V4MAPPED | dns.ALL,
  family: 'IPv6'
}, common.mustCall());

(async function() {
  await dnsPromises.lookup('', { family: 4, hints: 0 });
  await dnsPromises.lookup('', { family: 6, hints: dns.ADDRCONFIG });
  await dnsPromises.lookup('', { hints: dns.V4MAPPED });
  await dnsPromises.lookup('', { hints: dns.ADDRCONFIG | dns.V4MAPPED });
  await dnsPromises.lookup('', { hints: dns.ALL });
  await dnsPromises.lookup('', { hints: dns.V4MAPPED | dns.ALL });
  await dnsPromises.lookup('', {
    hints: dns.ADDRCONFIG | dns.V4MAPPED | dns.ALL
  });
  await dnsPromises.lookup('', { order: 'verbatim' });
})().then(common.mustCall());

{
  const err = {
    code: 'ERR_MISSING_ARGS',
    name: 'TypeError',
    message: 'The "address", "port", and "callback" arguments must be ' +
    'specified'
  };

  assert.throws(() => dns.lookupService('0.0.0.0'), err);
  err.message = 'The "address" and "port" arguments must be specified';
  assert.throws(() => dnsPromises.lookupService('0.0.0.0'), err);
}

{
  const invalidAddress = 'fasdfdsaf';
  const err = {
    code: 'ERR_INVALID_ARG_VALUE',
    name: 'TypeError',
    message: `The argument 'address' is invalid. Received '${invalidAddress}'`
  };

  assert.throws(() => {
    dnsPromises.lookupService(invalidAddress, 0);
  }, err);

  assert.throws(() => {
    dns.lookupService(invalidAddress, 0, common.mustNotCall());
  }, err);
}

const portErr = (port) => {
  const err = {
    code: 'ERR_SOCKET_BAD_PORT',
    name: 'RangeError'
  };

  assert.throws(() => {
    dnsPromises.lookupService('0.0.0.0', port);
  }, err);

  assert.throws(() => {
    dns.lookupService('0.0.0.0', port, common.mustNotCall());
  }, err);
};
[null, undefined, 65538, 'test', NaN, Infinity, Symbol(), 0n, true, false, '', () => {}, {}].forEach(portErr);

assert.throws(() => {
  dns.lookupService('0.0.0.0', 80, null);
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});

{
  dns.resolveMx('foo.onion', function(err) {
    assert.strictEqual(err.code, 'ENOTFOUND');
    assert.strictEqual(err.syscall, 'queryMx');
    assert.strictEqual(err.hostname, 'foo.onion');
    assert.strictEqual(err.message, 'queryMx ENOTFOUND foo.onion');
  });
}

{
  const cases = [
    { method: 'resolveAny',
      answers: [
        { type: 'A', address: '1.2.3.4', ttl: 0 },
        { type: 'AAAA', address: '::42', ttl: 0 },
        { type: 'MX', priority: 42, exchange: 'foobar.com', ttl: 0 },
        { type: 'NS', value: 'foobar.org', ttl: 0 },
        { type: 'PTR', value: 'baz.org', ttl: 0 },
        {
          type: 'SOA',
          nsname: 'ns1.example.com',
          hostmaster: 'admin.example.com',
          serial: 3210987654,
          refresh: 900,
          retry: 900,
          expire: 1800,
          minttl: 3333333333
        },
      ] },

    { method: 'resolve4',
      options: { ttl: true },
      answers: [ { type: 'A', address: '1.2.3.4', ttl: 0 } ] },

    { method: 'resolve6',
      options: { ttl: true },
      answers: [ { type: 'AAAA', address: '::42', ttl: 0 } ] },

    { method: 'resolveSoa',
      answers: [
        {
          type: 'SOA',
          nsname: 'ns1.example.com',
          hostmaster: 'admin.example.com',
          serial: 3210987654,
          refresh: 900,
          retry: 900,
          expire: 1800,
          minttl: 3333333333
        },
      ] },
  ];

  const server = dgram.createSocket('udp4');

  server.on('message', common.mustCall((msg, { address, port }) => {
    const parsed = dnstools.parseDNSPacket(msg);
    const domain = parsed.questions[0].domain;
    assert.strictEqual(domain, 'example.org');

    server.send(dnstools.writeDNSPacket({
      id: parsed.id,
      questions: parsed.questions,
      answers: cases[0].answers.map(
        (answer) => Object.assign({ domain }, answer)
      ),
    }), port, address);
  }, cases.length * 2 - 1));

  server.bind(0, common.mustCall(() => {
    const address = server.address();
    dns.setServers([`127.0.0.1:${address.port}`]);

    function validateResults(res) {
      if (!Array.isArray(res))
        res = [res];

      assert.deepStrictEqual(res.map(tweakEntry),
                             cases[0].answers.map(tweakEntry));
    }

    function tweakEntry(r) {
      const ret = { ...r };

      const { method } = cases[0];

      // TTL values are only provided for A and AAAA entries.
      if (!['A', 'AAAA'].includes(ret.type) && !/^resolve(4|6)?$/.test(method))
        delete ret.ttl;

      if (method !== 'resolveAny')
        delete ret.type;

      return ret;
    }

    (async function nextCase() {
      if (cases.length === 0)
        return server.close();

      const { method, options } = cases[0];

      validateResults(await dnsPromises[method]('example.org', options));

      dns[method]('example.org', options, common.mustSucceed((res) => {
        validateResults(res);
        cases.shift();
        nextCase();
      }));
    })().then(common.mustCall());

  }));
}
