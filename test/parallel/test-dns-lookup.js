// Flags: --expose-internals
'use strict';
const common = require('../common');
const assert = require('assert');
const { internalBinding } = require('internal/test/binding');
const cares = internalBinding('cares_wrap');

// Stub `getaddrinfo` to *always* error. This has to be done before we load the
// `dns` module to guarantee that the `dns` module uses the stub.
cares.getaddrinfo = () => internalBinding('uv').UV_ENOMEM;

const dns = require('dns');
const dnsPromises = dns.promises;

{
  const err = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: /^The "hostname" argument must be of type string\. Received type number/
  };

  assert.throws(() => dns.lookup(1, {}), err);
  assert.throws(() => dnsPromises.lookup(1, {}), err);
}

// This also verifies different expectWarning notations.
common.expectWarning({
  // For 'internal/test/binding' module.
  'internal/test/binding': [
    'These APIs are for internal testing only. Do not use them.',
  ],
  // For calling `dns.lookup` with falsy `hostname`.
  'DeprecationWarning': {
    DEP0118: 'The provided hostname "false" is not a valid ' +
      'hostname, and is supported in the dns module solely for compatibility.'
  }
});

assert.throws(() => {
  dns.lookup(false, 'cb');
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});

assert.throws(() => {
  dns.lookup(false, 'options', 'cb');
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});

{
  const err = {
    code: 'ERR_INVALID_ARG_VALUE',
    name: 'TypeError',
    message: "The argument 'hints' is invalid. Received 100"
  };
  const options = {
    hints: 100,
    family: 0,
    all: false
  };

  assert.throws(() => { dnsPromises.lookup(false, options); }, err);
  assert.throws(() => {
    dns.lookup(false, options, common.mustNotCall());
  }, err);
}

{
  const family = 20;
  const err = {
    code: 'ERR_INVALID_ARG_VALUE',
    name: 'TypeError',
    message: `The property 'options.family' must be one of: 0, 4, 6. Received ${family}`
  };
  const options = {
    hints: 0,
    family,
    all: false
  };

  assert.throws(() => { dnsPromises.lookup(false, options); }, err);
  assert.throws(() => {
    dns.lookup(false, options, common.mustNotCall());
  }, err);
}

[1, 0n, 1n, '', '0', Symbol(), true, false, {}, [], () => {}]
  .forEach((family) => {
    const err = { code: 'ERR_INVALID_ARG_VALUE' };
    const options = { family };
    assert.throws(() => { dnsPromises.lookup(false, options); }, err);
    assert.throws(() => {
      dns.lookup(false, options, common.mustNotCall());
    }, err);
  });
[0n, 1n, '', '0', Symbol(), true, false].forEach((family) => {
  const err = { code: 'ERR_INVALID_ARG_TYPE' };
  assert.throws(() => { dnsPromises.lookup(false, family); }, err);
  assert.throws(() => {
    dns.lookup(false, family, common.mustNotCall());
  }, err);
});
assert.throws(() => dnsPromises.lookup(false, () => {}),
              { code: 'ERR_INVALID_ARG_TYPE' });

[0n, 1n, '', '0', Symbol(), true, false, {}, [], () => {}].forEach((hints) => {
  const err = { code: 'ERR_INVALID_ARG_TYPE' };
  const options = { hints };
  assert.throws(() => { dnsPromises.lookup(false, options); }, err);
  assert.throws(() => {
    dns.lookup(false, options, common.mustNotCall());
  }, err);
});

[0, 1, 0n, 1n, '', '0', Symbol(), {}, [], () => {}].forEach((all) => {
  const err = { code: 'ERR_INVALID_ARG_TYPE' };
  const options = { all };
  assert.throws(() => { dnsPromises.lookup(false, options); }, err);
  assert.throws(() => {
    dns.lookup(false, options, common.mustNotCall());
  }, err);
});

[0, 1, 0n, 1n, '', '0', Symbol(), {}, [], () => {}].forEach((verbatim) => {
  const err = { code: 'ERR_INVALID_ARG_TYPE' };
  const options = { verbatim };
  assert.throws(() => { dnsPromises.lookup(false, options); }, err);
  assert.throws(() => {
    dns.lookup(false, options, common.mustNotCall());
  }, err);
});

[0, 1, 0n, 1n, '', '0', Symbol(), {}, [], () => {}].forEach((order) => {
  const err = { code: 'ERR_INVALID_ARG_VALUE' };
  const options = { order };
  assert.throws(() => { dnsPromises.lookup(false, options); }, err);
  assert.throws(() => {
    dns.lookup(false, options, common.mustNotCall());
  }, err);
});

(async function() {
  let res;

  res = await dnsPromises.lookup(false, {
    hints: 0,
    family: 0,
    all: true
  });
  assert.deepStrictEqual(res, []);

  res = await dnsPromises.lookup('127.0.0.1', {
    hints: 0,
    family: 4,
    all: true
  });
  assert.deepStrictEqual(res, [{ address: '127.0.0.1', family: 4 }]);

  res = await dnsPromises.lookup('127.0.0.1', {
    hints: 0,
    family: 4,
    all: false
  });
  assert.deepStrictEqual(res, { address: '127.0.0.1', family: 4 });
})().then(common.mustCall());

dns.lookup(false, {
  hints: 0,
  family: 0,
  all: true
}, common.mustSucceed((result, addressType) => {
  assert.deepStrictEqual(result, []);
  assert.strictEqual(addressType, undefined);
}));

dns.lookup('127.0.0.1', {
  hints: 0,
  family: 4,
  all: true
}, common.mustSucceed((result, addressType) => {
  assert.deepStrictEqual(result, [{
    address: '127.0.0.1',
    family: 4
  }]);
  assert.strictEqual(addressType, undefined);
}));

dns.lookup('127.0.0.1', {
  hints: 0,
  family: 4,
  all: false
}, common.mustSucceed((result, addressType) => {
  assert.strictEqual(result, '127.0.0.1');
  assert.strictEqual(addressType, 4);
}));

let tickValue = 0;

// Should fail due to stub.
dns.lookup('example.com', common.mustCall((error, result, addressType) => {
  assert(error);
  assert.strictEqual(tickValue, 1);
  assert.strictEqual(error.code, 'ENOMEM');
  const descriptor = Object.getOwnPropertyDescriptor(error, 'message');
  // The error message should be non-enumerable.
  assert.strictEqual(descriptor.enumerable, false);
}));

// Make sure that the error callback is called on next tick.
tickValue = 1;

// Should fail due to stub.
assert.rejects(dnsPromises.lookup('example.com'),
               { code: 'ENOMEM', hostname: 'example.com' }).then(common.mustCall());
