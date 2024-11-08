'use strict';

// This benchmarks the cost of running `containsModuleSyntax` on a CommonJS module being imported.
// We use the TypeScript fixture because it's a very large CommonJS file with no ESM syntax: the worst case.
const common = require('../common.js');
const tmpdir = require('../../test/common/tmpdir.js');
const fs = require('node:fs');

const bench = common.createBenchmark(main, {
  type: ['with-package-json', 'without-package-json'],
  n: [1e4],
});

async function main({ n, type }) {
  tmpdir.refresh();
  fs.mkdirSync(tmpdir.resolve('bench'));

  let loader = '';
  const modules = [];
  for (let i = 0; i < n; i++) {
    const url = tmpdir.fileURL('bench', `mod${i}.js`);
    fs.writeFileSync(url, `const foo${i} = ${i};\nexport { foo${i} };\n`);
    loader += `import { foo${i} } from './mod${i}.js';\n`;
    modules.push(url);
  }
  const loaderURL = tmpdir.fileURL('bench', 'load.js');
  fs.writeFileSync(loaderURL, loader);

  if (type === 'with-package-json') {
    fs.writeFileSync(tmpdir.resolve('bench', 'package.json'), '{ "type": "module" }');
  }

  bench.start();
  await import(loaderURL);
  bench.end(n);
}
