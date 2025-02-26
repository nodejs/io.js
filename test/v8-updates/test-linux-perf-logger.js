'use strict';

// --- About this test suite
//
// JIT support for perf(1) was added in 2009 (see https://lkml.org/lkml/2009/6/8/499).
// It works by looking for a perf map file in /tmp/perf-<pid>.map, where <pid> is the
// PID of the target process.
//
// The structure of this file is stable. Perf expects each line to specify a symbol
// in the form:
//
//     <start> <length> <name>
//
// where <start> is the hex representation of the instruction pointer for the beginning
// of the function, <length> is the byte length of the function, and <name> is the
// readable JIT name used for reporting.
//
// This file asserts that a node script run with the appropriate flags will produce
// a compliant perf map.
//
// NOTE: This test runs only on linux, as that is the only platform supported by perf, and
// accordingly the only platform where `perf-basic-prof*` v8 flags are available.
//
// MAINTAINERS' NOTE: As of early 2024, the most common failure mode for this test suite
// is for v8 options to change from version to version. If this suite fails, look there first.
// We use options to forcibly require certain test cases to JIT code, and the nodeFlags to do
// so can change.

const common = require('../common');
if (!common.isLinux) {
  common.skip('--perf-basic-prof* is statically defined as linux-only');
}

const assert = require('assert');
const { spawnSync } = require('child_process');
const { readFileSync } = require('fs');

const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const testCases = [
  {
    title: '--perf-basic-prof interpreted',
    nodeFlags: ['--perf-basic-prof', '--no-turbo-inlining', '--no-opt'],
    matches: [
      'JS:~functionOne .+/linux-perf-logger.js',
      'JS:~functionTwo .+/linux-perf-logger.js',
      String.raw`RegExp\.> src: 'test-regex' flags: 'gi'`,
    ],
    noMatches: [
      String.raw`JS:\*'functionOne`,
      String.raw`JS:\*'functionTwo`,
    ],
  },
  {
    title: '--perf-basic-prof compiled',
    nodeFlags: ['--perf-basic-prof', '--no-turbo-inlining', '--always-turbofan',
                '--minimum-invocations-before-optimization=0'],
    matches: [
      String.raw`RegExp\.> src: 'test-regex' flags: 'gi'`,
      'JS:~functionOne .+/linux-perf-logger.js',
      'JS:~functionTwo .+/linux-perf-logger.js',
      String.raw`JS:\*'functionOne .+/linux-perf-logger.js`,
      String.raw`JS:\*'functionTwo .+/linux-perf-logger.js`,
    ],
    noMatches: [],
  },
  {
    title: '--perf-basic-prof-only-functions interpreted',
    nodeFlags: ['--perf-basic-prof-only-functions', '--no-turbo-inlining', '--no-opt'],
    matches: [
      'JS:~functionOne .+/linux-perf-logger.js',
      'JS:~functionTwo .+/linux-perf-logger.js',
    ],
    noMatches: [
      String.raw`JS:\*'functionOne`,
      String.raw`JS:\*'functionTwo`,
      'test-regex',
    ],
  },
  {
    title: '--perf-basic-prof-only-functions compiled',
    nodeFlags: ['--perf-basic-prof-only-functions', '--no-turbo-inlining', '--always-turbofan',
                '--minimum-invocations-before-optimization=0'],
    matches: [
      'JS:~functionOne .+/linux-perf-logger.js',
      'JS:~functionTwo .+/linux-perf-logger.js',
      String.raw`JS:\*'functionOne .+/linux-perf-logger.js`,
      String.raw`JS:\*'functionTwo .+/linux-perf-logger.js`,
    ],
    noMatches: [
      'test-regex',
    ],
  },
];

function runTest(test) {
  const report = {
    title: test.title,
    perfMap: '[uninitialized]',
    errors: [],
  };

  const args = test.nodeFlags.concat(fixtures.path('linux-perf-logger.js'));
  const run = spawnSync(process.execPath, args, { cwd: tmpdir.path, encoding: 'utf8' });
  if (run.error) {
    report.errors.push(run.error.stack);
    return report;
  }
  if (run.status !== 0) {
    report.errors.push(`running script:\n${run.stderr}`);
    return report;
  }

  try {
    report.perfMap = readFileSync(`/tmp/perf-${run.pid}.map`, 'utf8');
  } catch (err) {
    report.errors.push(`reading perf map: ${err.stack}`);
    return report;
  }

  const hexRegex = '[a-fA-F0-9]+';
  for (const testRegex of test.matches) {
    const lineRegex = new RegExp(`${hexRegex} ${hexRegex}.* ${testRegex}`);
    if (!lineRegex.test(report.perfMap)) {
      report.errors.push(`Expected to match ${lineRegex}`);
    }
  }

  for (const regex of test.noMatches) {
    const noMatch = new RegExp(regex);
    if (noMatch.test(report.perfMap)) {
      report.errors.push(`Expected not to match ${noMatch}`);
    }
  }

  return report;
}

function serializeError(report, index) {
  return `[ERROR ${index + 1}] ${report.title}
Errors:
${report.errors.map((err, i) => `${i + 1}. ${err}`).join('\n')}
Perf map content:
${report.perfMap}
</end perf map content>
`;
}

function runSuite() {
  const failures = [];

  for (const tc of testCases) {
    const report = runTest(tc);
    if (report.errors.length > 0) {
      failures.push(report);
    }
  }

  const errorsToReport = failures.map(serializeError).join('\n--------\n');

  assert.strictEqual(failures.length, 0, `${failures.length} tests failed\n\n${errorsToReport}`);
}

runSuite();
