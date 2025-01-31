'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');
const util = require('util');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('process.chdir is not available in Workers');
}

const traceFile = 'node_trace.1.log';

tmpdir.refresh();
process.chdir(tmpdir.path);

const test_str = 'const dns = require("dns");' +
'const options = {' +
'  family: 4,' +
'  hints: dns.ADDRCONFIG | dns.V4MAPPED,' +
'};';

const tests = {
  'lookup': 'dns.lookup("example.com", options, (err, address, family) => {});',
  'lookupService': 'dns.lookupService("127.0.0.1", 22, ' +
                   '(err, hostname, service) => {});',
  'reverse': 'dns.reverse("8.8.8.8", (err, hostnames) => {});',
  'resolveAny': 'dns.resolveAny("example.com", (err, res) => {});',
  'resolve4': 'dns.resolve4("example.com", (err, res) => {});',
  'resolve6': 'dns.resolve6("example.com", (err, res) => {});',
  'resolveCaa': 'dns.resolveCaa("example.com", (err, res) => {});',
  'resolveCname': 'dns.resolveCname("example.com", (err, res) => {});',
  'resolveMx': 'dns.resolveMx("example.com", (err, res) => {});',
  'resolveNs': 'dns.resolveNs("example.com", (err, res) => {});',
  'resolveTxt': 'dns.resolveTxt("example.com", (err, res) => {});',
  'resolveSrv': 'dns.resolveSrv("example.com", (err, res) => {});',
  'resolvePtr': 'dns.resolvePtr("example.com", (err, res) => {});',
  'resolveNaptr': 'dns.resolveNaptr("example.com", (err, res) => {});',
  'resolveSoa': 'dns.resolveSoa("example.com", (err, res) => {});',
};

for (const tr in tests) {
  const proc = cp.spawnSync(process.execPath,
                            [ '--trace-event-categories',
                              'node.dns.native',
                              '-e',
                              test_str + tests[tr],
                            ],
                            { encoding: 'utf8' });

  // Make sure the operation is successful.
  // Don't use assert with a custom message here. Otherwise the
  // inspection in the message is done eagerly and wastes a lot of CPU
  // time.
  if (proc.status !== 0) {
    throw new Error(`${tr}:\n${util.inspect(proc)}`);
  }

  const file = tmpdir.resolve(traceFile);

  const data = fs.readFileSync(file);
  const traces = JSON.parse(data.toString()).traceEvents
        .filter((trace) => trace.cat !== '__metadata');

  assert(traces.length > 0);

  // DNS native trace events should be generated.
  assert(traces.some((trace) => {
    return (trace.name === tr && trace.pid === proc.pid);
  }));
}
