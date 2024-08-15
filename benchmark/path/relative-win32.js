'use strict';
const common = require('../common.js');
const { win32 } = require('path');

const bench = common.createBenchmark(main, {
  paths: [
    ['C:\\orandea\\test\\aaa', 'C:\\orandea\\impl\\bbb'].join('|'),
    ['C:\\', 'D:\\'].join('|'),
    ['C:\\foo\\bar\\baz', 'C:\\foo\\bar\\baz'].join('|'),
    ['C:\\foo\\BAR\\BAZ', 'C:\\foo\\bar\\baz'].join('|'),
    ['C:\\foo\\bar\\baz\\quux', 'C:\\'].join('|'),
    ['c:\\İ\\a\\İ', 'c:\\İ\\b\\İ\\test.txt', '..\\..\\b\\İ\\test.txt'].join('|'),
  ],
  n: [1e5],
});

function main({ n, paths }) {
  let to = '';
  const delimIdx = paths.indexOf('|');
  if (delimIdx > -1) {
    to = paths.slice(delimIdx + 1);
    paths = paths.slice(0, delimIdx);
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    if (i % 3 === 0)
      win32.relative(`${paths}${i}`, `${to}${i}`);
    else
      win32.relative(paths, to);
  }
  bench.end(n);
}
