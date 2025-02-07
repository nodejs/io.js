'use strict';
const common = require('../common');
const { pathToFileURL } = require('url');
const fixtures = require('../common/fixtures');
common.skipIfInspectorDisabled();

const { describe, it } = require('node:test');

function generateReport(report) {
  report = ([
    '# start of coverage report',
    ...report,
    '# end of coverage report',
  ]).join('\n');
  if (common.isWindows) {
    report = report.replaceAll('/', '\\');
  }
  return report;
}

const flags = [
  '--enable-source-maps',
  '--test',
  '--experimental-test-coverage',
  '--test-coverage-exclude=!test/**',
  '--test-reporter',
  'tap',
  '--no-experimental-strip-types',
];

describe('Coverage with source maps', async () => {
  await it('should work with source maps', async (t) => {
    const report = generateReport([
      '# --------------------------------------------------------------',
      '# file          | line % | branch % | funcs % | uncovered lines',
      '# --------------------------------------------------------------',
      '# a.test.ts     |  53.85 |   100.00 |  100.00 | 8-13',  // part of a bundle
      '# b.test.ts     |  55.56 |   100.00 |  100.00 | 1 7-9', // part of a bundle
      '# index.test.js |  71.43 |    66.67 |  100.00 | 6-7',  // no source map
      '# stdin.test.ts |  57.14 |   100.00 |  100.00 | 4-6',  // Source map without original file
      '# --------------------------------------------------------------',
      '# all files     |  58.33 |    87.50 |  100.00 | ',
      '# --------------------------------------------------------------',
    ]);

    const spawned = await common.spawnPromisified(process.execPath, flags, {
      cwd: fixtures.path('test-runner', 'coverage')
    });

    t.assert.strictEqual(spawned.stderr, '');
    t.assert.ok(spawned.stdout.includes(report));
    t.assert.strictEqual(spawned.code, 1);
  });

  await it('should only work with --enable-source-maps', async (t) => {
    const report = generateReport([
      '# --------------------------------------------------------------',
      '# file          | line % | branch % | funcs % | uncovered lines',
      '# --------------------------------------------------------------',
      '# a.test.mjs    | 100.00 |   100.00 |  100.00 | ',
      '# index.test.js |  71.43 |    66.67 |  100.00 | 6-7',
      '# stdin.test.js | 100.00 |   100.00 |  100.00 | ',
      '# --------------------------------------------------------------',
      '# all files     |  85.71 |    87.50 |  100.00 | ',
      '# --------------------------------------------------------------',
    ]);

    const spawned = await common.spawnPromisified(process.execPath, flags.slice(1), {
      cwd: fixtures.path('test-runner', 'coverage')
    });
    t.assert.strictEqual(spawned.stderr, '');
    t.assert.ok(spawned.stdout.includes(report));
    t.assert.strictEqual(spawned.code, 1);
  });

  await it('properly accounts for line endings in source maps', async (t) => {
    const report = generateReport([
      '# ------------------------------------------------------------------',
      '# file              | line % | branch % | funcs % | uncovered lines',
      '# ------------------------------------------------------------------',
      '# test              |        |          |         | ',
      '#  fixtures         |        |          |         | ',
      '#   test-runner     |        |          |         | ',
      '#    source-maps    |        |          |         | ',
      '#     line-lengths  |        |          |         | ',
      '#      index.ts     | 100.00 |   100.00 |  100.00 | ',
      '# ------------------------------------------------------------------',
      '# all files         | 100.00 |   100.00 |  100.00 | ',
      '# ------------------------------------------------------------------',
    ]);

    const spawned = await common.spawnPromisified(process.execPath, [
      ...flags,
      fixtures.path('test-runner', 'source-maps', 'line-lengths', 'index.js'),
    ]);
    t.assert.strictEqual(spawned.stderr, '');
    t.assert.ok(spawned.stdout.includes(report));
    t.assert.strictEqual(spawned.code, 0);
  });

  await it('should throw when a source map is missing a source file', async (t) => {
    const file = fixtures.path('test-runner', 'source-maps', 'missing-sources', 'index.js');
    const missing = fixtures.path('test-runner', 'source-maps', 'missing-sources', 'nonexistent.js');
    const spawned = await common.spawnPromisified(process.execPath, [...flags, file]);

    const error = `Cannot find '${pathToFileURL(missing)}' imported from the source map for '${pathToFileURL(file)}'`;
    t.assert.strictEqual(spawned.stderr, '');
    t.assert.ok(spawned.stdout.includes(error));
    t.assert.strictEqual(spawned.code, 1);
  });

  for (const [file, message] of [
    [fixtures.path('test-runner', 'source-maps', 'invalid-json', 'index.js'), 'is not valid JSON'],
    [fixtures.path('test-runner', 'source-maps', 'missing-map.js'), 'does not exist'],
  ]) {
    await it(`should throw when a source map ${message}`, async (t) => {
      const spawned = await common.spawnPromisified(process.execPath, [...flags, file]);

      const error = `The source map for '${pathToFileURL(file)}' does not exist or is corrupt`;
      t.assert.strictEqual(spawned.stderr, '');
      t.assert.ok(spawned.stdout.includes(error));
      t.assert.strictEqual(spawned.code, 1);
    });
  }
});
