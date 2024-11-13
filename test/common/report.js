'use strict';
const assert = require('assert');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');
const util = require('util');
const cpus = os.cpus();

function findReports(pid, dir) {
  // Default filenames are of the form
  // report.<date>.<time>.<pid>.<tid>.<seq>.json
  const format = '^report\\.\\d+\\.\\d+\\.' + pid + '\\.\\d+\\.\\d+\\.json$';
  const filePattern = new RegExp(format);
  const files = fs.readdirSync(dir);
  const results = [];

  files.forEach((file) => {
    if (filePattern.test(file))
      results.push(path.join(dir, file));
  });

  return results;
}

function validate(filepath, fields) {
  const report = fs.readFileSync(filepath, 'utf8');
  if (process.report.compact) {
    const end = report.indexOf('\n');
    assert.strictEqual(end, report.length - 1);
  }
  validateContent(JSON.parse(report), fields);
}

function validateContent(report, fields = []) {
  if (typeof report === 'string') {
    try {
      report = JSON.parse(report);
    } catch {
      throw new TypeError(
        'validateContent() expects a JSON string or JavaScript Object');
    }
  }
  try {
    _validateContent(report, fields);
  } catch (err) {
    try {
      err.stack += util.format('\n------\nFailing Report:\n%O', report);
    } catch {
      // Continue regardless of error.
    }
    throw err;
  }
}

