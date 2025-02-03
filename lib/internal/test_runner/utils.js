'use strict';
const {
  ArrayPrototypeFlatMap,
  ArrayPrototypeForEach,
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypePop,
  ArrayPrototypePush,
  ArrayPrototypeReduce,
  ArrayPrototypeSome,
  MathFloor,
  MathMax,
  MathMin,
  NumberParseInt,
  NumberPrototypeToFixed,
  ObjectGetOwnPropertyDescriptor,
  PromiseWithResolvers,
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
const { relative, sep } = require('path');
const { createWriteStream } = require('fs');
const { pathToFileURL } = require('internal/url');
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
const { validateInteger } = require('internal/validators');

const coverageColors = {
  __proto__: null,
  high: green,
  medium: yellow,
  low: red,
};

const kMultipleCallbackInvocations = 'multipleCallbackInvocations';
const kRegExpPattern = /^\/(.*)\/([a-z]*)$/;

const kPatterns = ['test', 'test/**/*', 'test-*', '*[._-]test'];
const kFileExtensions = ['js', 'mjs', 'cjs'];
if (getOptionValue('--experimental-strip-types')) {
  ArrayPrototypePush(kFileExtensions, 'ts', 'mts', 'cts');
}
const kDefaultPattern = `**/{${ArrayPrototypeJoin(kPatterns, ',')}}.{${ArrayPrototypeJoin(kFileExtensions, ',')}}`;

function createDeferredCallback() {
  let calledCount = 0;
  const { promise, resolve, reject } = PromiseWithResolvers();
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

const kDefaultReporter = 'spec';
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
    const destination = kBuiltinDestinations.get(destinations[i]) ??
      createWriteStream(destinations[i], { __proto__: null, flush: true });

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
  const watch = getOptionValue('--watch');
  const isChildProcess = process.env.NODE_TEST_CONTEXT === 'child';
  const isChildProcessV8 = process.env.NODE_TEST_CONTEXT === 'child-v8';
  let concurrency;
  let coverageExcludeGlobs;
  let coverageIncludeGlobs;
  let lineCoverage;
  let branchCoverage;
  let functionCoverage;
  let destinations;
  let isolation;
  let only = getOptionValue('--test-only');
  let reporters;
  let shard;
  let testNamePatterns = mapPatternFlagToRegExArray('--test-name-pattern');
  let testSkipPatterns = mapPatternFlagToRegExArray('--test-skip-pattern');
  let timeout;

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
    isolation = getOptionValue('--test-isolation');
    timeout = getOptionValue('--test-timeout') || Infinity;

    if (isolation === 'none') {
      concurrency = 1;
    } else {
      concurrency = getOptionValue('--test-concurrency') || true;
      only = false;
      testNamePatterns = null;
      testSkipPatterns = null;
    }

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
    timeout = Infinity;
    concurrency = 1;
    const testNamePatternFlag = getOptionValue('--test-name-pattern');
    only = getOptionValue('--test-only');
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
    if (!coverageExcludeGlobs || coverageExcludeGlobs.length === 0) {
      // TODO(pmarchini): this default should follow something similar to c8 defaults
      // Default exclusions should be also exported to be used by other tools / users
      coverageExcludeGlobs = [kDefaultPattern];
    }
    coverageIncludeGlobs = getOptionValue('--test-coverage-include');

    branchCoverage = getOptionValue('--test-coverage-branches');
    lineCoverage = getOptionValue('--test-coverage-lines');
    functionCoverage = getOptionValue('--test-coverage-functions');

    validateInteger(branchCoverage, '--test-coverage-branches', 0, 100);
    validateInteger(lineCoverage, '--test-coverage-lines', 0, 100);
    validateInteger(functionCoverage, '--test-coverage-functions', 0, 100);
  }

  const setup = reporterScope.bind(async (rootReporter) => {
    const reportersMap = await getReportersMap(reporters, destinations);

    for (let i = 0; i < reportersMap.length; i++) {
      const { reporter, destination } = reportersMap[i];
      compose(rootReporter, reporter).pipe(destination);
    }

    reporterScope.reporters = reportersMap;
  });

  globalTestOptions = {
    __proto__: null,
    isTestRunner,
    concurrency,
    coverage,
    coverageExcludeGlobs,
    coverageIncludeGlobs,
    destinations,
    forceExit,
    isolation,
    branchCoverage,
    functionCoverage,
    lineCoverage,
    only,
    reporters,
    setup,
    shard,
    sourceMaps,
    testNamePatterns,
    testSkipPatterns,
    timeout,
    updateSnapshots,
    watch,
  };

  return globalTestOptions;
}

function mapPatternFlagToRegExArray(flagName) {
  const patterns = getOptionValue(flagName);

  if (patterns?.length > 0) {
    return ArrayPrototypeMap(patterns, (re) => convertStringToRegExp(re, flagName));
  }

  return null;
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
    harness.success = false;
  } else if (!test.passed) {
    harness.counters.failed++;
    harness.success = false;
  } else {
    harness.counters.passed++;
  }
  harness.counters.tests++;
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

