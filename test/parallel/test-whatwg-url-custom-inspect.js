'use strict';

// Tests below are not from WPT.

const common = require('../common');
if (!common.hasIntl) {
  // A handful of the tests fail when ICU is not included.
  common.skip('missing Intl');
}

const util = require('util');
const assert = require('assert');

const url = new URL('https://username:password@host.name:8080/path/name/?que=ry#hash');

assert.strictEqual(
  util.inspect(url),
  `URL {
  href: 'https://username:password@host.name:8080/path/name/?que=ry#hash',
  origin: 'https://host.name:8080',
  protocol: 'https:',
  username: 'username',
  password: 'password',
  host: 'host.name:8080',
  hostname: 'host.name',
  port: '8080',
  pathname: '/path/name/',
  search: '?que=ry',
  searchParams: URLSearchParams { 'que' => 'ry' },
  hash: '#hash'
}`);

assert.strictEqual(
  util.inspect(url, { showHidden: true }),
  `URL {
  href: 'https://username:password@host.name:8080/path/name/?que=ry#hash',
  origin: 'https://host.name:8080',
  protocol: 'https:',
  username: 'username',
  password: 'password',
  host: 'host.name:8080',
  hostname: 'host.name',
  port: '8080',
  pathname: '/path/name/',
  search: '?que=ry',
  searchParams: URLSearchParams { 'que' => 'ry' },
  hash: '#hash',
  Symbol(context): URLContext {
    href: 'https://username:password@host.name:8080/path/name/?que=ry#hash',
    protocol_end: 6,
    username_end: 16,
    host_start: 25,
    host_end: 35,
    pathname_start: 40,
    search_start: 51,
    hash_start: 58,
    port: 8080,
    scheme_type: 2,
    [hasPort]: [Getter],
    [hasSearch]: [Getter],
    [hasHash]: [Getter]
  }
}`);

assert.strictEqual(
  util.inspect({ a: url }, { depth: 0 }),
  '{ a: URL {} }');

class MyURL extends URL {}
assert(util.inspect(new MyURL(url.href)).startsWith('MyURL {'));
