'use strict';
const common = require('../common');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { readdirSync } = require('node:fs');
const { test } = require('node:test');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const skipIfNoInspector = {
  skip: !process.features.inspector ? 'inspector disabled' : false
};

tmpdir.refresh();

function findCoverageFileForPid(pid) {
  const pattern = `^coverage\\-${pid}\\-(\\d{13})\\-(\\d+)\\.json$`;
  const regex = new RegExp(pattern);

  return readdirSync(tmpdir.path).find((file) => {
    return regex.test(file);
  });
}

function getTapCoverageFixtureReport() {

  const report = [
    '# start of coverage report',
    '# --------------------------------------------------------------------------------------------',
    '# file              | line % | branch % | funcs % | uncovered lines',
    '# --------------------------------------------------------------------------------------------',
    '# test              |        |          |         | ',
    '#  fixtures         |        |          |         | ',
    '#   test-runner     |        |          |         | ',
    '#    coverage.js    |  78.65 |    38.46 |   60.00 | 12-13 16-22 27 39 43-44 61-62 66-67 71-72',
    '#    invalid-tap.js | 100.00 |   100.00 |  100.00 | ',
    '#   v8-coverage     |        |          |         | ',
    '#    throw.js       |  71.43 |    50.00 |  100.00 | 5-6',
    '# --------------------------------------------------------------------------------------------',
    '# all files         |  78.35 |    43.75 |   60.00 | ',
    '# --------------------------------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');


  if (common.isWindows) {
    return report.replaceAll('/', '\\');
  }

  return report;
}

function getSpecCoverageFixtureReport() {

  const report = [
    '\u2139 start of coverage report',
    '\u2139 --------------------------------------------------------------------------------------------',
    '\u2139 file              | line % | branch % | funcs % | uncovered lines',
    '\u2139 --------------------------------------------------------------------------------------------',
    '\u2139 test              |        |          |         | ',
    '\u2139  fixtures         |        |          |         | ',
    '\u2139   test-runner     |        |          |         | ',
    '\u2139    coverage.js    |  78.65 |    38.46 |   60.00 | 12-13 16-22 27 39 43-44 61-62 66-67 71-72',
    '\u2139    invalid-tap.js | 100.00 |   100.00 |  100.00 | ',
    '\u2139   v8-coverage     |        |          |         | ',
    '\u2139    throw.js       |  71.43 |    50.00 |  100.00 | 5-6',
    '\u2139 --------------------------------------------------------------------------------------------',
    '\u2139 all files         |  78.35 |    43.75 |   60.00 | ',
    '\u2139 --------------------------------------------------------------------------------------------',
    '\u2139 end of coverage report',
  ].join('\n');


  if (common.isWindows) {
    return report.replaceAll('/', '\\');
  }

  return report;
}

test('test coverage report', async (t) => {
  await t.test('handles the inspector not being available', (t) => {
    if (process.features.inspector) {
      return;
    }

    const fixture = fixtures.path('test-runner', 'coverage.js');
    const args = [
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      fixture,
    ];
    const result = spawnSync(process.execPath, args);

    assert(!result.stdout.toString().includes('# start of coverage report'));
    assert(result.stderr.toString().includes('coverage could not be collected'));
    assert.strictEqual(result.status, 0);
    assert(!findCoverageFileForPid(result.pid));
  });
});

