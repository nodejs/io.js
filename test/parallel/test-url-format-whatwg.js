'use strict';

const { hasIntl } = require('../common');

const assert = require('node:assert');
const url = require('node:url');
const { test } = require('node:test');

const myURL = new URL('http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c');

test('should format', { skip: !hasIntl }, () => {
  assert.strictEqual(
    url.format(myURL),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, {}),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );
});

test('handle invalid arguments', { skip: !hasIntl }, () => {
  for (const value of [true, 1, 'test', Infinity]) {
    assert.throws(
      () => url.format(myURL, value),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
      }
    );
  }
});

test('any falsy value other than undefined will be treated as false', { skip: !hasIntl }, () => {
  assert.strictEqual(
    url.format(myURL, { auth: false }),
    'http://xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { auth: '' }),
    'http://xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { auth: 0 }),
    'http://xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { auth: 1 }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { auth: {} }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { fragment: false }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b'
  );

  assert.strictEqual(
    url.format(myURL, { fragment: '' }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b'
  );

  assert.strictEqual(
    url.format(myURL, { fragment: 0 }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b'
  );

  assert.strictEqual(
    url.format(myURL, { fragment: 1 }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { fragment: {} }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { search: false }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a#c'
  );

  assert.strictEqual(
    url.format(myURL, { search: '' }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a#c'
  );

  assert.strictEqual(
    url.format(myURL, { search: 0 }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a#c'
  );

  assert.strictEqual(
    url.format(myURL, { search: 1 }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { search: {} }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { unicode: true }),
    'http://user:pass@理容ナカムラ.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { unicode: 1 }),
    'http://user:pass@理容ナカムラ.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { unicode: {} }),
    'http://user:pass@理容ナカムラ.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { unicode: false }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );

  assert.strictEqual(
    url.format(myURL, { unicode: 0 }),
    'http://user:pass@xn--lck1c3crb1723bpq4a.com/a?a=b#c'
  );
});

test('should format with unicode: true', { skip: !hasIntl }, () => {
  assert.strictEqual(
    url.format(new URL('http://user:pass@xn--0zwm56d.com:8080/path'), { unicode: true }),
    'http://user:pass@测试.com:8080/path'
  );
});

test('should format tel: prefix', { skip: !hasIntl }, () => {
  assert.strictEqual(
    url.format(new URL('tel:123')),
    url.format(new URL('tel:123'), { unicode: true })
  );
});
