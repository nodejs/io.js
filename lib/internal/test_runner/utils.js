'use strict';
const {
  ArrayPrototypeFlatMap,
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypePush,
  ArrayPrototypeReduce,
  ArrayPrototypeSome,
  MathFloor,
  MathMax,
  MathMin,
  NumberParseInt,
  NumberPrototypeToFixed,
  ObjectGetOwnPropertyDescriptor,
  RegExp,
  RegExpPrototypeExec,
  SafeMap,
  SafePromiseAllReturnArrayLike,
  StringPrototypePadEnd,
  StringPrototypePadStart,
  StringPrototypeRepeat,
  StringPrototypeSlice,
  StringPrototypeSplit,
} = primordials;

const { AsyncResource } = require('async_hooks');
const { relative } = require('path');
const { createWriteStream } = require('fs');
const { pathToFileURL } = require('internal/url');
const { createDeferredPromise } = require('internal/util');
const { getOptionValue } = require('internal/options');
const { green, yellow, red, white, shouldColorize } = require('internal/util/colors');

const {
  codes: {
    ERR_INVALID_ARG_VALUE,
    ERR_TEST_FAILURE,
  },
  kIsNodeError,
} = require('internal/errors');
const { compose } = require('stream');

const coverageColors = {
  __proto__: null,
  high: green,
  medium: yellow,
  low: red,
};

const kMultipleCallbackInvocations = 'multipleCallbackInvocations';
const kRegExpPattern = /^\/(.*)\/([a-z]*)$/;

const kPatterns = ['test', 'test/**/*', 'test-*', '*[.-_]test'];
const kDefaultPattern = `**/{${ArrayPrototypeJoin(kPatterns, ',')}}.?(c|m)js`;


function createDeferredCallback() {
  let calledCount = 0;
  const { promise, resolve, reject } = createDeferredPromise();
  const cb = (err) => {
    calledCount++;

    // If the callback is called a second time, let the user know, but
    // don't let them know more than once.
    if (calledCount > 1) {
      if (calledCount === 2) {
        throw new ERR_TEST_FAILURE(
          'callback invoked multiple times',
          kMultipleCallbackInvocations,
        );
      }

      return;
    }

    if (err) {
      return reject(err);
    }

    resolve();
  };

  return { __proto__: null, promise, cb };
}

function isTestFailureError(err) {
  return err?.code === 'ERR_TEST_FAILURE' && kIsNodeError in err;
}

function convertStringToRegExp(str, name) {
  const match = RegExpPrototypeExec(kRegExpPattern, str);
  const pattern = match?.[1] ?? str;
  const flags = match?.[2] || '';

  try {
    return new RegExp(pattern, flags);
  } catch (err) {
    const msg = err?.message;

    throw new ERR_INVALID_ARG_VALUE(
      name,
      str,
      `is an invalid regular expression.${msg ? ` ${msg}` : ''}`,
    );
  }
}

const kBuiltinDestinations = new SafeMap([
  ['stdout', process.stdout],
  ['stderr', process.stderr],
]);

const kBuiltinReporters = new SafeMap([
  ['spec', 'internal/test_runner/reporter/spec'],
  ['dot', 'internal/test_runner/reporter/dot'],
  ['tap', 'internal/test_runner/reporter/tap'],
  ['junit', 'internal/test_runner/reporter/junit'],
  ['lcov', 'internal/test_runner/reporter/lcov'],
]);

const kDefaultReporter = process.stdout.isTTY ? 'spec' : 'tap';
const kDefaultDestination = 'stdout';

function tryBuiltinReporter(name) {
  const builtinPath = kBuiltinReporters.get(name);

  if (builtinPath === undefined) {
    return;
  }

  return require(builtinPath);
}

function shouldColorizeTestFiles(destinations) {
  // This function assumes only built-in destinations (stdout/stderr) supports coloring
  return ArrayPrototypeSome(destinations, (_, index) => {
    const destination = kBuiltinDestinations.get(destinations[index]);
    return destination && shouldColorize(destination);
  });
}

