'use strict';
const common = require('../common.js');
const querystring = require('querystring');

const bench = common.createBenchmark(main, {
  input: [
    'there is nothing to unescape here',
    'there+are+spaces+to+unescape+here',
    'there%20are%20several%20spaces%20that%20need%20to%20be%20unescaped',
    '%20%21%22%23%24%25%26%27%28%29%2A%2B%2C%2D%2E%2F%30%31%32%33%34%35%36%37',
    'there%2Qare%0-fake%escaped values in%%%%this%9Hstring',
    'there%2Qare%0-fake%escaped%20values in%%%%this%9Hstring',
    '%%2a',
    '%2sf%2a',
    '%2%2af%2a',
  ],
  n: [10e6],
  decodeSpaces: [1, 0],
});

function main({ input, n, decodeSpaces }) {
  decodeSpaces = !!decodeSpaces;
  bench.start();
  for (let i = 0; i < n; i += 1)
    querystring.unescape(input, decodeSpaces);
  bench.end(n);
}