test('test tap coverage reporter', skipIfNoInspector, async (t) => {
  await t.test('coverage is reported and dumped to NODE_V8_COVERAGE if present', (t) => {
    const fixture = fixtures.path('test-runner', 'coverage.js');
    const args = [
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      '--test-reporter',
      'tap',
      fixture,
    ];
    const options = { env: { ...process.env, NODE_V8_COVERAGE: tmpdir.path } };
    const result = spawnSync(process.execPath, args, options);
    const report = getTapCoverageFixtureReport();
    assert(result.stdout.toString().includes(report));
    assert.strictEqual(result.stderr.toString(), '');
    assert.strictEqual(result.status, 0);
    assert(findCoverageFileForPid(result.pid));
  });

  await t.test('coverage is reported without NODE_V8_COVERAGE present', (t) => {
    const fixture = fixtures.path('test-runner', 'coverage.js');
    const args = [
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      '--test-reporter',
      'tap',
      fixture,
    ];
    const result = spawnSync(process.execPath, args);
    const report = getTapCoverageFixtureReport();

    assert(result.stdout.toString().includes(report));
    assert.strictEqual(result.stderr.toString(), '');
    assert.strictEqual(result.status, 0);
    assert(!findCoverageFileForPid(result.pid));
  });
});

test('test spec coverage reporter', skipIfNoInspector, async (t) => {
  await t.test('coverage is reported and dumped to NODE_V8_COVERAGE if present', (t) => {
    const fixture = fixtures.path('test-runner', 'coverage.js');
    const args = [
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      '--test-reporter',
      'spec',
      fixture];
    const options = { env: { ...process.env, NODE_V8_COVERAGE: tmpdir.path } };
    const result = spawnSync(process.execPath, args, options);
    const report = getSpecCoverageFixtureReport();

    assert(result.stdout.toString().includes(report));
    assert.strictEqual(result.stderr.toString(), '');
    assert.strictEqual(result.status, 0);
    assert(findCoverageFileForPid(result.pid));
  });

  await t.test('coverage is reported without NODE_V8_COVERAGE present', (t) => {
    const fixture = fixtures.path('test-runner', 'coverage.js');
    const args = [
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      '--test-reporter',
      'spec',
      fixture];
    const result = spawnSync(process.execPath, args);
    const report = getSpecCoverageFixtureReport();

    assert(result.stdout.toString().includes(report));
    assert.strictEqual(result.stderr.toString(), '');
    assert.strictEqual(result.status, 0);
    assert(!findCoverageFileForPid(result.pid));
  });
});

test('single process coverage is the same with --test', skipIfNoInspector, () => {
  const fixture = fixtures.path('test-runner', 'coverage.js');
  const args = [
    '--test',
    '--experimental-test-coverage',
    '--test-coverage-exclude=!test/**',
    '--test-reporter',
    'tap',
    fixture,
  ];
  const result = spawnSync(process.execPath, args);
  const report = getTapCoverageFixtureReport();

  assert.strictEqual(result.stderr.toString(), '');
  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
  assert(!findCoverageFileForPid(result.pid));
});

test('coverage is combined for multiple processes', skipIfNoInspector, () => {
  let report = [
    '# start of coverage report',
    '# -------------------------------------------------------------------',
    '# file           | line % | branch % | funcs % | uncovered lines',
    '# -------------------------------------------------------------------',
    '# common.js      |  89.86 |    62.50 |  100.00 | 8 13-14 18 34-35 53',
    '# first.test.js  |  83.33 |   100.00 |   50.00 | 5-6',
    '# second.test.js | 100.00 |   100.00 |  100.00 | ',
    '# third.test.js  | 100.00 |   100.00 |  100.00 | ',
    '# -------------------------------------------------------------------',
    '# all files      |  92.11 |    72.73 |   88.89 | ',
    '# -------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');

  if (common.isWindows) {
    report = report.replaceAll('/', '\\');
  }

  const fixture = fixtures.path('v8-coverage', 'combined_coverage');
  const args = [
    '--test',
    '--experimental-test-coverage',
    '--test-coverage-exclude=!test/**',
    '--test-reporter',
    'tap',
  ];
  const result = spawnSync(process.execPath, args, {
    env: { ...process.env, NODE_TEST_TMPDIR: tmpdir.path },
    cwd: fixture,
  });

  assert.strictEqual(result.stderr.toString(), '');
  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
});

