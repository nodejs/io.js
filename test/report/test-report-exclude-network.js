'use strict';
require('../common');
const assert = require('node:assert');
const { spawnSync } = require('node:child_process');
const tmpdir = require('../common/tmpdir');
const { describe, it, before } = require('node:test');
const fs = require('node:fs');
const helper = require('../common/report');

function validate(pid) {
  const reports = helper.findReports(pid, tmpdir.path);
  assert.strictEqual(reports.length, 1);
  let report = fs.readFileSync(reports[0], { encoding: 'utf8' });
  report = JSON.parse(report);
  assert.strictEqual(report.header.networkInterfaces, undefined);
  fs.unlinkSync(reports[0]);
}

describe('report exclude network option', () => {
  before(() => {
    tmpdir.refresh();
    process.report.directory = tmpdir.path;
  });

  it('should be configurable with --report-exclude-network', () => {
    const args = ['--report-exclude-network', '-e', 'process.report.writeReport()'];
    const child = spawnSync(process.execPath, args, { cwd: tmpdir.path });
    assert.strictEqual(child.status, 0);
    assert.strictEqual(child.signal, null);
    validate(child.pid);
  });

  it('should be configurable with report.excludeNetwork', () => {
    process.report.excludeNetwork = true;
    process.report.writeReport();
    validate(process.pid);

    const report = process.report.getReport();
    assert.strictEqual(report.header.networkInterfaces, undefined);
  });
});
