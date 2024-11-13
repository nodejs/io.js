'use strict';
require('../common');
const http = require('node:http');
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

  it('should not do DNS queries in libuv if exclude network', async () => {
    const server = http.createServer(function(req, res) {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end();
    });
    let ipv6Available = true;
    const port = await new Promise((resolve) => server.listen(0, async () => {
      await Promise.all([
        fetch('http://127.0.0.1:' + server.address().port),
        fetch('http://[::1]:' + server.address().port).catch(() => ipv6Available = false),
      ]);
      resolve(server.address().port);
      server.close();
    }));
    process.report.excludeNetwork = false;
    let report = process.report.getReport();
    let tcp = report.libuv.filter((uv) => uv.type === 'tcp' && uv.remoteEndpoint?.port === port);
    assert.strictEqual(tcp.length, ipv6Available ? 2 : 1);
    const findHandle = (local, ip4 = true) => {
      return tcp.find(
        ({ [local ? 'localEndpoint' : 'remoteEndpoint']: ep }) =>
          (ep[ip4 ? 'ip4' : 'ip6'] === (ip4 ? '127.0.0.1' : '::1')),
      )?.[local ? 'localEndpoint' : 'remoteEndpoint'];
    };
    try {
      // The reverse DNS of 127.0.0.1 can be a lot of things other than localhost
      // it could resolve to the server name for instance
      assert.notStrictEqual(findHandle(true)?.host, '127.0.0.1');
      assert.notStrictEqual(findHandle(false)?.host, '127.0.0.1');

      if (ipv6Available) {
        assert.notStrictEqual(findHandle(true, false)?.host, '::1');
        assert.notStrictEqual(findHandle(false, false)?.host, '::1');
      }
    } catch (e) {
      throw new Error(e?.message + ' in ' + JSON.stringify(tcp, null, 2), { cause: e });
    }

    process.report.excludeNetwork = true;
    report = process.report.getReport();
    tcp = report.libuv.filter((uv) => uv.type === 'tcp' && uv.remoteEndpoint?.port === port);

    try {
      assert.strictEqual(tcp.length, ipv6Available ? 2 : 1);
      assert.strictEqual(findHandle(true)?.host, '127.0.0.1');
      assert.strictEqual(findHandle(false)?.host, '127.0.0.1');

      if (ipv6Available) {
        assert.strictEqual(findHandle(true, false)?.host, '::1');
        assert.strictEqual(findHandle(false, false)?.host, '::1');
      }
    } catch (e) {
      throw new Error(e?.message + ' in ' + JSON.stringify(tcp, null, 2), { cause: e });
    }
  });
});
