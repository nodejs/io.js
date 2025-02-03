'use strict';
const common = require('../common');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const { readdirSync } = require('node:fs');
const { test } = require('node:test');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');

common.skipIfInspectorDisabled();
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

const fixture = fixtures.path('test-runner', 'coverage.js');
const reporter = fixtures.fileURL('test-runner/custom_reporters/coverage.mjs');

const coverages = [
  { flag: '--test-coverage-lines', name: 'line', actual: 78.35 },
  { flag: '--test-coverage-functions', name: 'function', actual: 60.00 },
  { flag: '--test-coverage-branches', name: 'branch', actual: 43.75 },
];

for (const coverage of coverages) {
  test(`test passing ${coverage.flag}`, () => {
    const result = spawnSync(process.execPath, [
      '--test',
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      `${coverage.flag}=25`,
      '--test-reporter', 'tap',
      fixture,
    ]);

    const stdout = result.stdout.toString();
    assert(stdout.includes(getTapCoverageFixtureReport()));
    assert.doesNotMatch(stdout, RegExp(`Error: [\\d\\.]+% ${coverage.name} coverage`));
    assert.strictEqual(result.status, 0);
    assert(!findCoverageFileForPid(result.pid));
  });

  test(`test passing ${coverage.flag} with custom reporter`, () => {
    const result = spawnSync(process.execPath, [
      '--test',
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      `${coverage.flag}=25`,
      '--test-reporter', reporter,
      fixture,
    ]);

    const stdout = JSON.parse(result.stdout.toString());
    assert.strictEqual(stdout.summary.thresholds[coverage.name], 25);
    assert.strictEqual(result.status, 0);
    assert(!findCoverageFileForPid(result.pid));
  });

  test(`test failing ${coverage.flag}`, () => {
    const result = spawnSync(process.execPath, [
      '--test',
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      `${coverage.flag}=99`,
      '--test-reporter', 'tap',
      fixture,
    ]);

    const stdout = result.stdout.toString();
    assert(stdout.includes(getTapCoverageFixtureReport()));
    assert.match(stdout, RegExp(`Error: ${coverage.actual.toFixed(2)}% ${coverage.name} coverage does not meet threshold of 99%`));
    assert.strictEqual(result.status, 1);
    assert(!findCoverageFileForPid(result.pid));
  });

  test(`test failing ${coverage.flag} with custom reporter`, () => {
    const result = spawnSync(process.execPath, [
      '--test',
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      `${coverage.flag}=99`,
      '--test-reporter', reporter,
      fixture,
    ]);

    const stdout = JSON.parse(result.stdout.toString());
    assert.strictEqual(stdout.summary.thresholds[coverage.name], 99);
    assert.strictEqual(result.status, 1);
    assert(!findCoverageFileForPid(result.pid));
  });

  test(`test out-of-range ${coverage.flag} (too high)`, () => {
    const result = spawnSync(process.execPath, [
      '--test',
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      `${coverage.flag}=101`,
      fixture,
    ]);

    assert.match(result.stderr.toString(), RegExp(`The value of "${coverage.flag}`));
    assert.strictEqual(result.status, 1);
    assert(!findCoverageFileForPid(result.pid));
  });

  test(`test out-of-range ${coverage.flag} (too low)`, () => {
    const result = spawnSync(process.execPath, [
      '--test',
      '--experimental-test-coverage',
      '--test-coverage-exclude=!test/**',
      `${coverage.flag}=-1`,
      fixture,
    ]);

    assert.match(result.stderr.toString(), RegExp(`The value of "${coverage.flag}`));
    assert.strictEqual(result.status, 1);
    assert(!findCoverageFileForPid(result.pid));
  });
}
