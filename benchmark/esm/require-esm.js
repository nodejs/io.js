'use strict';

const common = require('../common');
const fs = require('fs');
const tmpdir = require('../../test/common/tmpdir');
const path = require('path');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  type: ['all', 'access', 'load'],
  exports: ['default', 'named'],
  n: [1000],
}, {
  flags: ['--experimental-require-module', '--no-warnings'],
});

function prepare(count, useDefault) {
  tmpdir.refresh();
  const dir = tmpdir.resolve('modules');
  fs.mkdirSync(dir, { recursive: true });
  let mainSource = '';
  let useSource = 'exports.access = function() { return 0';
  for (let i = 0; i < count; ++i) {
    let modSource = `const value${i} = 1;\n`;
    if (useDefault) {
      modSource += `export default { value${i} }\n`;
    } else {
      modSource += `export { value${i} };\n`;
    }
    const filename = `mod${i}.mjs`;
    fs.writeFileSync(
      path.resolve(dir, filename),
      modSource,
      'utf8',
    );
    mainSource += `const mod${i} = require('./modules/${filename}');\n`;
    if (useDefault) {
      useSource += ` + mod${i}.default.value${i}`;
    } else {
      useSource += ` + mod${i}.value${i}`;
    }
  }
  useSource += '; };\n';
  const script = tmpdir.resolve('main.js');
  fs.writeFileSync(script, mainSource + useSource, 'utf8');
  return script;
}

function main({ n, exports, type }) {
  const script = prepare(n, exports === 'default');
  switch (type) {
    case 'all': {
      bench.start();
      const result = require(script).access();
      bench.end(n);
      assert.strictEqual(result, n);
      break;
    }
    case 'access': {
      const { access } = require(script);
      bench.start();
      let result = access();
      for (let i = 0; i < 99; ++i) {
        result = access();
      }
      bench.end(n * 100);
      assert.strictEqual(result, n);
      break;
    }
    case 'load': {
      bench.start();
      const { access } = require(script);
      bench.end(n);
      const result = access();
      assert.strictEqual(result, n);
      break;
    }
  }
}
