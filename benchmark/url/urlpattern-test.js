'use strict';
const common = require('../common.js');
const { URLPattern } = require('url');
const { notStrictEqual } = require('assert');

const tests = [
  'https://(sub.)?example(.com/)foo',
  { 'hostname': 'xn--caf-dma.com' },
  { 'pathname': '/foo', 'search': 'bar', 'hash': 'baz',
    'baseURL': 'https://example.com:8080' },
  { 'pathname': '/([[a-z]--a])' },
];

const bench = common.createBenchmark(main, {
  pattern: tests.map(JSON.stringify),
  n: [1e5],
});

function main({ pattern, n }) {
  const inputPattern = JSON.parse(pattern);
  const urlpattern = new URLPattern(inputPattern);

  let deadcode;
  bench.start();
  for (let i = 0; i < n; i += 1) {
    deadcode = urlpattern.test('https://sub.example.com/foo');
  }
  bench.end(n);
  notStrictEqual(deadcode, undefined);  // We don't care if it is true or false.
}
