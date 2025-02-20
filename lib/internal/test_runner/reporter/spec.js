'use strict';
const {
  ArrayPrototypeJoin,
  ArrayPrototypePop,
  ArrayPrototypePush,
  ArrayPrototypeShift,
  ArrayPrototypeUnshift,
} = primordials;
const assert = require('assert');
const Transform = require('internal/streams/transform');
const colors = require('internal/util/colors');
const { kSubtestsFailed } = require('internal/test_runner/test');
const { getCoverageReport } = require('internal/test_runner/utils');
const { relative } = require('path');
const {
  formatTestReport,
  indent,
  reporterColorMap,
  reporterUnicodeSymbolMap,
} = require('internal/test_runner/reporter/utils');

class SpecReporter extends Transform {
  #stack = [];
  #reported = [];
  #failedTests = [];
  #cwd = process.cwd();

  constructor() {
    super({ __proto__: null, writableObjectMode: true });
    colors.refresh();
  }

  #formatFailedTestResults() {
    if (this.#failedTests.length === 0) {
      return '';
    }

    const results = [
      `\n${reporterColorMap['test:fail']}${reporterUnicodeSymbolMap['test:fail']}failing tests:${colors.white}\n`,
    ];

    for (let i = 0; i < this.#failedTests.length; i++) {
      const test = this.#failedTests[i];
      const formattedErr = formatTestReport('test:fail', test);

      if (test.file) {
        const relPath = relative(this.#cwd, test.file);
        const location = `test at ${relPath}:${test.line}:${test.column}`;
        ArrayPrototypePush(results, location);
      }

      ArrayPrototypePush(results, formattedErr);
    }

    this.#failedTests = []; // Clean up the failed tests
    return ArrayPrototypeJoin(results, '\n'); ;
  }
  #handleTestReportEvent(type, data) {
    const subtest = ArrayPrototypeShift(this.#stack); // This is the matching `test:start` event
    if (subtest) {
      assert(subtest.type === 'test:start');
      assert(subtest.data.nesting === data.nesting);
      assert(subtest.data.name === data.name);
    }
    let prefix = '';
    while (this.#stack.length) {
      // Report all the parent `test:start` events
      const parent = ArrayPrototypePop(this.#stack);
      assert(parent.type === 'test:start');
      const msg = parent.data;
      ArrayPrototypeUnshift(this.#reported, msg);
      prefix += `${indent(msg.nesting)}${reporterUnicodeSymbolMap['arrow:right']}${msg.name}\n`;
    }
    let hasChildren = false;
    if (this.#reported[0] && this.#reported[0].nesting === data.nesting && this.#reported[0].name === data.name) {
      ArrayPrototypeShift(this.#reported);
      hasChildren = true;
    }
    const indentation = indent(data.nesting);
    return `${formatTestReport(type, data, prefix, indentation, hasChildren, false)}\n`;
  }
  #handleEvent({ type, data }) {
    switch (type) {
      case 'test:fail':
        if (data.details?.error?.failureType !== kSubtestsFailed) {
          ArrayPrototypePush(this.#failedTests, data);
        }
        return this.#handleTestReportEvent(type, data);
      case 'test:pass':
        return this.#handleTestReportEvent(type, data);
      case 'test:start':
        ArrayPrototypeUnshift(this.#stack, { __proto__: null, data, type });
        break;
      case 'test:stderr':
      case 'test:stdout':
        return data.message;
      case 'test:diagnostic':{
        const diagnosticColor = reporterColorMap[data.level] || reporterColorMap['test:diagnostic'];
        return `${diagnosticColor}${indent(data.nesting)}${reporterUnicodeSymbolMap[type]}${data.message}${colors.white}\n`;
      }
      case 'test:coverage':
        return getCoverageReport(indent(data.nesting), data.summary,
                                 reporterUnicodeSymbolMap['test:coverage'], colors.blue, true);
      case 'test:summary':
        // We report only the root test summary
        if (data.file === undefined) {
          return this.#formatFailedTestResults();
        }
    }
  }
  _transform({ type, data }, encoding, callback) {
    callback(null, this.#handleEvent({ __proto__: null, type, data }));
  }
  _flush(callback) {
    callback(null, this.#formatFailedTestResults());
  }
}

module.exports = SpecReporter;