async function getReportersMap(reporters, destinations) {
  return SafePromiseAllReturnArrayLike(reporters, async (name, i) => {
    const destination = kBuiltinDestinations.get(destinations[i]) ?? createWriteStream(destinations[i]);

    // Load the test reporter passed to --test-reporter
    let reporter = tryBuiltinReporter(name);

    if (reporter === undefined) {
      let parentURL;

      try {
        parentURL = pathToFileURL(process.cwd() + '/').href;
      } catch {
        parentURL = 'file:///';
      }

      const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
      reporter = await cascadedLoader.import(name, parentURL, { __proto__: null });
    }

    if (reporter?.default) {
      reporter = reporter.default;
    }

    if (reporter?.prototype && ObjectGetOwnPropertyDescriptor(reporter.prototype, 'constructor')) {
      reporter = new reporter();
    }

    if (!reporter) {
      throw new ERR_INVALID_ARG_VALUE('Reporter', name, 'is not a valid reporter');
    }

    return { __proto__: null, reporter, destination };
  });
}

const reporterScope = new AsyncResource('TestReporterScope');
const setupTestReporters = reporterScope.bind(async (rootReporter) => {
  const { reporters, destinations } = parseCommandLine();
  const reportersMap = await getReportersMap(reporters, destinations);
  for (let i = 0; i < reportersMap.length; i++) {
    const { reporter, destination } = reportersMap[i];
    compose(rootReporter, reporter).pipe(destination);
  }
});

let globalTestOptions;

function parseCommandLine() {
  if (globalTestOptions) {
    return globalTestOptions;
  }

  const isTestRunner = getOptionValue('--test');
  const coverage = getOptionValue('--experimental-test-coverage');
  const forceExit = getOptionValue('--test-force-exit');
  const sourceMaps = getOptionValue('--enable-source-maps');
  const updateSnapshots = getOptionValue('--test-update-snapshots');
  const watchMode = getOptionValue('--watch');
  const isChildProcess = process.env.NODE_TEST_CONTEXT === 'child';
  const isChildProcessV8 = process.env.NODE_TEST_CONTEXT === 'child-v8';
  let coverageExcludeGlobs;
  let coverageIncludeGlobs;
  let destinations;
  let perFileTimeout;
  let reporters;
  let runnerConcurrency;
  let testNamePatterns;
  let testSkipPatterns;
  let testOnlyFlag;
  let shard;

  if (isChildProcessV8) {
    kBuiltinReporters.set('v8-serializer', 'internal/test_runner/reporter/v8-serializer');
    reporters = ['v8-serializer'];
    destinations = [kDefaultDestination];
  } else if (isChildProcess) {
    reporters = ['tap'];
    destinations = [kDefaultDestination];
  } else {
    destinations = getOptionValue('--test-reporter-destination');
    reporters = getOptionValue('--test-reporter');
    if (reporters.length === 0 && destinations.length === 0) {
      ArrayPrototypePush(reporters, kDefaultReporter);
    }

    if (reporters.length === 1 && destinations.length === 0) {
      ArrayPrototypePush(destinations, kDefaultDestination);
    }

    if (destinations.length !== reporters.length) {
      throw new ERR_INVALID_ARG_VALUE(
        '--test-reporter',
        reporters,
        'must match the number of specified \'--test-reporter-destination\'',
      );
    }
  }

  if (isTestRunner) {
    perFileTimeout = getOptionValue('--test-timeout') || Infinity;
    runnerConcurrency = getOptionValue('--test-concurrency') || true;
    testOnlyFlag = false;
    testNamePatterns = null;

    const shardOption = getOptionValue('--test-shard');
    if (shardOption) {
      if (!RegExpPrototypeExec(/^\d+\/\d+$/, shardOption)) {
        throw new ERR_INVALID_ARG_VALUE(
          '--test-shard',
          shardOption,
          'must be in the form of <index>/<total>',
        );
      }

      const indexAndTotal = StringPrototypeSplit(shardOption, '/');
      shard = {
        __proto__: null,
        index: NumberParseInt(indexAndTotal[0], 10),
        total: NumberParseInt(indexAndTotal[1], 10),
      };
    }
  } else {
    perFileTimeout = Infinity;
    runnerConcurrency = 1;
    const testNamePatternFlag = getOptionValue('--test-name-pattern');
    testOnlyFlag = getOptionValue('--test-only');
    testNamePatterns = testNamePatternFlag?.length > 0 ?
      ArrayPrototypeMap(
        testNamePatternFlag,
        (re) => convertStringToRegExp(re, '--test-name-pattern'),
      ) : null;
    const testSkipPatternFlag = getOptionValue('--test-skip-pattern');
    testSkipPatterns = testSkipPatternFlag?.length > 0 ?
      ArrayPrototypeMap(testSkipPatternFlag, (re) => convertStringToRegExp(re, '--test-skip-pattern')) : null;
  }

  if (coverage) {
    coverageExcludeGlobs = getOptionValue('--test-coverage-exclude');
    coverageIncludeGlobs = getOptionValue('--test-coverage-include');
  }

  globalTestOptions = {
    __proto__: null,
    isTestRunner,
    coverage,
    coverageExcludeGlobs,
    coverageIncludeGlobs,
    forceExit,
    perFileTimeout,
    runnerConcurrency,
    shard,
    sourceMaps,
    testOnlyFlag,
    testNamePatterns,
    testSkipPatterns,
    updateSnapshots,
    reporters,
    destinations,
    watchMode,
  };

  return globalTestOptions;
}