test.skip('coverage works with isolation=none', skipIfNoInspector, () => {
  // There is a bug in coverage calculation. The branch % in the common.js
  // fixture is different depending on the test isolation mode. The 'none' mode
  // is closer to what c8 reports here, so the bug is likely in the code that
  // merges reports from different processes.
  let report = [
    '# start of coverage report',
    '# -------------------------------------------------------------------',
    '# file           | line % | branch % | funcs % | uncovered lines',
    '# -------------------------------------------------------------------',
    '# common.js      |  89.86 |    68.42 |  100.00 | 8 13-14 18 34-35 53',
    '# first.test.js  |  83.33 |   100.00 |   50.00 | 5-6',
    '# second.test.js | 100.00 |   100.00 |  100.00 | ',
    '# third.test.js  | 100.00 |   100.00 |  100.00 | ',
    '# -------------------------------------------------------------------',
    '# all files      |  92.11 |    76.00 |   88.89 | ',
    '# -------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');

  if (common.isWindows) {
    report = report.replaceAll('/', '\\');
  }

  const fixture = fixtures.path('v8-coverage', 'combined_coverage');
  const args = [
    '--test',
    '--experimental-test-coverage',
    '--test-reporter',
    'tap',
    '--test-isolation=none',
  ];
  const result = spawnSync(process.execPath, args, {
    env: { ...process.env, NODE_TEST_TMPDIR: tmpdir.path },
    cwd: fixture,
  });

  assert.strictEqual(result.stderr.toString(), '');
  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
});

test('coverage reports on lines, functions, and branches', skipIfNoInspector, async (t) => {
  const fixture = fixtures.path('test-runner', 'coverage.js');
  const child = spawnSync(process.execPath,
                          [
                            '--test',
                            '--experimental-test-coverage',
                            '--test-coverage-exclude=!test/**',
                            '--test-reporter',
                            fixtures.fileURL('test-runner/custom_reporters/coverage.mjs'),
                            fixture,
                          ]);
  assert.strictEqual(child.stderr.toString(), '');
  const stdout = child.stdout.toString();
  const coverage = JSON.parse(stdout);

  await t.test('does not include node_modules', () => {
    assert.strictEqual(coverage.summary.files.length, 3);
    const files = ['coverage.js', 'invalid-tap.js', 'throw.js'];
    coverage.summary.files.forEach((file, index) => {
      assert.ok(file.path.endsWith(files[index]));
    });
  });

  const file = coverage.summary.files[0];

  await t.test('reports on function coverage', () => {
    const uncalledFunction = file.functions.find((f) => f.name === 'uncalledTopLevelFunction');
    assert.strictEqual(uncalledFunction.count, 0);
    assert.strictEqual(uncalledFunction.line, 16);

    const calledTwice = file.functions.find((f) => f.name === 'fnWithControlFlow');
    assert.strictEqual(calledTwice.count, 2);
    assert.strictEqual(calledTwice.line, 35);
  });

  await t.test('reports on branch coverage', () => {
    const uncalledBranch = file.branches.find((b) => b.line === 6);
    assert.strictEqual(uncalledBranch.count, 0);

    const calledTwice = file.branches.find((b) => b.line === 35);
    assert.strictEqual(calledTwice.count, 2);
  });

  await t.test('reports on line coverage', () => {
    [
      { line: 36, count: 2 },
      { line: 37, count: 1 },
      { line: 38, count: 1 },
      { line: 39, count: 0 },
      { line: 40, count: 1 },
      { line: 41, count: 1 },
      { line: 42, count: 1 },
      { line: 43, count: 0 },
      { line: 44, count: 0 },
    ].forEach((line) => {
      const testLine = file.lines.find((l) => l.line === line.line);
      assert.strictEqual(testLine.count, line.count);
    });
  });
});

