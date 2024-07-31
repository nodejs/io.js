'use strict';

const common = require('../common.js');
const fs = require('fs');
const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();

// Some variants are commented out as they do not show a change and just slow
const bench = common.createBenchmark(main, {
  encoding: ['utf8'],
  useFd: ['true', 'false'],
  length: [1024, 102400, 1024 * 1024],

  // useBuffer: ['true', 'false'],
  useBuffer: ['false'],

  // func: ['appendFile', 'writeFile'],
  func: ['writeFile'],

  n: [1e3],
});

function main({ n, func, encoding, length, useFd, useBuffer }) {
  tmpdir.refresh();
  const enc = encoding === 'undefined' ? undefined : encoding;
  const path = tmpdir.resolve(`.writefilesync-file-${Date.now()}`);

  useFd = useFd === 'true';
  const file = useFd ? fs.openSync(path, 'w') : path;

  let data = 'a'.repeat(length);
  if (useBuffer === 'true') data = Buffer.from(data, encoding);

  const fn = fs[func + 'Sync'];

  bench.start();
  for (let i = 0; i < n; ++i) {
    fn(file, data, enc);
  }
  bench.end(n);

  if (useFd) fs.closeSync(file);
}
