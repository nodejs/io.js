'use strict';

require('../common');

const { URLPattern } = require('url');
const { throws } = require('assert');

const pattern = new URLPattern();
const proto = Object.getPrototypeOf(pattern);

// Verifies that attempts to call the property getters on a URLPattern
// with the incorrect `this` will not crash the process.
[
  'protocol',
  'username',
  'password',
  'hostname',
  'port',
  'pathname',
  'search',
  'hash',
  'hasRegExpGroups',
].forEach((i) => {
  const prop = Object.getOwnPropertyDescriptor(proto, i).get;
  throws(() => prop({}), {
    message: 'Illegal invocation',
  }, i);
});

// Verifies that attempts to call the exec and test functions
// with the wrong this also throw

const { test, exec } = pattern;

throws(() => test({}), {
  message: 'Illegal invocation',
});
throws(() => exec({}), {
  message: 'Illegal invocation',
});
