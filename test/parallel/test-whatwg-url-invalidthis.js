'use strict';

require('../common');

const { URL } = require('url');
const assert = require('assert');

[
  'toString',
  'toJSON',
].forEach((i) => {
  assert.throws(() => Reflect.apply(URL.prototype[i], [], {}), {
    name: 'TypeError',
    message: /Receiver must be an instance of class/,
  });
});

[
  'href',
  'search',
].forEach((i) => {
  assert.throws(() => Reflect.get(URL.prototype, i, {}), {
    name: 'TypeError',
    message: /Receiver must be an instance of class/,
  });

  assert.throws(() => Reflect.set(URL.prototype, i, null, {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });
});

[
  'protocol',
  'username',
  'password',
  'host',
  'hostname',
  'port',
  'pathname',
  'hash',
].forEach((i) => {
  assert.throws(() => Reflect.get(URL.prototype, i, {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });

  assert.throws(() => Reflect.set(URL.prototype, i, null, {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });
});

[
  'origin',
  'searchParams',
].forEach((i) => {
  assert.throws(() => Reflect.get(URL.prototype, i, {}), {
    name: 'TypeError',
    message: /Cannot read private member/,
  });
});