function buildFileTree(summary) {
  const tree = { __proto__: null };
  let treeDepth = 1;
  let longestFile = 0;

  ArrayPrototypeForEach(summary.files, (file) => {
    let longestPart = 0;
    const parts = StringPrototypeSplit(relative(summary.workingDirectory, file.path), sep);
    let current = tree;

    ArrayPrototypeForEach(parts, (part, index) => {
      current[part] ||= { __proto__: null };
      current = current[part];
      // If this is the last part, add the file to the tree
      if (index === parts.length - 1) {
        current.file = file;
      }
      // Keep track of the longest part for padding
      longestPart = MathMax(longestPart, part.length);
    });

    treeDepth = MathMax(treeDepth, parts.length);
    longestFile = MathMax(longestPart, longestFile);
  });

  return { __proto__: null, tree, treeDepth, longestFile };
}

function getCoverageReport(pad, summary, symbol, color, table) {
  const prefix = `${pad}${symbol}`;
  let report = `${color}${prefix}start of coverage report\n`;

  let filePadLength;
  let columnPadLengths = [];
  let uncoveredLinesPadLength;
  let tableWidth;

  // Create a tree of file paths
  const { tree, treeDepth, longestFile } = buildFileTree(summary);
  if (table) {
    // Calculate expected column sizes based on the tree
    filePadLength = table && longestFile;
    filePadLength += (treeDepth - 1);
    if (color) {
      filePadLength += 2;
    }
    filePadLength = MathMax(filePadLength, 'file'.length);
    if (filePadLength > (process.stdout.columns / 2)) {
      filePadLength = MathFloor(process.stdout.columns / 2);
    }
    const fileWidth = filePadLength + 2;

    columnPadLengths = ArrayPrototypeMap(kColumns, (column) => (table ? MathMax(column.length, 6) : 0));
    const columnsWidth = ArrayPrototypeReduce(columnPadLengths, (acc, columnPadLength) => acc + columnPadLength + 3, 0);

    uncoveredLinesPadLength = table && ArrayPrototypeReduce(summary.files, (acc, file) =>
      MathMax(acc, formatUncoveredLines(getUncoveredLines(file.lines), table).length), 0);
    uncoveredLinesPadLength = MathMax(uncoveredLinesPadLength, 'uncovered lines'.length);
    const uncoveredLinesWidth = uncoveredLinesPadLength + 2;

    tableWidth = fileWidth + columnsWidth + uncoveredLinesWidth;

    const availableWidth = (process.stdout.columns || Infinity) - prefix.length;
    const columnsExtras = tableWidth - availableWidth;
    if (table && columnsExtras > 0) {
      filePadLength = MathMin(availableWidth * 0.5, filePadLength);
      uncoveredLinesPadLength = MathMax(availableWidth - columnsWidth - (filePadLength + 2) - 2, 1);
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

  function writeReportLine({ file, depth = 0, coveragesColumns, fileCoverage, uncoveredLines }) {
    const fileColumn = `${prefix}${StringPrototypeRepeat(' ', depth)}${getCell(file, filePadLength - depth, StringPrototypePadEnd, truncateStart, fileCoverage)}`;
    const coverageColumns = ArrayPrototypeJoin(ArrayPrototypeMap(coveragesColumns, (coverage, j) => {
      const coverageText = typeof coverage === 'number' ? NumberPrototypeToFixed(coverage, 2) : coverage;
      return getCell(coverageText, columnPadLengths[j], StringPrototypePadStart, false, coverage);
    }), kSeparator);

    const uncoveredLinesColumn = getCell(uncoveredLines, uncoveredLinesPadLength, false, truncateEnd);

    return `${fileColumn}${kSeparator}${coverageColumns}${kSeparator}${uncoveredLinesColumn}\n`;
  }

  function printCoverageBodyTree(tree, depth = 0) {
    for (const key in tree) {
      if (tree[key].file) {
        const file = tree[key].file;
        const fileName = ArrayPrototypePop(StringPrototypeSplit(file.path, sep));

        let fileCoverage = 0;
        const coverages = ArrayPrototypeMap(kColumnsKeys, (columnKey) => {
          const percent = file[columnKey];
          fileCoverage += percent;
          return percent;
        });
        fileCoverage /= kColumnsKeys.length;

        const uncoveredLines = formatUncoveredLines(getUncoveredLines(file.lines), table);

        report += writeReportLine({
          __proto__: null,
          file: fileName,
          depth: depth,
          coveragesColumns: coverages,
          fileCoverage: fileCoverage,
          uncoveredLines: uncoveredLines,
        });
      } else {
        report += writeReportLine({
          __proto__: null,
          file: key,
          depth: depth,
          coveragesColumns: ArrayPrototypeMap(columnPadLengths, () => ''),
          fileCoverage: undefined,
          uncoveredLines: '',
        });
        printCoverageBodyTree(tree[key], depth + 1);
      }
    }
  }

  // -------------------------- Coverage Report --------------------------
  if (table) report += addTableLine(prefix, tableWidth);

  // Print the header
  report += writeReportLine({
    __proto__: null,
    file: 'file',
    coveragesColumns: kColumns,
    fileCoverage: undefined,
    uncoveredLines: 'uncovered lines',
  });

  if (table) report += addTableLine(prefix, tableWidth);

  // Print the body
  printCoverageBodyTree(tree);

  if (table) report += addTableLine(prefix, tableWidth);

  // Print the footer
  const allFilesCoverages = ArrayPrototypeMap(kColumnsKeys, (columnKey) => summary.totals[columnKey]);
  report += writeReportLine({
    __proto__: null,
    file: 'all files',
    coveragesColumns: allFilesCoverages,
    fileCoverage: undefined,
    uncoveredLines: '',
  });

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
  shouldColorizeTestFiles,
  getCoverageReport,
};
