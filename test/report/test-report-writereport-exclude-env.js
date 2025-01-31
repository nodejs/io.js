// Flags: --report-exclude-env
'use strict';

// Test producing a report via API call, using the no-hooks/no-signal interface.
require('../common');
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const helper = require('../common/report');
const tmpdir = require('../common/tmpdir');

tmpdir.refresh();
process.report.directory = tmpdir.path;

function validate() {
  const reports = helper.findReports(process.pid, tmpdir.path);
  assert.strictEqual(reports.length, 1);
  helper.validate(reports[0], arguments[0]);
  fs.unlinkSync(reports[0]);
  return reports[0];
}

{
  // Test with no arguments.
  process.report.writeReport();
  validate();
}

{
  // Test with an error argument.
  process.report.writeReport(new Error('test error'));
  validate();
}

{
  // Test with an error with one line stack
  const error = new Error();
  error.stack = 'only one line';
  process.report.writeReport(error);
  validate();
}

{
  const error = new Error();
  error.foo = 'goo';
  process.report.writeReport(error);
  validate([['javascriptStack.errorProperties.foo', 'goo']]);
}

{
  // Test with a file argument.
  const file = process.report.writeReport('custom-name-1.json');
  const absolutePath = tmpdir.resolve(file);
  assert.strictEqual(helper.findReports(process.pid, tmpdir.path).length, 0);
  assert.strictEqual(file, 'custom-name-1.json');
  helper.validate(absolutePath);
  fs.unlinkSync(absolutePath);
}

{
  // Test with file and error arguments.
  const file = process.report.writeReport('custom-name-2.json',
                                          new Error('test error'));
  const absolutePath = tmpdir.resolve(file);
  assert.strictEqual(helper.findReports(process.pid, tmpdir.path).length, 0);
  assert.strictEqual(file, 'custom-name-2.json');
  helper.validate(absolutePath);
  fs.unlinkSync(absolutePath);
}

{
  // Test with a filename option.
  process.report.filename = 'custom-name-3.json';
  const file = process.report.writeReport();
  assert.strictEqual(helper.findReports(process.pid, tmpdir.path).length, 0);
  const filename = path.join(process.report.directory, 'custom-name-3.json');
  assert.strictEqual(file, process.report.filename);
  helper.validate(filename);
  fs.unlinkSync(filename);
}