function countCompletedTest(test, harness = test.root.harness) {
  if (test.nesting === 0) {
    harness.counters.topLevel++;
  }
  if (test.reportedType === 'suite') {
    harness.counters.suites++;
    return;
  }
  // Check SKIP and TODO tests first, as those should not be counted as
  // failures.
  if (test.skipped) {
    harness.counters.skipped++;
  } else if (test.isTodo) {
    harness.counters.todo++;
  } else if (test.cancelled) {
    harness.counters.cancelled++;
  } else if (!test.passed) {
    harness.counters.failed++;
  } else {
    harness.counters.passed++;
  }
  harness.counters.all++;
}


const memo = new SafeMap();
function addTableLine(prefix, width) {
  const key = `${prefix}-${width}`;
  let value = memo.get(key);
  if (value === undefined) {
    value = `${prefix}${StringPrototypeRepeat('-', width)}\n`;
    memo.set(key, value);
  }

  return value;
}

const kHorizontalEllipsis = '\u2026';
function truncateStart(string, width) {
  return string.length > width ? `${kHorizontalEllipsis}${StringPrototypeSlice(string, string.length - width + 1)}` : string;
}

function truncateEnd(string, width) {
  return string.length > width ? `${StringPrototypeSlice(string, 0, width - 1)}${kHorizontalEllipsis}` : string;
}

function formatLinesToRanges(values) {
  return ArrayPrototypeMap(ArrayPrototypeReduce(values, (prev, current, index, array) => {
    if ((index > 0) && ((current - array[index - 1]) === 1)) {
      prev[prev.length - 1][1] = current;
    } else {
      prev.push([current]);
    }
    return prev;
  }, []), (range) => ArrayPrototypeJoin(range, '-'));
}

function getUncoveredLines(lines) {
  return ArrayPrototypeFlatMap(lines, (line) => (line.count === 0 ? line.line : []));
}

function formatUncoveredLines(lines, table) {
  if (table) return ArrayPrototypeJoin(formatLinesToRanges(lines), ' ');
  return ArrayPrototypeJoin(lines, ', ');
}

const kColumns = ['line %', 'branch %', 'funcs %'];
const kColumnsKeys = ['coveredLinePercent', 'coveredBranchPercent', 'coveredFunctionPercent'];
const kSeparator = ' | ';

