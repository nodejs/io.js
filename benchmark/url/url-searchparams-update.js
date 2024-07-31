'use strict';
const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  searchParams: ['true', 'false'],
  property: ['pathname', 'search', 'hash'],
  n: [1e6],
});

function getMethod(url, property) {
  if (property === 'pathname') return (x) => url.pathname = `/${x}`;
  if (property === 'search') return (x) => url.search = `?${x}`;
  if (property === 'hash') return (x) => url.hash = `#${x}`;
  throw new Error(`Unsupported property "${property}"`);
}

function main({ searchParams, property, n }) {
  const url = new URL('https://nodejs.org');
  if (searchParams === 'true') assert(url.searchParams);

  const method = getMethod(url, property);

  bench.start();
  for (let i = 0; i < n; i++) {
    method(i);
  }
  bench.end(n);
}