test('coverage with ESM hook - source irrelevant', skipIfNoInspector, () => {
  let report = [
    '# start of coverage report',
    '# ------------------------------------------------------------------',
    '# file              | line % | branch % | funcs % | uncovered lines',
    '# ------------------------------------------------------------------',
    '# hooks.mjs         | 100.00 |   100.00 |  100.00 | ',
    '# register-hooks.js | 100.00 |   100.00 |  100.00 | ',
    '# virtual.js        | 100.00 |   100.00 |  100.00 | ',
    '# ------------------------------------------------------------------',
    '# all files         | 100.00 |   100.00 |  100.00 | ',
    '# ------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');

  if (common.isWindows) {
    report = report.replaceAll('/', '\\');
  }

  const fixture = fixtures.path('test-runner', 'coverage-loader');
  const args = [
    '--import',
    './register-hooks.js',
    '--test',
    '--experimental-test-coverage',
    '--test-coverage-exclude=!test/**',
    '--test-reporter',
    'tap',
    'virtual.js',
  ];
  const result = spawnSync(process.execPath, args, { cwd: fixture });

  assert.strictEqual(result.stderr.toString(), '');
  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
});

test('coverage with ESM hook - source transpiled', skipIfNoInspector, () => {
  let report = [
    '# start of coverage report',
    '# ------------------------------------------------------------------',
    '# file              | line % | branch % | funcs % | uncovered lines',
    '# ------------------------------------------------------------------',
    '# hooks.mjs         | 100.00 |   100.00 |  100.00 | ',
    '# register-hooks.js | 100.00 |   100.00 |  100.00 | ',
    '# sum.test.ts       | 100.00 |   100.00 |  100.00 | ',
    '# sum.ts            | 100.00 |   100.00 |  100.00 | ',
    '# ------------------------------------------------------------------',
    '# all files         | 100.00 |   100.00 |  100.00 | ',
    '# ------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');

  if (common.isWindows) {
    report = report.replaceAll('/', '\\');
  }

  const fixture = fixtures.path('test-runner', 'coverage-loader');
  const args = [
    '--import', './register-hooks.js',
    '--test',
    '--experimental-test-coverage',
    '--test-coverage-exclude=!test/**',
    '--test-reporter', 'tap', 'sum.test.ts',
  ];
  const result = spawnSync(process.execPath, args, { cwd: fixture });

  assert.strictEqual(result.stderr.toString(), '');
  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
});

test('coverage with excluded files', skipIfNoInspector, () => {
  const fixture = fixtures.path('test-runner', 'coverage.js');
  const args = [
    '--experimental-test-coverage', '--test-reporter', 'tap',
    '--test-coverage-exclude=test/*/test-runner/invalid-tap.js',
    '--test-coverage-exclude=!test/**',
    fixture];
  const result = spawnSync(process.execPath, args);
  const report = [
    '# start of coverage report',
    '# -----------------------------------------------------------------------------------------',
    '# file           | line % | branch % | funcs % | uncovered lines',
    '# -----------------------------------------------------------------------------------------',
    '# test           |        |          |         | ',
    '#  fixtures      |        |          |         | ',
    '#   test-runner  |        |          |         | ',
    '#    coverage.js |  78.65 |    38.46 |   60.00 | 12-13 16-22 27 39 43-44 61-62 66-67 71-72',
    '#   v8-coverage  |        |          |         | ',
    '#    throw.js    |  71.43 |    50.00 |  100.00 | 5-6',
    '# -----------------------------------------------------------------------------------------',
    '# all files      |  78.13 |    40.00 |   60.00 | ',
    '# -----------------------------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');


  if (common.isWindows) {
    return report.replaceAll('/', '\\');
  }

  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
  assert(!findCoverageFileForPid(result.pid));
});