function getCoverageReport(pad, summary, symbol, color, table) {
  const prefix = `${pad}${symbol}`;
  let report = `${color}${prefix}start of coverage report\n`;

  let filePadLength;
  let columnPadLengths = [];
  let uncoveredLinesPadLength;
  let tableWidth;

  if (table) {
    // Get expected column sizes
    filePadLength = table && ArrayPrototypeReduce(summary.files, (acc, file) =>
      MathMax(acc, relative(summary.workingDirectory, file.path).length), 0);
    filePadLength = MathMax(filePadLength, 'file'.length);
    const fileWidth = filePadLength + 2;

    columnPadLengths = ArrayPrototypeMap(kColumns, (column) => (table ? MathMax(column.length, 6) : 0));
    const columnsWidth = ArrayPrototypeReduce(columnPadLengths, (acc, columnPadLength) => acc + columnPadLength + 3, 0);

    uncoveredLinesPadLength = table && ArrayPrototypeReduce(summary.files, (acc, file) =>
      MathMax(acc, formatUncoveredLines(getUncoveredLines(file.lines), table).length), 0);
    uncoveredLinesPadLength = MathMax(uncoveredLinesPadLength, 'uncovered lines'.length);
    const uncoveredLinesWidth = uncoveredLinesPadLength + 2;

    tableWidth = fileWidth + columnsWidth + uncoveredLinesWidth;

    // Fit with sensible defaults
    const availableWidth = (process.stdout.columns || Infinity) - prefix.length;
    const columnsExtras = tableWidth - availableWidth;
    if (table && columnsExtras > 0) {
      // Ensure file name is sufficiently visible
      const minFilePad = MathMin(8, filePadLength);
      filePadLength -= MathFloor(columnsExtras * 0.2);
      filePadLength = MathMax(filePadLength, minFilePad);

      // Get rest of available space, subtracting margins
      uncoveredLinesPadLength = MathMax(availableWidth - columnsWidth - (filePadLength + 2) - 2, 1);

      // Update table width
      tableWidth = availableWidth;
    } else {
      uncoveredLinesPadLength = Infinity;
    }
  }


  function getCell(string, width, pad, truncate, coverage) {
    if (!table) return string;

    let result = string;
    if (pad) result = pad(result, width);
    if (truncate) result = truncate(result, width);
    if (color && coverage !== undefined) {
      if (coverage > 90) return `${coverageColors.high}${result}${color}`;
      if (coverage > 50) return `${coverageColors.medium}${result}${color}`;
      return `${coverageColors.low}${result}${color}`;
    }
    return result;
  }

  // Head
  if (table) report += addTableLine(prefix, tableWidth);
  report += `${prefix}${getCell('file', filePadLength, StringPrototypePadEnd, truncateEnd)}${kSeparator}` +
            `${ArrayPrototypeJoin(ArrayPrototypeMap(kColumns, (column, i) => getCell(column, columnPadLengths[i], StringPrototypePadStart)), kSeparator)}${kSeparator}` +
            `${getCell('uncovered lines', uncoveredLinesPadLength, false, truncateEnd)}\n`;
  if (table) report += addTableLine(prefix, tableWidth);

  // Body
  for (let i = 0; i < summary.files.length; ++i) {
    const file = summary.files[i];
    const relativePath = relative(summary.workingDirectory, file.path);

    let fileCoverage = 0;
    const coverages = ArrayPrototypeMap(kColumnsKeys, (columnKey) => {
      const percent = file[columnKey];
      fileCoverage += percent;
      return percent;
    });
    fileCoverage /= kColumnsKeys.length;

    report += `${prefix}${getCell(relativePath, filePadLength, StringPrototypePadEnd, truncateStart, fileCoverage)}${kSeparator}` +
              `${ArrayPrototypeJoin(ArrayPrototypeMap(coverages, (coverage, j) => getCell(NumberPrototypeToFixed(coverage, 2), columnPadLengths[j], StringPrototypePadStart, false, coverage)), kSeparator)}${kSeparator}` +
              `${getCell(formatUncoveredLines(getUncoveredLines(file.lines), table), uncoveredLinesPadLength, false, truncateEnd)}\n`;
  }

  // Foot
  if (table) report += addTableLine(prefix, tableWidth);
  report += `${prefix}${getCell('all files', filePadLength, StringPrototypePadEnd, truncateEnd)}${kSeparator}` +
            `${ArrayPrototypeJoin(ArrayPrototypeMap(kColumnsKeys, (columnKey, j) => getCell(NumberPrototypeToFixed(summary.totals[columnKey], 2), columnPadLengths[j], StringPrototypePadStart, false, summary.totals[columnKey])), kSeparator)} |\n`;
  if (table) report += addTableLine(prefix, tableWidth);

  report += `${prefix}end of coverage report\n`;
  if (color) {
    report += white;
  }
  return report;
}

module.exports = {
  convertStringToRegExp,
  countCompletedTest,
  createDeferredCallback,
  isTestFailureError,
  kDefaultPattern,
  parseCommandLine,
  reporterScope,
  setupTestReporters,
  shouldColorizeTestFiles,
  getCoverageReport,
};