function _validateContent(report, fields = []) {
  const isWindows = process.platform === 'win32';
  const isJavaScriptThreadReport = report.javascriptHeap != null;

  // Verify that all sections are present as own properties of the report.
  const sections = ['header', 'nativeStack', 'javascriptStack', 'libuv',
                    'sharedObjects', 'resourceUsage', 'workers'];

  if (!process.report.excludeEnv) {
    sections.push('environmentVariables');
  }

  if (!isWindows)
    sections.push('userLimits');

  if (report.uvthreadResourceUsage)
    sections.push('uvthreadResourceUsage');

  if (isJavaScriptThreadReport)
    sections.push('javascriptHeap');

  checkForUnknownFields(report, sections);
  sections.forEach((section) => {
    assert(Object.hasOwn(report, section));
    assert(typeof report[section] === 'object' && report[section] !== null);
  });

  fields.forEach((field) => {
    function checkLoop(actual, rest, expect) {
      actual = actual[rest.shift()];
      if (rest.length === 0 && actual !== undefined) {
        assert.strictEqual(actual, expect);
      } else {
        assert(actual);
        checkLoop(actual, rest, expect);
      }
    }
    let actual, expect;
    if (Array.isArray(field)) {
      [actual, expect] = field;
    } else {
      actual = field;
      expect = undefined;
    }
    checkLoop(report, actual.split('.'), expect);
  });

  // Verify the format of the header section.
  const header = report.header;
  const headerFields = ['event', 'trigger', 'filename', 'dumpEventTime',
                        'dumpEventTimeStamp', 'processId', 'commandLine',
                        'nodejsVersion', 'wordSize', 'arch', 'platform',
                        'componentVersions', 'release', 'osName', 'osRelease',
                        'osVersion', 'osMachine', 'cpus', 'host',
                        'glibcVersionRuntime', 'glibcVersionCompiler', 'cwd',
                        'reportVersion', 'networkInterfaces', 'threadId'];
  checkForUnknownFields(header, headerFields);
  assert.strictEqual(header.reportVersion, 4);  // Increment as needed.
  assert.strictEqual(typeof header.event, 'string');
  assert.strictEqual(typeof header.trigger, 'string');
  assert(typeof header.filename === 'string' || header.filename === null);
  assert.notStrictEqual(new Date(header.dumpEventTime).toString(),
                        'Invalid Date');
  assert(String(+header.dumpEventTimeStamp), header.dumpEventTimeStamp);
  assert(Number.isSafeInteger(header.processId));
  assert(Number.isSafeInteger(header.threadId) || header.threadId === null);
  assert.strictEqual(typeof header.cwd, 'string');
  assert(Array.isArray(header.commandLine));
  header.commandLine.forEach((arg) => {
    assert.strictEqual(typeof arg, 'string');
  });
  assert.strictEqual(header.nodejsVersion, process.version);
  assert(Number.isSafeInteger(header.wordSize));
  assert.strictEqual(header.arch, os.arch());
  assert.strictEqual(header.platform, os.platform());
  assert.deepStrictEqual(header.componentVersions, process.versions);
  assert.deepStrictEqual(header.release, process.release);
  assert.strictEqual(header.osName, os.type());
  assert.strictEqual(header.osRelease, os.release());
  assert.strictEqual(typeof header.osVersion, 'string');
  assert.strictEqual(typeof header.osMachine, 'string');
  assert(Array.isArray(header.cpus));
  assert.strictEqual(header.cpus.length, cpus.length);
  header.cpus.forEach((cpu) => {
    assert.strictEqual(typeof cpu.model, 'string');
    assert.strictEqual(typeof cpu.speed, 'number');
    assert.strictEqual(typeof cpu.user, 'number');
    assert.strictEqual(typeof cpu.nice, 'number');
    assert.strictEqual(typeof cpu.sys, 'number');
    assert.strictEqual(typeof cpu.idle, 'number');
    assert.strictEqual(typeof cpu.irq, 'number');
    assert(cpus.some((c) => {
      return c.model === cpu.model;
    }));
  });

  assert(Array.isArray(header.networkInterfaces));
  header.networkInterfaces.forEach((iface) => {
    assert.strictEqual(typeof iface.name, 'string');
    assert.strictEqual(typeof iface.internal, 'boolean');
    assert.match(iface.mac, /^([0-9A-F][0-9A-F]:){5}[0-9A-F]{2}$/i);

    if (iface.family === 'IPv4') {
      assert.strictEqual(net.isIPv4(iface.address), true);
      assert.strictEqual(net.isIPv4(iface.netmask), true);
      assert.strictEqual(iface.scopeid, undefined);
    } else if (iface.family === 'IPv6') {
      assert.strictEqual(net.isIPv6(iface.address), true);
      assert.strictEqual(net.isIPv6(iface.netmask), true);
      assert(Number.isInteger(iface.scopeid));
    } else {
      assert.strictEqual(iface.family, 'unknown');
      assert.strictEqual(iface.address, undefined);
      assert.strictEqual(iface.netmask, undefined);
      assert.strictEqual(iface.scopeid, undefined);
    }
  });
  assert.strictEqual(header.host, os.hostname());

  // Verify the format of the nativeStack section.
  assert(Array.isArray(report.nativeStack));
  report.nativeStack.forEach((frame) => {
    assert(typeof frame === 'object' && frame !== null);
    checkForUnknownFields(frame, ['pc', 'symbol']);
    assert.strictEqual(typeof frame.pc, 'string');
    assert.match(frame.pc, /^0x[0-9a-f]+$/);
    assert.strictEqual(typeof frame.symbol, 'string');
  });

  if (isJavaScriptThreadReport) {
    // Verify the format of the javascriptStack section.
    checkForUnknownFields(report.javascriptStack,
                          ['message', 'stack', 'errorProperties']);
    assert.strictEqual(typeof report.javascriptStack.errorProperties,
                       'object');
    assert.strictEqual(typeof report.javascriptStack.message, 'string');
    if (report.javascriptStack.stack !== undefined) {
      assert(Array.isArray(report.javascriptStack.stack));
      report.javascriptStack.stack.forEach((frame) => {
        assert.strictEqual(typeof frame, 'string');
      });
    }

    // Verify the format of the javascriptHeap section.
    const heap = report.javascriptHeap;
    // See `PrintGCStatistics` in node_report.cc
    const jsHeapFields = [
      'totalMemory',
      'executableMemory',
      'totalCommittedMemory',
      'availableMemory',
      'totalGlobalHandlesMemory',
      'usedGlobalHandlesMemory',
      'usedMemory',
      'memoryLimit',
      'mallocedMemory',
      'externalMemory',
      'peakMallocedMemory',
      'nativeContextCount',
      'detachedContextCount',
      'doesZapGarbage',
      'heapSpaces',
    ];
    checkForUnknownFields(heap, jsHeapFields);
    // Do not check `heapSpaces` here
    for (let i = 0; i < jsHeapFields.length - 1; i++) {
      assert(
        Number.isSafeInteger(heap[jsHeapFields[i]]),
        `heap.${jsHeapFields[i]} is not a safe integer`,
      );
    }
    assert(typeof heap.heapSpaces === 'object' && heap.heapSpaces !== null);
    const heapSpaceFields = ['memorySize', 'committedMemory', 'capacity',
                             'used', 'available'];
    Object.keys(heap.heapSpaces).forEach((spaceName) => {
      const space = heap.heapSpaces[spaceName];
      checkForUnknownFields(space, heapSpaceFields);
      heapSpaceFields.forEach((field) => {
        assert(Number.isSafeInteger(space[field]));
      });
    });
  }

  // Verify the format of the resourceUsage section.
  const usage = { ...report.resourceUsage };
  // Delete it, otherwise checkForUnknownFields will throw error
  delete usage.constrained_memory;
  const resourceUsageFields = ['userCpuSeconds', 'kernelCpuSeconds',
                               'cpuConsumptionPercent', 'userCpuConsumptionPercent',
                               'kernelCpuConsumptionPercent',
                               'maxRss', 'rss', 'free_memory', 'total_memory',
                               'available_memory', 'pageFaults', 'fsActivity'];
  checkForUnknownFields(usage, resourceUsageFields);
  assert.strictEqual(typeof usage.userCpuSeconds, 'number');
  assert.strictEqual(typeof usage.kernelCpuSeconds, 'number');
  assert.strictEqual(typeof usage.cpuConsumptionPercent, 'number');
  assert.strictEqual(typeof usage.userCpuConsumptionPercent, 'number');
  assert.strictEqual(typeof usage.kernelCpuConsumptionPercent, 'number');
  assert(typeof usage.rss, 'string');
  assert(typeof usage.maxRss, 'string');
  assert(typeof usage.free_memory, 'string');
  assert(typeof usage.total_memory, 'string');
  assert(typeof usage.available_memory, 'string');
  // This field may not exist
  if (report.resourceUsage.constrained_memory) {
    assert(typeof report.resourceUsage.constrained_memory, 'string');
  }
  assert(typeof usage.pageFaults === 'object' && usage.pageFaults !== null);
  checkForUnknownFields(usage.pageFaults, ['IORequired', 'IONotRequired']);
  assert(Number.isSafeInteger(usage.pageFaults.IORequired));
  assert(Number.isSafeInteger(usage.pageFaults.IONotRequired));
  assert(typeof usage.fsActivity === 'object' && usage.fsActivity !== null);
  checkForUnknownFields(usage.fsActivity, ['reads', 'writes']);
  assert(Number.isSafeInteger(usage.fsActivity.reads));
  assert(Number.isSafeInteger(usage.fsActivity.writes));

  // Verify the format of the uvthreadResourceUsage section, if present.
  if (report.uvthreadResourceUsage) {
    const usage = report.uvthreadResourceUsage;
    const threadUsageFields = ['userCpuSeconds', 'kernelCpuSeconds',
                               'cpuConsumptionPercent', 'fsActivity',
                               'userCpuConsumptionPercent',
                               'kernelCpuConsumptionPercent'];
    checkForUnknownFields(usage, threadUsageFields);
    assert.strictEqual(typeof usage.userCpuSeconds, 'number');
    assert.strictEqual(typeof usage.kernelCpuSeconds, 'number');
    assert.strictEqual(typeof usage.cpuConsumptionPercent, 'number');
    assert.strictEqual(typeof usage.userCpuConsumptionPercent, 'number');
    assert.strictEqual(typeof usage.kernelCpuConsumptionPercent, 'number');
    assert(typeof usage.fsActivity === 'object' && usage.fsActivity !== null);
    checkForUnknownFields(usage.fsActivity, ['reads', 'writes']);
    assert(Number.isSafeInteger(usage.fsActivity.reads));
    assert(Number.isSafeInteger(usage.fsActivity.writes));
  }

  // Verify the format of the libuv section.
  assert(Array.isArray(report.libuv));
  report.libuv.forEach((resource) => {
    assert.strictEqual(typeof resource.type, 'string');
    assert.strictEqual(typeof resource.address, 'string');
    assert.match(resource.address, /^0x[0-9a-f]+$/);
    assert.strictEqual(typeof resource.is_active, 'boolean');
    assert.strictEqual(typeof resource.is_referenced,
                       resource.type === 'loop' ? 'undefined' : 'boolean');
  });

  if (!process.report.excludeEnv) {
    // Verify the format of the environmentVariables section.
    for (const [key, value] of Object.entries(report.environmentVariables)) {
      assert.strictEqual(typeof key, 'string');
      assert.strictEqual(typeof value, 'string');
    }
  }

  // Verify the format of the userLimits section on non-Windows platforms.
  if (!isWindows) {
    const userLimitsFields = ['core_file_size_blocks', 'data_seg_size_kbytes',
                              'file_size_blocks', 'max_locked_memory_bytes',
                              'max_memory_size_kbytes', 'open_files',
                              'stack_size_bytes', 'cpu_time_seconds',
                              'max_user_processes', 'virtual_memory_kbytes'];
    checkForUnknownFields(report.userLimits, userLimitsFields);
    for (const [type, limits] of Object.entries(report.userLimits)) {
      assert.strictEqual(typeof type, 'string');
      assert(typeof limits === 'object' && limits !== null);
      checkForUnknownFields(limits, ['soft', 'hard']);
      assert(typeof limits.soft === 'number' || limits.soft === 'unlimited',
             `Invalid ${type} soft limit of ${limits.soft}`);
      assert(typeof limits.hard === 'number' || limits.hard === 'unlimited',
             `Invalid ${type} hard limit of ${limits.hard}`);
    }
  }

  // Verify the format of the sharedObjects section.
  assert(Array.isArray(report.sharedObjects));
  report.sharedObjects.forEach((sharedObject) => {
    assert.strictEqual(typeof sharedObject, 'string');
  });

  // Verify the format of the workers section.
  assert(Array.isArray(report.workers));
  report.workers.forEach((worker) => _validateContent(worker));
}

function checkForUnknownFields(actual, expected) {
  Object.keys(actual).forEach((field) => {
    assert(expected.includes(field), `'${field}' not expected in ${expected}`);
  });
}

module.exports = { findReports, validate, validateContent };
