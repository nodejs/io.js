'use strict';
const common = require('../common.js');
const { fileURLToPath, pathToFileURL } = require('node:url');
const isWindows = process.platform === 'win32';

const inputs = isWindows ? [
  'C:\\foo',
  'C:\\Program Files\\Music\\Web Sys\\main.html?REQUEST=RADIO',
  '\\\\nas\\My Docs\\File.doc',
  '\\\\?\\UNC\\server\\share\\folder\\file.txt',
  'file:///C:/foo',
  'file:///C:/dir/foo?query=1',
  'file:///C:/dir/foo#fragment',
] : [
  '/dev/null',
  '/dev/null?key=param&bool',
  '/dev/null?key=param&bool#hash',
  'file:///dev/null',
  'file:///dev/null?key=param&bool',
  'file:///dev/null?key=param&bool#hash',
];

const bench = common.createBenchmark(
  main,
  {
    method: ['pathToFileURL', 'fileURLToPath'],
    input: Object.values(inputs),
    n: [5e6],
  },
  {
    combinationFilter: (p) => (
      (isWindows ?
        (!p.input.startsWith('file://') && p.method === 'pathToFileURL') :
        p.method === 'pathToFileURL'
      ) ||
      (p.input.startsWith('file://') && p.method === 'fileURLToPath')
    ),
  },
);

function main({ method, input, n }) {
  const methodFunc = method === 'fileURLToPath' ? fileURLToPath : pathToFileURL;
  bench.start();
  for (let i = 0; i < n; i++) {
    methodFunc(input);
  }
  bench.end(n);
}