test('coverage with included files', skipIfNoInspector, () => {
  const fixture = fixtures.path('test-runner', 'coverage.js');
  const args = [
    '--experimental-test-coverage', '--test-reporter', 'tap',
    '--test-coverage-include=test/fixtures/test-runner/coverage.js',
    '--test-coverage-include=test/fixtures/v8-coverage/throw.js',
    '--test-coverage-exclude=!test/**',
    fixture,
  ];
  const result = spawnSync(process.execPath, args);
  const report = [
    '# start of coverage report',
    '# -----------------------------------------------------------------------------------------',
    '# file           | line % | branch % | funcs % | uncovered lines',
    '# -----------------------------------------------------------------------------------------',
    '# test           |        |          |         | ',
    '#  fixtures      |        |          |         | ',
    '#   test-runner  |        |          |         | ',
    '#    coverage.js |  78.65 |    38.46 |   60.00 | 12-13 16-22 27 39 43-44 61-62 66-67 71-72',
    '#   v8-coverage  |        |          |         | ',
    '#    throw.js    |  71.43 |    50.00 |  100.00 | 5-6',
    '# -----------------------------------------------------------------------------------------',
    '# all files      |  78.13 |    40.00 |   60.00 | ',
    '# -----------------------------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');


  if (common.isWindows) {
    return report.replaceAll('/', '\\');
  }

  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
  assert(!findCoverageFileForPid(result.pid));
});

test('coverage with included and excluded files', skipIfNoInspector, () => {
  const fixture = fixtures.path('test-runner', 'coverage.js');
  const args = [
    '--experimental-test-coverage', '--test-reporter', 'tap',
    '--test-coverage-include=test/fixtures/test-runner/*.js',
    '--test-coverage-exclude=test/fixtures/test-runner/*-tap.js',
    fixture,
  ];
  const result = spawnSync(process.execPath, args);
  const report = [
    '# start of coverage report',
    '# -----------------------------------------------------------------------------------------',
    '# file           | line % | branch % | funcs % | uncovered lines',
    '# -----------------------------------------------------------------------------------------',
    '# test           |        |          |         | ',
    '#  fixtures      |        |          |         | ',
    '#   test-runner  |        |          |         | ',
    '#    coverage.js |  78.65 |    38.46 |   60.00 | 12-13 16-22 27 39 43-44 61-62 66-67 71-72',
    '# -----------------------------------------------------------------------------------------',
    '# all files      |  78.65 |    38.46 |   60.00 | ',
    '# -----------------------------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');


  if (common.isWindows) {
    return report.replaceAll('/', '\\');
  }

  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
  assert(!findCoverageFileForPid(result.pid));
});

test('correctly prints the coverage report of files contained in parent directories', skipIfNoInspector, () => {
  let report = [
    '# start of coverage report',
    '# --------------------------------------------------------------------------------------------',
    '# file              | line % | branch % | funcs % | uncovered lines',
    '# --------------------------------------------------------------------------------------------',
    '# ..                |        |          |         | ',
    '#  coverage.js      |  78.65 |    38.46 |   60.00 | 12-13 16-22 27 39 43-44 61-62 66-67 71-72',
    '#  invalid-tap.js   | 100.00 |   100.00 |  100.00 | ',
    '#  ..               |        |          |         | ',
    '#   v8-coverage     |        |          |         | ',
    '#    throw.js       |  71.43 |    50.00 |  100.00 | 5-6',
    '# --------------------------------------------------------------------------------------------',
    '# all files         |  78.35 |    43.75 |   60.00 | ',
    '# --------------------------------------------------------------------------------------------',
    '# end of coverage report',
  ].join('\n');

  if (common.isWindows) {
    report = report.replaceAll('/', '\\');
  }
  const fixture = fixtures.path('test-runner', 'coverage.js');
  const args = [
    '--test',
    '--experimental-test-coverage',
    '--test-coverage-exclude=!test/**',
    '--test-reporter',
    'tap',
    fixture,
  ];
  const result = spawnSync(process.execPath, args, {
    env: { ...process.env, NODE_TEST_TMPDIR: tmpdir.path },
    cwd: fixtures.path('test-runner', 'coverage'),
  });

  assert.strictEqual(result.stderr.toString(), '');
  assert(result.stdout.toString().includes(report));
  assert.strictEqual(result.status, 0);
});
